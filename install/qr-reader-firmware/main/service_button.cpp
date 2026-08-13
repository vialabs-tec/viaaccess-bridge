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

bool ConfigLooksFactoryReset(const viaaccess::RuntimeConfig& cfg) {
  return !cfg.configured && cfg.wifi.ssid.empty() && cfg.device_key.empty();
}

void FactoryResetAndReboot() {
  ESP_LOGW(kTag, "long-press: factory reset (credentials + Wi-Fi)");
  buzzer::BeepFail();

  viaaccess::RuntimeConfig cfg = app::State::Instance().config();
  cfg = viaaccess::ResetToSetup(std::move(cfg));
  cfg.wifi.ssid.clear();
  cfg.wifi.password.clear();

  // Persist on disk only. State::SaveConfig also re-applies every driver and
  // can stall this task (BOOT then looks "dead", LED stays SYNC_STALE red).
  esp_err_t err = storage::SaveConfig(cfg);
  viaaccess::RuntimeConfig loaded = storage::LoadConfig();
  if (err != ESP_OK || !ConfigLooksFactoryReset(loaded)) {
    ESP_LOGE(kTag, "factory reset save not durable (err=%s configured=%d); wiping",
             esp_err_to_name(err), loaded.configured ? 1 : 0);
    storage::WipeProvisioning();
    err = storage::SaveConfig(cfg);
    loaded = storage::LoadConfig();
  }
  if (err != ESP_OK || !ConfigLooksFactoryReset(loaded)) {
    ESP_LOGE(kTag, "factory reset wipe fallback");
    storage::WipeProvisioning();
  }
  storage::Unmount();

  // GPIO0 is a strapping pin. Restarting while BOOT is still held puts the
  // ESP32-S3 in download mode; when the chip later boots the app, LittleFS may
  // still show the old config and the technician sees green / no SoftAP.
  ESP_LOGW(kTag, "factory reset saved; release BOOT to reboot");
  buzzer::BeepSuccess();
  while (ReadPressed()) {
    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }
  vTaskDelay(pdMS_TO_TICKS(250));
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
  // Provisioned ForcePortal: any click while SoftAP is up and STA is online
  // dismisses the open network. Unprovisioned units must keep viaaccess-setup
  // (one click used to strand the technician after Identity had already claimed).
  if (ClickWhilePortal(gesture) && wifi::connected() &&
      app::State::Instance().configured()) {
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

  // Stack sized for factory reset (save + read-back + wait for BOOT release).
  xTaskCreate(WatcherLoop, "va_boot_btn", 8192, nullptr, 3, nullptr);
  ESP_LOGI(kTag,
           "BOOT GPIO0 gestures: 1=status 2=REX(if sim) 3=SoftAP "
           "(any click dismisses SoftAP if STA up) hold5s=factory-reset");
  return ESP_OK;
}

}  // namespace service_button
