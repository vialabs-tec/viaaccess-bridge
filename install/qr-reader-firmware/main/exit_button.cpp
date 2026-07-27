#include "exit_button.hpp"

#include <atomic>
#include <mutex>

#include "app_state.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "identity_client.hpp"
#include "relay.hpp"
#include "viaaccess/exit_button.hpp"

namespace exit_button {
namespace {

constexpr const char* kTag = "rex";
constexpr int kPollMs = 50;

std::mutex g_mutex;
viaaccess::ExitButtonConfig g_config;
viaaccess::ExitButtonEngine g_engine;
std::atomic<bool> g_started{false};
bool g_ready = false;
bool g_sim_pressed = false;
int g_claimed_pin = -1;

int64_t NowMs() { return esp_timer_get_time() / 1000; }

bool LevelIsPressed(int level) {
  if (g_config.active_low) {
    return level == 0;
  }
  return level != 0;
}

bool ReadPressedLocked() {
  if (g_config.simulated) {
    return g_sim_pressed;
  }
  if (!g_ready || g_claimed_pin < 0) {
    return false;
  }
  const int level = gpio_get_level(static_cast<gpio_num_t>(g_claimed_pin));
  return LevelIsPressed(level);
}

void ReleasePinLocked() {
  if (g_claimed_pin < 0) {
    return;
  }
  gpio_reset_pin(static_cast<gpio_num_t>(g_claimed_pin));
  g_claimed_pin = -1;
}

esp_err_t ClaimPinLocked(int pin) {
  if (pin < 0 || !GPIO_IS_VALID_GPIO(pin)) {
    ESP_LOGE(kTag, "GPIO %d is not a valid input on this target", pin);
    return ESP_ERR_INVALID_ARG;
  }

  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << static_cast<unsigned>(pin);
  io.mode = GPIO_MODE_INPUT;
  // Internal pull-up: momentary button to GND, so LOW means pressed.
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;

  const esp_err_t err = gpio_config(&io);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "gpio_config failed on GPIO %d: %s", pin, esp_err_to_name(err));
    return err;
  }
  g_claimed_pin = pin;
  return ESP_OK;
}

void PublishHealthLocked() {
  app::State::Instance().set_exit_button(
      g_config.enabled, g_config.simulated, g_ready, g_config.gpio_pin,
      g_config.enabled ? viaaccess::ExitButtonStateString(g_engine.stable()) : "");
}

void HandlePress() {
  const viaaccess::RuntimeConfig cfg = app::State::Instance().config();
  if (!cfg.configured || !cfg.exit_button.enabled) {
    return;
  }

  ESP_LOGI(kTag, "pressed");

  // Identity first (grace window for the door-contact opened that follows), but
  // egress is local-first: a failed notify still pulses the lock. Only a revoked
  // device key aborts before the pulse.
  const identity::Outcome outcome = identity::PostExitButtonEvent(cfg, "pressed");
  if (outcome.unauthorized) {
    ESP_LOGE(kTag, "Identity rejected the device key on exit-button");
    app::State::Instance().EnterSetupMode("exit-button rejected device key");
    return;
  }
  if (!outcome.ok) {
    ESP_LOGW(kTag, "notify failed (continuing unlock): %s", outcome.error.c_str());
  }

  if (!cfg.unlock_webhook_url.empty()) {
    identity::UnlockWebhookPayload payload;
    payload.correlation_outcome = "EXIT_REQUEST";
    payload.access_point_slug = cfg.access_point_slug;
    const identity::UnlockWebhookResult unlock =
        identity::PostUnlockWebhook(cfg.unlock_webhook_url, payload, 8000);
    if (!unlock.ok) {
      ESP_LOGW(kTag, "unlock webhook failed: %s", unlock.error.c_str());
    }
  }

  const esp_err_t pulsed = relay::Pulse();
  if (pulsed != ESP_OK) {
    ESP_LOGW(kTag, "relay pulse failed: %s", esp_err_to_name(pulsed));
  }
}

esp_err_t ConfigureLocked(const viaaccess::ExitButtonConfig& cfg) {
  ReleasePinLocked();
  g_ready = false;
  g_config = cfg;
  g_engine.Reset(cfg.debounce_ms, cfg.cooldown_ms);
  g_sim_pressed = false;

  if (!cfg.enabled) {
    ESP_LOGW(kTag, "disabled in config");
    PublishHealthLocked();
    return ESP_OK;
  }

  if (cfg.simulated) {
    g_ready = true;
    g_engine.Seed(g_sim_pressed, NowMs());
    ESP_LOGI(kTag, "simulated button (debounce=%d ms, cooldown=%d ms)", cfg.debounce_ms,
             cfg.cooldown_ms);
    PublishHealthLocked();
    return ESP_OK;
  }

  const esp_err_t err = ClaimPinLocked(cfg.gpio_pin);
  if (err != ESP_OK) {
    PublishHealthLocked();
    return err;
  }
  g_ready = true;
  g_engine.Seed(ReadPressedLocked(), NowMs());
  ESP_LOGI(kTag, "ready on GPIO %d (activeLow=%d, state=%s)", cfg.gpio_pin,
           static_cast<int>(cfg.active_low),
           viaaccess::ExitButtonStateString(g_engine.stable()));
  PublishHealthLocked();
  return ESP_OK;
}

void WatcherLoop(void* /*argument*/) {
  for (;;) {
    bool fire = false;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_config.enabled && g_ready) {
        const int64_t now = NowMs();
        viaaccess::ExitButtonStep step = g_engine.ApplyRaw(ReadPressedLocked(), now);
        if (!step.emit_pressed) {
          step = g_engine.Tick(now);
        }
        fire = step.emit_pressed;
        PublishHealthLocked();
      }
    }
    if (fire) {
      HandlePress();
    }
    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }
}

}  // namespace

esp_err_t Start(const viaaccess::ExitButtonConfig& cfg) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const esp_err_t err = ConfigureLocked(cfg);
    if (err != ESP_OK) {
      return err;
    }
  }
  bool expected = false;
  if (g_started.compare_exchange_strong(expected, true)) {
    xTaskCreate(WatcherLoop, "va_rex", 4096, nullptr, 3, nullptr);
  }
  return ESP_OK;
}

esp_err_t ApplyConfig(const viaaccess::ExitButtonConfig& cfg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (cfg.enabled == g_config.enabled && cfg.gpio_pin == g_config.gpio_pin &&
      cfg.active_low == g_config.active_low &&
      cfg.debounce_ms == g_config.debounce_ms &&
      cfg.cooldown_ms == g_config.cooldown_ms &&
      cfg.simulated == g_config.simulated) {
    return ESP_OK;
  }
  return ConfigureLocked(cfg);
}

esp_err_t SetSimPressed(bool pressed) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_config.enabled || !g_config.simulated || !g_ready) {
    return ESP_ERR_INVALID_STATE;
  }
  g_sim_pressed = pressed;
  return ESP_OK;
}

bool enabled() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_config.enabled;
}

bool simulated() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_config.simulated;
}

bool ready() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_ready;
}

int gpio_pin() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_config.gpio_pin;
}

std::string state() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_config.enabled) {
    return "";
  }
  return viaaccess::ExitButtonStateString(g_engine.stable());
}

}  // namespace exit_button
