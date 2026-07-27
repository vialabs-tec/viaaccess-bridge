// Boot sequence for the ViaAccess QR Reader appliance on ESP32-S3.
//
// Mirrors cmd/viaaccess-qr-agent of the Go agent: load config, bring the local
// server up so /setup is always reachable, then start the online workers only
// when the appliance is provisioned. The order matters, an unprovisioned or
// offline appliance must still answer /setup and /health.
#include <string>

#include "app_state.hpp"
#include "clock_service.hpp"
#include "config_json.hpp"
#include "door_contact.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "http_server.hpp"
#include "mdns.h"
#include "nvs_flash.h"
#include "qr_reader.hpp"
#include "relay.hpp"
#include "storage.hpp"
#include "sync_task.hpp"
#include "viaaccess/config.hpp"
#include "viaaccess/version.hpp"
#include "wifi_manager.hpp"

namespace {

constexpr const char* kTag = "viaaccess";

void InitNvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // A truncated OTA or a partition table change can leave NVS unusable; the
    // device key is recoverable through reprovisioning, a brick is not.
    ESP_LOGW(kTag, "NVS unusable, erasing: %s", esp_err_to_name(err));
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

void StartMdns(const viaaccess::MdnsConfig& cfg, int port) {
  if (!cfg.enabled) {
    return;
  }
  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "mDNS init failed: %s", esp_err_to_name(err));
    return;
  }
  mdns_hostname_set(cfg.hostname.c_str());
  mdns_instance_name_set("ViaAccess QR Reader");
  mdns_service_add(nullptr, "_http", "_tcp", static_cast<uint16_t>(port), nullptr, 0);
  ESP_LOGI(kTag, "mDNS: http://%s.local:%d/setup", cfg.hostname.c_str(), port);
}

// ApplyHostname keeps the LAN name in sync when provisioning derives it from the
// access point slug, without a reboot.
void ApplyHostname(const std::string& hostname, bool enabled) {
  if (!enabled) {
    mdns_service_remove("_http", "_tcp");
    return;
  }
  mdns_hostname_set(hostname.c_str());
  ESP_LOGI(kTag, "mDNS hostname is now %s.local", hostname.c_str());
}

// MarkFirmwareValid confirms the running image after an OTA. Without this the
// bootloader rolls back on the next reset, which is exactly what should happen
// if the appliance crashed before reaching this line.
void MarkFirmwareValid() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
    return;
  }
  if (state != ESP_OTA_IMG_PENDING_VERIFY) {
    return;
  }
  ESP_LOGI(kTag, "confirming freshly flashed image");
  esp_ota_mark_app_valid_cancel_rollback();
}

}  // namespace

extern "C" void app_main() {
  ESP_LOGI(kTag, "ViaAccess QR Reader firmware %s", viaaccess::kFirmwareVersion);
  InitNvs();
  ESP_ERROR_CHECK(storage::Mount());

  app::State& state = app::State::Instance();
  state.Init(storage::LoadConfig());
  const viaaccess::RuntimeConfig boot = state.config();

  // Restoring the last snapshot before the first sync keeps /health honest about
  // policy age after a reboot, and is what contingency will read from later.
  const std::string snapshot = storage::LoadPolicySnapshot();
  viaaccess::PolicyState policy;
  if (!snapshot.empty() && config_json::ParsePolicySnapshot(snapshot, &policy)) {
    state.set_policy(policy);
  }

  // Before the radio: a DS3231 that survived the outage gives the first TLS
  // handshake a valid date instead of waiting for SNTP.
  clock_service::Init(boot.rtc);

  ESP_ERROR_CHECK_WITHOUT_ABORT(relay::Init(boot.relay));
  state.set_relay_simulated(!relay::available());
  ESP_ERROR_CHECK_WITHOUT_ABORT(door_contact::Start(boot.door_contact));

  state.set_on_config_applied([](const viaaccess::RuntimeConfig& cfg) {
    // Setup and Identity device-config can both move pins, pulse width, the
    // reader baud rate or the HTTP port; re-arm the drivers in place.
    ESP_ERROR_CHECK_WITHOUT_ABORT(relay::ApplyConfig(cfg.relay));
    app::State::Instance().set_relay_simulated(!relay::available());
    ESP_ERROR_CHECK_WITHOUT_ABORT(door_contact::ApplyConfig(cfg.door_contact));
    ESP_ERROR_CHECK_WITHOUT_ABORT(qr_reader::ApplyConfig(cfg.qr_reader));
    ESP_ERROR_CHECK_WITHOUT_ABORT(http_server::ApplyPort(cfg.http_port));
  });
  state.set_on_mdns_hostname_changed(ApplyHostname);
  state.set_on_became_operational([] { sync_task::Start(); });

  ESP_ERROR_CHECK(wifi::Start(boot.wifi));
  StartMdns(boot.mdns, boot.http_port);
  ESP_ERROR_CHECK(http_server::Start());
  ESP_ERROR_CHECK_WITHOUT_ABORT(qr_reader::Start(boot.qr_reader));

  if (boot.configured) {
    sync_task::Start();
  } else {
    ESP_LOGW(kTag, "not provisioned: connect to the %s network and open /setup",
             wifi::kSetupApSsid);
  }

  MarkFirmwareValid();
}
