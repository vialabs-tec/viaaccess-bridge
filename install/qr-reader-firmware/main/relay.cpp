#include "relay.hpp"

#include <mutex>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace relay {
namespace {

constexpr const char* kTag = "relay";

std::mutex g_mutex;
viaaccess::RelayConfig g_config;
esp_timer_handle_t g_release_timer = nullptr;
bool g_ready = false;

int IdleLevel() { return g_config.active_high ? 0 : 1; }
int ActiveLevel() { return g_config.active_high ? 1 : 0; }

void ReleaseLine(void* /*argument*/) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_ready) {
    return;
  }
  gpio_set_level(static_cast<gpio_num_t>(g_config.gpio_pin), IdleLevel());
  ESP_LOGI(kTag, "released after %d ms", g_config.pulse_ms);
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
  g_ready = false;
  g_config = cfg;

  if (!cfg.enabled) {
    ESP_LOGW(kTag, "disabled in config, unlock will not drive any GPIO");
    return ESP_OK;
  }
  if (cfg.gpio_pin < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(cfg.gpio_pin)) {
    ESP_LOGE(kTag, "GPIO %d cannot drive an output on this target", cfg.gpio_pin);
    return ESP_ERR_INVALID_ARG;
  }

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
  ESP_LOGI(kTag, "ready on GPIO %d (activeHigh=%d, pulse=%d ms)", cfg.gpio_pin,
           static_cast<int>(cfg.active_high), cfg.pulse_ms);
  return ESP_OK;
}

}  // namespace

esp_err_t Init(const viaaccess::RelayConfig& cfg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return ConfigureLocked(cfg);
}

esp_err_t ApplyConfig(const viaaccess::RelayConfig& cfg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (cfg.gpio_pin == g_config.gpio_pin && cfg.enabled == g_config.enabled &&
      cfg.active_high == g_config.active_high && cfg.pulse_ms == g_config.pulse_ms) {
    return ESP_OK;
  }
  if (g_release_timer != nullptr) {
    esp_timer_stop(g_release_timer);
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
  const esp_err_t err = esp_timer_start_once(
      g_release_timer, static_cast<uint64_t>(g_config.pulse_ms) * 1000);
  if (err != ESP_OK) {
    // Without a release timer the door would stay unlocked; fail closed.
    gpio_set_level(static_cast<gpio_num_t>(g_config.gpio_pin), IdleLevel());
    ESP_LOGE(kTag, "timer start failed, released immediately: %s", esp_err_to_name(err));
    return err;
  }
  ESP_LOGI(kTag, "pulse %d ms on GPIO %d", g_config.pulse_ms, g_config.gpio_pin);
  return ESP_OK;
}

bool available() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_ready;
}

}  // namespace relay
