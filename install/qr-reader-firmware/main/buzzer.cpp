#include "buzzer.hpp"

#include <atomic>
#include <mutex>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "viaaccess/buzzer.hpp"

namespace buzzer {
namespace {

constexpr const char* kTag = "buzzer";

std::mutex g_mutex;
viaaccess::BuzzerConfig g_config;
std::atomic<bool> g_started{false};
bool g_ready = false;
bool g_pin_claimed = false;
QueueHandle_t g_queue = nullptr;

int IdleLevel() { return g_config.active_high ? 0 : 1; }
int ActiveLevel() { return g_config.active_high ? 1 : 0; }

void DriveIdleLocked() {
  if (g_pin_claimed) {
    gpio_set_level(static_cast<gpio_num_t>(g_config.gpio_pin), IdleLevel());
  }
}

void ReleasePinLocked() {
  if (!g_pin_claimed) {
    return;
  }
  DriveIdleLocked();
  gpio_reset_pin(static_cast<gpio_num_t>(g_config.gpio_pin));
  g_pin_claimed = false;
}

esp_err_t ClaimPinLocked(int pin) {
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
  g_pin_claimed = true;
  return ESP_OK;
}

esp_err_t ConfigureLocked(const viaaccess::BuzzerConfig& cfg) {
  ReleasePinLocked();
  g_ready = false;
  g_config = cfg;
  if (!cfg.enabled) {
    ESP_LOGW(kTag, "disabled in config");
    return ESP_OK;
  }
  const esp_err_t err = ClaimPinLocked(cfg.gpio_pin);
  if (err != ESP_OK) {
    return err;
  }
  g_ready = true;
  ESP_LOGI(kTag, "ready on GPIO %d (activeHigh=%d)", cfg.gpio_pin,
           cfg.active_high ? 1 : 0);
  return ESP_OK;
}

bool PlayPlan(const viaaccess::BeepPlan& plan) {
  if (plan.pulses <= 0 || plan.on_ms <= 0) {
    return true;
  }
  for (int i = 0; i < plan.pulses; ++i) {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_ready || !g_pin_claimed) {
        return false;
      }
      gpio_set_level(static_cast<gpio_num_t>(g_config.gpio_pin), ActiveLevel());
    }
    vTaskDelay(pdMS_TO_TICKS(plan.on_ms));
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      DriveIdleLocked();
    }
    if (i + 1 < plan.pulses && plan.off_ms > 0) {
      vTaskDelay(pdMS_TO_TICKS(plan.off_ms));
    }
  }
  return true;
}

void Silence() {
  std::lock_guard<std::mutex> lock(g_mutex);
  DriveIdleLocked();
}

void Worker(void* /*arg*/) {
  bool held_alarm = false;
  viaaccess::BeepKind kind = viaaccess::BeepKind::kStop;
  while (true) {
    const TickType_t wait = held_alarm ? 0 : portMAX_DELAY;
    if (xQueueReceive(g_queue, &kind, wait) == pdTRUE) {
      viaaccess::BeepKind newer = kind;
      while (xQueueReceive(g_queue, &newer, 0) == pdTRUE) {
        kind = newer;
      }
      if (kind == viaaccess::BeepKind::kStop) {
        held_alarm = false;
        Silence();
        continue;
      }
      if (kind == viaaccess::BeepKind::kHeldOpen) {
        held_alarm = true;
        ESP_LOGI(kTag, "held-open alarm");
      } else {
        held_alarm = false;
        PlayPlan(viaaccess::PlanForBeep(kind));
        continue;
      }
    }

    if (held_alarm) {
      const viaaccess::BeepPlan plan = viaaccess::PlanForBeep(viaaccess::BeepKind::kHeldOpen);
      if (!PlayPlan(plan)) {
        held_alarm = false;
        continue;
      }
      if (plan.off_ms > 0) {
        // Abort the pause early when Stop / a new cue arrives.
        if (xQueueReceive(g_queue, &kind, pdMS_TO_TICKS(plan.off_ms)) == pdTRUE) {
          viaaccess::BeepKind newer = kind;
          while (xQueueReceive(g_queue, &newer, 0) == pdTRUE) {
            kind = newer;
          }
          if (kind == viaaccess::BeepKind::kStop) {
            held_alarm = false;
            Silence();
          } else if (kind == viaaccess::BeepKind::kHeldOpen) {
            held_alarm = true;
          } else {
            held_alarm = false;
            PlayPlan(viaaccess::PlanForBeep(kind));
          }
        }
      }
    }
  }
}

void Enqueue(viaaccess::BeepKind kind) {
  if (g_queue == nullptr) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready && kind != viaaccess::BeepKind::kStop) {
      return;
    }
  }
  xQueueOverwrite(g_queue, &kind);
}

}  // namespace

esp_err_t Start(const viaaccess::BuzzerConfig& cfg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_started.exchange(true)) {
    g_queue = xQueueCreate(1, sizeof(viaaccess::BeepKind));
    if (g_queue == nullptr) {
      g_started = false;
      return ESP_ERR_NO_MEM;
    }
    const BaseType_t ok = xTaskCreate(Worker, "buzzer", 2048, nullptr, 5, nullptr);
    if (ok != pdPASS) {
      vQueueDelete(g_queue);
      g_queue = nullptr;
      g_started = false;
      return ESP_ERR_NO_MEM;
    }
  }
  return ConfigureLocked(cfg);
}

esp_err_t ApplyConfig(const viaaccess::BuzzerConfig& cfg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_started) {
    return ESP_ERR_INVALID_STATE;
  }
  return ConfigureLocked(cfg);
}

void BeepSuccess() { Enqueue(viaaccess::BeepKind::kSuccess); }

void BeepFail() { Enqueue(viaaccess::BeepKind::kFail); }

void BeepHeldOpen() { Enqueue(viaaccess::BeepKind::kHeldOpen); }

void Stop() { Enqueue(viaaccess::BeepKind::kStop); }

bool available() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_ready;
}

}  // namespace buzzer
