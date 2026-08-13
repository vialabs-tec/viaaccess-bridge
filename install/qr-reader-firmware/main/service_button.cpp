#include "service_button.hpp"

#include <atomic>

#include "app_state.hpp"
#include "buzzer.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "exit_button.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage.hpp"
#include "viaaccess/config.hpp"
#include "viaaccess/mode.hpp"
#include "viaaccess/service_button.hpp"
#include "wifi_manager.hpp"

namespace service_button {
namespace {

constexpr const char* kTag = "boot_btn";
// ESP32-S3-DevKitC-1 BOOT button (strapping pin; sampled only after app start).
constexpr gpio_num_t kBootGpio = GPIO_NUM_0;
constexpr int kPollMs = 20;

std::atomic<bool> g_started{false};
viaaccess::ServiceButtonEngine g_engine;

int64_t NowMs() { return esp_timer_get_time() / 1000; }

bool ReadPressed() {
  // Active low with internal pull-up (DevKit BOOT to GND).
  return gpio_get_level(kBootGpio) == 0;
}

void AnnounceMode() {
  const viaaccess::OperationMode mode = app::State::Instance().operation_mode();
  ESP_LOGI(kTag, "status gesture: mode=%s portal=%d", viaaccess::ModeString(mode),
           wifi::portal_active() ? 1 : 0);
  switch (mode) {
    case viaaccess::OperationMode::kOnline:
      buzzer::BeepSuccess();
      break;
    case viaaccess::OperationMode::kSetup:
      // Two short fails read as "not online yet / portal".
      buzzer::BeepFail();
      break;
    case viaaccess::OperationMode::kContingency:
    case viaaccess::OperationMode::kSyncStale:
      buzzer::BeepFail();
      break;
  }
}

void OpenSetupPortal() {
  ESP_LOGW(kTag, "triple-click: forcing SoftAP portal");
  const esp_err_t err = wifi::ForcePortal();
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "ForcePortal failed: %s", esp_err_to_name(err));
    buzzer::BeepFail();
    return;
  }
  buzzer::BeepSuccess();
}

void SyntheticRex() {
  // Lab-only: Fiação must enable REX and turn on Simular before BOOT can unlock.
  if (!exit_button::enabled() || !exit_button::simulated()) {
    ESP_LOGW(kTag, "double-click REX ignored (enable REX + Simular in Fiação)");
    buzzer::BeepFail();
    return;
  }
  ESP_LOGW(kTag, "double-click: synthetic REX");
  const esp_err_t err = exit_button::TriggerPress();
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "REX unavailable (%s)", esp_err_to_name(err));
    buzzer::BeepFail();
  }
}

void FactoryResetAndReboot() {
  ESP_LOGW(kTag, "long-press: factory reset (credentials + Wi-Fi)");
  buzzer::BeepFail();

  viaaccess::RuntimeConfig cfg = app::State::Instance().config();
  cfg = viaaccess::ResetToSetup(std::move(cfg));
  cfg.wifi.ssid.clear();
  cfg.wifi.password.clear();

  const esp_err_t err = storage::SaveConfig(cfg);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "factory reset save failed: %s", esp_err_to_name(err));
    return;
  }

  // Brief pause so the fail cue can finish before the chip resets.
  vTaskDelay(pdMS_TO_TICKS(400));
  esp_restart();
}

void DismissPortalFromBoot() {
  ESP_LOGW(kTag, "BOOT: dismissing SoftAP portal");
  if (wifi::DismissPortal()) {
    buzzer::BeepSuccess();
    return;
  }
  buzzer::BeepFail();
}

bool ClickWhilePortal(viaaccess::ServiceGesture gesture) {
  switch (gesture) {
    case viaaccess::ServiceGesture::kSingleClick:
    case viaaccess::ServiceGesture::kDoubleClick:
    case viaaccess::ServiceGesture::kTripleClick:
      return wifi::portal_active();
    default:
      return false;
  }
}

void HandleGesture(viaaccess::ServiceGesture gesture) {
  // Any click burst while the setup AP is up and the station is online: leave
  // provisioning. Hold stays factory-reset so a long press cannot be mistaken
  // for "just close the portal".
  if (ClickWhilePortal(gesture) && wifi::connected()) {
    DismissPortalFromBoot();
    return;
  }
  switch (gesture) {
    case viaaccess::ServiceGesture::kNone:
      break;
    case viaaccess::ServiceGesture::kSingleClick:
      AnnounceMode();
      break;
    case viaaccess::ServiceGesture::kDoubleClick:
      SyntheticRex();
      break;
    case viaaccess::ServiceGesture::kTripleClick:
      OpenSetupPortal();
      break;
    case viaaccess::ServiceGesture::kLongPressArmed:
      ESP_LOGW(kTag, "long-press armed — keep holding to factory reset");
      buzzer::BeepFail();
      break;
    case viaaccess::ServiceGesture::kLongPress:
      FactoryResetAndReboot();
      break;
  }
}

void WatcherLoop(void* /*argument*/) {
  g_engine.Reset({});
  // Seed without treating a stuck BOOT at boot as a gesture.
  g_engine.ApplyRaw(ReadPressed(), NowMs());

  for (;;) {
    const viaaccess::ServiceGesture gesture = g_engine.ApplyRaw(ReadPressed(), NowMs());
    if (gesture != viaaccess::ServiceGesture::kNone) {
      HandleGesture(gesture);
    }
    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }
}

}  // namespace

esp_err_t Start() {
  bool expected = false;
  if (!g_started.compare_exchange_strong(expected, true)) {
    return ESP_OK;
  }

  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << static_cast<unsigned>(kBootGpio);
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  const esp_err_t err = gpio_config(&io);
  if (err != ESP_OK) {
    g_started.store(false);
    ESP_LOGE(kTag, "gpio_config BOOT failed: %s", esp_err_to_name(err));
    return err;
  }

  // Stack sized for factory reset (save + restart) and REX notify.
  xTaskCreate(WatcherLoop, "va_boot_btn", 4096, nullptr, 3, nullptr);
  ESP_LOGI(kTag,
           "BOOT GPIO0 gestures: 1=status 2=REX(if sim) 3=SoftAP "
           "(any click dismisses SoftAP if STA up) hold5s=factory-reset");
  return ESP_OK;
}

}  // namespace service_button
