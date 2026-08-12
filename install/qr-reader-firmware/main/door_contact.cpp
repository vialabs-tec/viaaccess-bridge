#include "door_contact.hpp"

#include <atomic>
#include <mutex>

#include "app_state.hpp"
#include "buzzer.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "identity_client.hpp"
#include "relay.hpp"
#include "viaaccess/door_contact.hpp"

namespace door_contact {
namespace {

constexpr const char* kTag = "door";
constexpr int kPollMs = 50;

std::mutex g_mutex;
viaaccess::DoorContactConfig g_config;
viaaccess::DoorContactEngine g_engine;
std::atomic<bool> g_started{false};
bool g_ready = false;
bool g_sim_open = false;
int g_claimed_pin = -1;

int64_t NowMs() { return esp_timer_get_time() / 1000; }

bool LevelIsOpen(int level) {
  if (g_config.active_low) {
    return level != 0;
  }
  return level == 0;
}

bool ReadOpenLocked() {
  if (g_config.simulated) {
    return g_sim_open;
  }
  if (!g_ready || g_claimed_pin < 0) {
    return false;
  }
  const int level = gpio_get_level(static_cast<gpio_num_t>(g_claimed_pin));
  return LevelIsOpen(level);
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
  // Internal pull-up: MC38 NF closes to GND, so HIGH means the door is open.
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
  app::State::Instance().set_door_contact(
      g_config.enabled, g_config.simulated, g_ready, g_config.gpio_pin,
      g_config.enabled ? viaaccess::DoorStateString(g_engine.stable()) : "");
}

void MergeSteps(viaaccess::DoorContactStep* into, const viaaccess::DoorContactStep& extra) {
  for (int i = 0; i < extra.count; ++i) {
    into->Push(extra.kinds[i]);
  }
}

void Emit(const viaaccess::DoorContactStep& step) {
  if (step.count == 0) {
    return;
  }

  const viaaccess::RuntimeConfig cfg = app::State::Instance().config();
  if (!cfg.configured || !cfg.door_contact.enabled) {
    return;
  }

  for (int i = 0; i < step.count; ++i) {
    const viaaccess::DoorKind door_kind = step.kinds[i];
    const char* kind = viaaccess::DoorKindString(door_kind);
    ESP_LOGI(kTag, "event %s", kind);

    // Local held-open alarm is the main buzzer job: repeat until the door
    // closes, independent of whether Identity accepted the event.
    if (door_kind == viaaccess::DoorKind::kHeldOpen) {
      buzzer::BeepHeldOpen();
    } else if (door_kind == viaaccess::DoorKind::kOpened) {
      relay::OnDoorOpen();
    } else if (door_kind == viaaccess::DoorKind::kClosed) {
      buzzer::Stop();
      relay::OnDoorClosed();
    }

    const identity::Outcome outcome = identity::PostDoorContactEvent(cfg, kind);
    if (outcome.unauthorized) {
      ESP_LOGE(kTag, "Identity rejected the device key on door-contact");
      app::State::Instance().EnterSetupMode("door-contact rejected device key");
      return;
    }
    if (!outcome.ok) {
      ESP_LOGW(kTag, "post %s failed: %s", kind, outcome.error.c_str());
    }
  }
}

esp_err_t ConfigureLocked(const viaaccess::DoorContactConfig& cfg) {
  ReleasePinLocked();
  g_ready = false;
  g_config = cfg;
  g_engine.Reset(cfg.debounce_ms, cfg.held_open_after_ms);
  g_sim_open = false;

  if (!cfg.enabled) {
    ESP_LOGW(kTag, "disabled in config");
    PublishHealthLocked();
    return ESP_OK;
  }

  if (cfg.simulated) {
    g_ready = true;
    g_engine.Seed(g_sim_open, NowMs());
    ESP_LOGI(kTag, "simulated reed (debounce=%d ms, held=%d ms)", cfg.debounce_ms,
             cfg.held_open_after_ms);
    PublishHealthLocked();
    return ESP_OK;
  }

  const esp_err_t err = ClaimPinLocked(cfg.gpio_pin);
  if (err != ESP_OK) {
    PublishHealthLocked();
    return err;
  }
  g_ready = true;
  g_engine.Seed(ReadOpenLocked(), NowMs());
  ESP_LOGI(kTag, "ready on GPIO %d (activeLow=%d, state=%s)", cfg.gpio_pin,
           static_cast<int>(cfg.active_low),
           viaaccess::DoorStateString(g_engine.stable()));
  PublishHealthLocked();
  return ESP_OK;
}

void WatcherLoop(void* /*argument*/) {
  for (;;) {
    viaaccess::DoorContactStep step;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_config.enabled && g_ready) {
        const int64_t now = NowMs();
        // ApplyRaw starts (or refreshes) the debounce window; Tick commits it
        // once the window elapses and fires held_open when due. Both must run
        // every poll: ApplyRaw alone never commits a non-zero debounce.
        step = g_engine.ApplyRaw(ReadOpenLocked(), now);
        MergeSteps(&step, g_engine.Tick(now));
        PublishHealthLocked();
      }
    }
    // Emit outside the mutex: the Identity POST can take seconds and must not
    // stall SetSimOpen or ApplyConfig.
    Emit(step);
    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }
}

}  // namespace

esp_err_t Start(const viaaccess::DoorContactConfig& cfg) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const esp_err_t err = ConfigureLocked(cfg);
    if (err != ESP_OK) {
      return err;
    }
  }
  bool expected = false;
  if (g_started.compare_exchange_strong(expected, true)) {
    xTaskCreate(WatcherLoop, "va_door", 4096, nullptr, 3, nullptr);
  }
  return ESP_OK;
}

esp_err_t ApplyConfig(const viaaccess::DoorContactConfig& cfg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (cfg.enabled == g_config.enabled && cfg.gpio_pin == g_config.gpio_pin &&
      cfg.active_low == g_config.active_low &&
      cfg.debounce_ms == g_config.debounce_ms &&
      cfg.held_open_after_ms == g_config.held_open_after_ms &&
      cfg.simulated == g_config.simulated) {
    return ESP_OK;
  }
  return ConfigureLocked(cfg);
}

esp_err_t SetSimOpen(bool open) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_config.enabled || !g_config.simulated || !g_ready) {
    return ESP_ERR_INVALID_STATE;
  }
  g_sim_open = open;
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
  return viaaccess::DoorStateString(g_engine.stable());
}

bool is_open() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_config.enabled && g_ready &&
         g_engine.stable() == viaaccess::DoorState::kOpen;
}

}  // namespace door_contact
