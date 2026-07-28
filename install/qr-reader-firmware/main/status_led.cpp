#include "status_led.hpp"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

#include "app_state.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "viaaccess/status_led.hpp"

namespace status_led {
namespace {

constexpr const char* kTag = "led";
constexpr int kPollMs = 500;

std::mutex g_mutex;
viaaccess::StatusLedConfig g_config;
viaaccess::LedPattern g_pattern;
std::atomic<bool> g_started{false};
bool g_ready = false;
bool g_blink_on = true;
bool g_pins_claimed = false;
char g_pattern_name[24] = "UNKNOWN";

int IdleLevel() { return g_config.active_high ? 0 : 1; }
int ActiveLevel() { return g_config.active_high ? 1 : 0; }

int Level(bool on) { return on ? ActiveLevel() : IdleLevel(); }

void ReleasePinsLocked() {
  if (!g_pins_claimed) {
    return;
  }
  gpio_set_level(static_cast<gpio_num_t>(g_config.red_pin), IdleLevel());
  gpio_set_level(static_cast<gpio_num_t>(g_config.green_pin), IdleLevel());
  gpio_set_level(static_cast<gpio_num_t>(g_config.blue_pin), IdleLevel());
  gpio_reset_pin(static_cast<gpio_num_t>(g_config.red_pin));
  gpio_reset_pin(static_cast<gpio_num_t>(g_config.green_pin));
  gpio_reset_pin(static_cast<gpio_num_t>(g_config.blue_pin));
  g_pins_claimed = false;
}

esp_err_t ClaimPin(int pin) {
  if (pin < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
    ESP_LOGE(kTag, "GPIO %d cannot drive an output on this target", pin);
    return ESP_ERR_INVALID_ARG;
  }
  gpio_set_level(static_cast<gpio_num_t>(pin), IdleLevel());

  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << static_cast<unsigned>(pin);
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  const esp_err_t err = gpio_config(&io);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "gpio_config failed on GPIO %d: %s", pin, esp_err_to_name(err));
    return err;
  }
  gpio_set_level(static_cast<gpio_num_t>(pin), IdleLevel());
  return ESP_OK;
}

void DriveLocked(bool red, bool green, bool blue) {
  if (!g_ready || !g_pins_claimed) {
    return;
  }
  gpio_set_level(static_cast<gpio_num_t>(g_config.red_pin), Level(red));
  gpio_set_level(static_cast<gpio_num_t>(g_config.green_pin), Level(green));
  gpio_set_level(static_cast<gpio_num_t>(g_config.blue_pin), Level(blue));
}

void PublishHealthLocked() {
  app::State::Instance().set_status_led(
      g_config.enabled, g_ready, g_pattern_name, g_pattern.red, g_pattern.green,
      g_pattern.blue, g_pattern.blink);
}

void ApplyPatternLocked(const viaaccess::LedPattern& next) {
  const bool changed = std::strcmp(g_pattern_name, next.name) != 0 ||
                       g_pattern.red != next.red || g_pattern.green != next.green ||
                       g_pattern.blue != next.blue || g_pattern.blink != next.blink;
  g_pattern = next;
  std::strncpy(g_pattern_name, next.name, sizeof(g_pattern_name) - 1);
  g_pattern_name[sizeof(g_pattern_name) - 1] = '\0';
  g_blink_on = true;
  if (changed) {
    ESP_LOGI(kTag, "pattern %s", g_pattern_name);
  }
  DriveLocked(next.red, next.green, next.blue);
  PublishHealthLocked();
}

esp_err_t ConfigureLocked(const viaaccess::StatusLedConfig& cfg) {
  ReleasePinsLocked();
  g_ready = false;
  g_config = cfg;
  g_pattern = {};
  std::strncpy(g_pattern_name, "UNKNOWN", sizeof(g_pattern_name));
  g_blink_on = true;

  if (!cfg.enabled) {
    ESP_LOGW(kTag, "disabled in config");
    PublishHealthLocked();
    return ESP_OK;
  }

  esp_err_t err = ClaimPin(cfg.red_pin);
  if (err != ESP_OK) {
    PublishHealthLocked();
    return err;
  }
  err = ClaimPin(cfg.green_pin);
  if (err != ESP_OK) {
    gpio_reset_pin(static_cast<gpio_num_t>(cfg.red_pin));
    PublishHealthLocked();
    return err;
  }
  err = ClaimPin(cfg.blue_pin);
  if (err != ESP_OK) {
    gpio_reset_pin(static_cast<gpio_num_t>(cfg.red_pin));
    gpio_reset_pin(static_cast<gpio_num_t>(cfg.green_pin));
    PublishHealthLocked();
    return err;
  }
  g_pins_claimed = true;
  g_ready = true;
  ESP_LOGI(kTag, "KY-016 ready on R=%d G=%d B=%d (activeHigh=%d)", cfg.red_pin,
           cfg.green_pin, cfg.blue_pin, static_cast<int>(cfg.active_high));
  PublishHealthLocked();
  return ESP_OK;
}

void Loop(void* /*argument*/) {
  for (;;) {
    const viaaccess::OperationMode mode = app::State::Instance().operation_mode();
    const viaaccess::LedPattern next = viaaccess::PatternForMode(mode);

    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_config.enabled || !g_ready) {
        // still publish so /health stays honest while disabled
      } else {
        const bool same = std::strcmp(g_pattern_name, next.name) == 0 &&
                          g_pattern.red == next.red && g_pattern.green == next.green &&
                          g_pattern.blue == next.blue && g_pattern.blink == next.blink;
        if (!same) {
          ApplyPatternLocked(next);
        } else if (g_pattern.blink) {
          g_blink_on = !g_blink_on;
          if (g_blink_on) {
            DriveLocked(g_pattern.red, g_pattern.green, g_pattern.blue);
          } else {
            DriveLocked(false, false, false);
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }
}

}  // namespace

esp_err_t Start(const viaaccess::StatusLedConfig& cfg) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const esp_err_t err = ConfigureLocked(cfg);
    if (err != ESP_OK) {
      return err;
    }
  }
  bool expected = false;
  if (g_started.compare_exchange_strong(expected, true)) {
    xTaskCreate(Loop, "va_led", 3072, nullptr, 2, nullptr);
  }
  // Apply the first pattern immediately so SETUP blinks before the first tick.
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_ready) {
      ApplyPatternLocked(viaaccess::PatternForMode(app::State::Instance().operation_mode()));
    }
  }
  return ESP_OK;
}

esp_err_t ApplyConfig(const viaaccess::StatusLedConfig& cfg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (cfg.enabled == g_config.enabled && cfg.red_pin == g_config.red_pin &&
      cfg.green_pin == g_config.green_pin && cfg.blue_pin == g_config.blue_pin &&
      cfg.active_high == g_config.active_high) {
    return ESP_OK;
  }
  const esp_err_t err = ConfigureLocked(cfg);
  if (err == ESP_OK && g_ready) {
    ApplyPatternLocked(viaaccess::PatternForMode(app::State::Instance().operation_mode()));
  }
  return err;
}

bool enabled() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_config.enabled;
}

bool ready() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_ready;
}

}  // namespace status_led
