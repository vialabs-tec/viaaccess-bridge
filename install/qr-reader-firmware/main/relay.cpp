#include "relay.hpp"

#include <mutex>
#include <string>

#include "driver/gpio.h"
#include "door_contact.hpp"
#include "esp_log.h"
#include "esp_timer.h"

namespace relay {
namespace {

constexpr const char* kTag = "relay";

enum class UntilPhase {
  kIdle,
  kWaitingOpen,
  kWaitingClose,
};

std::mutex g_mutex;
viaaccess::RelayConfig g_config;
esp_timer_handle_t g_release_timer = nullptr;
bool g_ready = false;
bool g_active = false;
UntilPhase g_until_phase = UntilPhase::kIdle;

int IdleLevel() { return g_config.active_high ? 0 : 1; }
int ActiveLevel() { return g_config.active_high ? 1 : 0; }

bool IsUntilClosedLocked() {
  return g_config.unlock_mode == viaaccess::kRelayUnlockModeUntilClosed;
}

void DriveIdleLocked() {
  if (!g_ready) {
    return;
  }
  gpio_set_level(static_cast<gpio_num_t>(g_config.gpio_pin), IdleLevel());
  g_active = false;
  g_until_phase = UntilPhase::kIdle;
}

void ReleaseLine(void* /*argument*/) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_ready || !g_active) {
    return;
  }
  DriveIdleLocked();
  ESP_LOGI(kTag, "released after timeout (%d ms, mode=%s)", g_config.pulse_ms,
           g_config.unlock_mode.c_str());
}

esp_err_t EnsureTimer() {
  if (g_release_timer != nullptr) {
    return ESP_OK;
  }
  const esp_timer_create_args_t args = {
      .callback = ReleaseLine,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "relay_release",
      .skip_unhandled_events = true,
  };
  return esp_timer_create(&args, &g_release_timer);
}

esp_err_t ConfigureLocked(const viaaccess::RelayConfig& cfg) {
  if (g_release_timer != nullptr) {
    esp_timer_stop(g_release_timer);
  }
  DriveIdleLocked();
  g_ready = false;
  g_config = cfg;
  g_config.unlock_mode = viaaccess::NormalizeRelayUnlockMode(cfg.unlock_mode);

  if (!cfg.enabled) {
    ESP_LOGW(kTag, "disabled in config, unlock will not drive any GPIO");
    return ESP_OK;
  }
  if (cfg.gpio_pin < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(cfg.gpio_pin)) {
    ESP_LOGE(kTag, "GPIO %d cannot drive an output on this target", cfg.gpio_pin);
    return ESP_ERR_INVALID_ARG;
  }

  // Load the idle level before the pin becomes an output. The output register
  // starts at 0, which is the active level on a low-triggered board, so
  // configuring first would drive the coil until the set_level below lands.
  gpio_set_level(static_cast<gpio_num_t>(cfg.gpio_pin), IdleLevel());

  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << static_cast<unsigned>(cfg.gpio_pin);
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;

  esp_err_t err = gpio_config(&io);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "gpio_config failed on GPIO %d: %s", cfg.gpio_pin,
             esp_err_to_name(err));
    return err;
  }
  err = EnsureTimer();
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "timer create failed: %s", esp_err_to_name(err));
    return err;
  }

  g_ready = true;
  gpio_set_level(static_cast<gpio_num_t>(cfg.gpio_pin), IdleLevel());
  ESP_LOGI(kTag, "ready on GPIO %d (activeHigh=%d, mode=%s, pulse=%d ms)", cfg.gpio_pin,
           static_cast<int>(cfg.active_high), g_config.unlock_mode.c_str(), cfg.pulse_ms);
  return ESP_OK;
}

}  // namespace

esp_err_t Init(const viaaccess::RelayConfig& cfg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return ConfigureLocked(cfg);
}

esp_err_t ApplyConfig(const viaaccess::RelayConfig& cfg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const std::string mode = viaaccess::NormalizeRelayUnlockMode(cfg.unlock_mode);
  if (cfg.gpio_pin == g_config.gpio_pin && cfg.enabled == g_config.enabled &&
      cfg.active_high == g_config.active_high && cfg.pulse_ms == g_config.pulse_ms &&
      mode == g_config.unlock_mode) {
    return ESP_OK;
  }
  return ConfigureLocked(cfg);
}

esp_err_t Pulse() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_ready) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_timer_stop(g_release_timer);
  gpio_set_level(static_cast<gpio_num_t>(g_config.gpio_pin), ActiveLevel());
  g_active = true;
  if (IsUntilClosedLocked()) {
    // Door already open (held or propped): skip straight to waiting for close.
    g_until_phase = door_contact::is_open() ? UntilPhase::kWaitingClose
                                            : UntilPhase::kWaitingOpen;
  } else {
    g_until_phase = UntilPhase::kIdle;
  }

  const esp_err_t err = esp_timer_start_once(
      g_release_timer, static_cast<uint64_t>(g_config.pulse_ms) * 1000);
  if (err != ESP_OK) {
    // Without a release timer the door would stay unlocked; fail closed.
    DriveIdleLocked();
    ESP_LOGE(kTag, "timer start failed, released immediately: %s", esp_err_to_name(err));
    return err;
  }
  ESP_LOGI(kTag, "unlock %d ms on GPIO %d (mode=%s)", g_config.pulse_ms, g_config.gpio_pin,
           g_config.unlock_mode.c_str());
  return ESP_OK;
}

void OnDoorOpen() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_ready || !g_active || !IsUntilClosedLocked()) {
    return;
  }
  if (g_until_phase == UntilPhase::kWaitingOpen) {
    g_until_phase = UntilPhase::kWaitingClose;
    ESP_LOGI(kTag, "until_closed: door opened, waiting for close");
  }
}

void OnDoorClosed() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_ready || !g_active || !IsUntilClosedLocked()) {
    return;
  }
  // If the door was already open when unlocked, the first closed after open
  // re-locks. A close while still waiting for open is ignored (door stayed shut).
  if (g_until_phase != UntilPhase::kWaitingClose) {
    return;
  }
  esp_timer_stop(g_release_timer);
  DriveIdleLocked();
  ESP_LOGI(kTag, "until_closed: door closed, released");
}

bool available() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_ready;
}

}  // namespace relay
