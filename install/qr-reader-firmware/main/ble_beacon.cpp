#include "ble_beacon.hpp"

#include <cstring>
#include <mutex>

#include "esp_log.h"
#include "sdkconfig.h"

#if defined(CONFIG_BT_NIMBLE_ENABLED) && CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#define VA_BLE_NIMBLE 1
#else
#define VA_BLE_NIMBLE 0
#endif

namespace ble_beacon {
namespace {

constexpr const char* kTag = "ble_beacon";

// ViaAccess company ID (ASCII "VA") + iBeacon-compatible type/length payload.
// Do NOT use Apple 0x004C: iOS CoreBluetooth strips/hides true iBeacon mfg data,
// so react-native-ble-plx on iPhone would never see the door beacon.
constexpr uint16_t kViaAccessCompanyId = 0x5641;
constexpr uint8_t kIBeaconType = 0x02;
constexpr uint8_t kIBeaconLength = 0x15;
constexpr std::size_t kMfgDataLen = 25;  // company(2) + type/len(2) + uuid(16) + maj/min(4) + tx(1)

std::mutex g_mutex;
viaaccess::BleBeaconConfig g_config;
bool g_advertising = false;
bool g_stack_ready = false;
bool g_stack_failed = false;
bool g_init_started = false;
bool g_pending_start = false;

#if VA_BLE_NIMBLE

bool ParseUuidBytes(const std::string& uuid, uint8_t out[16]) {
  if (uuid.size() != 36) {
    return false;
  }
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
      return 10 + (c - 'A');
    }
    return -1;
  };
  std::size_t out_i = 0;
  for (std::size_t i = 0; i < uuid.size();) {
    if (uuid[i] == '-') {
      ++i;
      continue;
    }
    if (i + 1 >= uuid.size() || out_i >= 16) {
      return false;
    }
    const int hi = nibble(uuid[i]);
    const int lo = nibble(uuid[i + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    out[out_i++] = static_cast<uint8_t>((hi << 4) | lo);
    i += 2;
  }
  return out_i == 16;
}

void BuildMfgData(const viaaccess::BleBeaconConfig& cfg, uint8_t out[kMfgDataLen]) {
  uint8_t uuid[16] = {};
  ParseUuidBytes(cfg.uuid, uuid);
  out[0] = static_cast<uint8_t>(kViaAccessCompanyId & 0xff);
  out[1] = static_cast<uint8_t>((kViaAccessCompanyId >> 8) & 0xff);
  out[2] = kIBeaconType;
  out[3] = kIBeaconLength;
  std::memcpy(out + 4, uuid, 16);
  out[20] = static_cast<uint8_t>((cfg.major >> 8) & 0xff);
  out[21] = static_cast<uint8_t>(cfg.major & 0xff);
  out[22] = static_cast<uint8_t>((cfg.minor >> 8) & 0xff);
  out[23] = static_cast<uint8_t>(cfg.minor & 0xff);
  out[24] = static_cast<uint8_t>(static_cast<int8_t>(cfg.tx_power));
}

int StopAdvertisingLocked() {
  if (!g_advertising) {
    return 0;
  }
  const int rc = ble_gap_adv_stop();
  g_advertising = false;
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    ESP_LOGW(kTag, "ble_gap_adv_stop failed: %d", rc);
    return rc;
  }
  return 0;
}

int StartAdvertisingLocked() {
  if (!g_config.enabled || g_config.uuid.empty()) {
    return StopAdvertisingLocked();
  }
  if (!g_stack_ready) {
    g_pending_start = true;
    return 0;
  }

  uint8_t mfg[kMfgDataLen];
  BuildMfgData(g_config, mfg);

  struct ble_hs_adv_fields fields = {};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.mfg_data = mfg;
  fields.mfg_data_len = kMfgDataLen;

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(kTag, "ble_gap_adv_set_fields failed: %d", rc);
    g_advertising = false;
    return rc;
  }

  struct ble_gap_adv_params adv_params = {};
  adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  // ~100 ms interval keeps the member app responsive without saturating the radio.
  adv_params.itvl_min = 160;  // 160 * 0.625 ms = 100 ms
  adv_params.itvl_max = 160;

  uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;
  rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
    ESP_LOGE(kTag, "ble_hs_id_infer_auto failed: %d", rc);
    return rc;
  }

  StopAdvertisingLocked();
  rc = ble_gap_adv_start(own_addr_type, nullptr, BLE_HS_FOREVER, &adv_params, nullptr,
                         nullptr);
  if (rc != 0) {
    ESP_LOGE(kTag, "ble_gap_adv_start failed: %d", rc);
    g_advertising = false;
    return rc;
  }
  g_advertising = true;
  g_pending_start = false;
  ESP_LOGI(kTag, "iBeacon advertising uuid=%s major=%d minor=%d txPower=%d",
           g_config.uuid.c_str(), g_config.major, g_config.minor, g_config.tx_power);
  return 0;
}

void OnReset(int reason) {
  ESP_LOGW(kTag, "NimBLE reset, reason=%d", reason);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_stack_ready = false;
  g_advertising = false;
}

void OnSync() {
  ESP_LOGI(kTag, "NimBLE host synced");
  std::lock_guard<std::mutex> lock(g_mutex);
  g_stack_ready = true;
  if (g_pending_start || g_config.enabled) {
    StartAdvertisingLocked();
  }
}

void HostTask(void* /*param*/) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

esp_err_t EnsureStackLocked() {
  if (g_stack_failed) {
    return ESP_ERR_INVALID_STATE;
  }
  if (g_init_started) {
    return ESP_OK;
  }
  g_init_started = true;

  const esp_err_t err = nimble_port_init();
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "nimble_port_init failed: %s (BLE beacon disabled)",
             esp_err_to_name(err));
    g_stack_failed = true;
    return err;
  }
  ble_hs_cfg.reset_cb = OnReset;
  ble_hs_cfg.sync_cb = OnSync;
  nimble_port_freertos_init(HostTask);
  ESP_LOGI(kTag, "NimBLE stack starting");
  return ESP_OK;
}

#endif  // VA_BLE_NIMBLE

}  // namespace

esp_err_t ApplyConfig(const viaaccess::BleBeaconConfig& cfg) {
  std::lock_guard<std::mutex> lock(g_mutex);

  const viaaccess::BleBeaconConfig next = [&] {
    viaaccess::BleBeaconConfig copy = cfg;
    copy.uuid = viaaccess::NormalizeBleBeaconUuid(copy.uuid);
    if (copy.enabled && copy.uuid.empty()) {
      copy.enabled = false;
    }
    return copy;
  }();

  const bool same = g_config.enabled == next.enabled && g_config.uuid == next.uuid &&
                    g_config.major == next.major && g_config.minor == next.minor &&
                    g_config.tx_power == next.tx_power &&
                    (!next.enabled || g_advertising);
  g_config = next;

  if (!next.enabled) {
#if VA_BLE_NIMBLE
    StopAdvertisingLocked();
#endif
    g_pending_start = false;
    if (!same) {
      ESP_LOGI(kTag, "iBeacon advertising stopped");
    }
    return ESP_OK;
  }

#if !VA_BLE_NIMBLE
  ESP_LOGW(kTag, "BLE/NimBLE not enabled in this build; cannot advertise iBeacon");
  g_advertising = false;
  return ESP_ERR_NOT_SUPPORTED;
#else
  const esp_err_t stack = EnsureStackLocked();
  if (stack != ESP_OK) {
    g_advertising = false;
    return stack;
  }
  if (same) {
    return ESP_OK;
  }
  const int rc = StartAdvertisingLocked();
  return rc == 0 ? ESP_OK : ESP_FAIL;
#endif
}

bool advertising() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_advertising;
}

}  // namespace ble_beacon
