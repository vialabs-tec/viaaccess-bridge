#include "wifi_manager.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>

#include "app_state.hpp"
#include "clock_service.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "lwip/ip4_addr.h"
#include "viaaccess/strings.hpp"

namespace wifi {
namespace {

constexpr const char* kTag = "wifi";

// After this many consecutive failures the SoftAP comes back so a technician can
// fix the credentials without a serial cable (wrong password, SSID renamed).
constexpr int kFailuresBeforePortal = 5;
constexpr int kMaxScanRecords = 24;
// SoftAP forced while STA is still online auto-closes so the open portal does
// not linger on APSTA forever.
constexpr int kForcedPortalTtlMs = 10 * 60 * 1000;

std::mutex g_mutex;
esp_netif_t* g_sta_netif = nullptr;
esp_netif_t* g_ap_netif = nullptr;
std::string g_ssid;
std::string g_ip;
bool g_connected = false;
bool g_ap_active = false;
// ForcePortal hold: STA GOT_IP must not tear SoftAP down while the technician
// is still on viaaccess-qr-setup (TTL clears the hold).
bool g_portal_hold = false;
int g_failures = 0;
esp_timer_handle_t g_reconnect_timer = nullptr;
esp_timer_handle_t g_portal_ttl_timer = nullptr;

void PortalTtlFired(void* /*argument*/);

void ReconnectTimerFired(void* /*argument*/) { esp_wifi_connect(); }

// ScheduleReconnect defers esp_wifi_connect instead of sleeping inside the event
// handler: that task also delivers IP and HTTP-relevant events, so blocking it
// would stall the whole stack for the duration of the backoff.
void ScheduleReconnect(uint32_t delay_ms) {
  if (g_reconnect_timer == nullptr) {
    const esp_timer_create_args_t args = {
        .callback = ReconnectTimerFired,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_reconnect",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&args, &g_reconnect_timer) != ESP_OK) {
      esp_wifi_connect();
      return;
    }
  }
  esp_timer_stop(g_reconnect_timer);
  esp_timer_start_once(g_reconnect_timer, static_cast<uint64_t>(delay_ms) * 1000);
}

void PublishPhase(app::WifiPhase phase) {
  app::State::Instance().set_wifi(phase, g_ssid, g_ip);
}

void CopySsidPassword(wifi_sta_config_t* sta, const std::string& ssid,
                      const std::string& password) {
  std::memset(sta->ssid, 0, sizeof(sta->ssid));
  std::memset(sta->password, 0, sizeof(sta->password));
  std::memcpy(sta->ssid, ssid.data(), std::min(ssid.size(), sizeof(sta->ssid) - 1));
  std::memcpy(sta->password, password.data(),
              std::min(password.size(), sizeof(sta->password) - 1));
}

void StopPortalTtlTimer() {
  if (g_portal_ttl_timer != nullptr) {
    esp_timer_stop(g_portal_ttl_timer);
  }
}

void SchedulePortalTtlIfNeeded() {
  // Only auto-close when SoftAP is sharing the radio with a live STA link.
  if (!g_ap_active || !g_connected) {
    StopPortalTtlTimer();
    return;
  }
  if (g_portal_ttl_timer == nullptr) {
    const esp_timer_create_args_t args = {
        .callback = PortalTtlFired,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "softap_ttl",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&args, &g_portal_ttl_timer) != ESP_OK) {
      return;
    }
  }
  esp_timer_stop(g_portal_ttl_timer);
  esp_timer_start_once(g_portal_ttl_timer,
                       static_cast<uint64_t>(kForcedPortalTtlMs) * 1000);
  ESP_LOGI(kTag, "SoftAP will auto-close in %d min while STA stays up",
           kForcedPortalTtlMs / 60000);
}

void PortalTtlFired(void* /*argument*/) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_portal_hold = false;
  if (!g_ap_active) {
    return;
  }
  if (!g_connected) {
    // Still offline — keep SoftAP for recovery.
    return;
  }
  ESP_LOGW(kTag, "SoftAP TTL elapsed; closing portal (STA still connected)");
  const esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err == ESP_OK) {
    g_ap_active = false;
    PublishPhase(app::WifiPhase::kConnected);
  }
}

esp_err_t SetApMode(bool enable) {
  if (enable == g_ap_active) {
    if (enable) {
      SchedulePortalTtlIfNeeded();
    }
    return ESP_OK;
  }
  const esp_err_t err = esp_wifi_set_mode(enable ? WIFI_MODE_APSTA : WIFI_MODE_STA);
  if (err != ESP_OK) {
    return err;
  }
  g_ap_active = enable;
  ESP_LOGI(kTag, "SoftAP %s", enable ? "up" : "down");
  if (enable) {
    SchedulePortalTtlIfNeeded();
  } else {
    StopPortalTtlTimer();
  }
  return ESP_OK;
}

void OnWifiEvent(void* /*arg*/, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
    return;
  }
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const auto* event = static_cast<wifi_event_sta_disconnected_t*>(data);
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_connected = false;
      g_ip.clear();
      g_failures++;
      ESP_LOGW(kTag, "disconnected from %s (reason %d, attempt %d)", g_ssid.c_str(),
               event != nullptr ? event->reason : -1, g_failures);
      if (g_failures >= kFailuresBeforePortal) {
        SetApMode(true);
        PublishPhase(app::WifiPhase::kProvisioning);
      } else {
        PublishPhase(app::WifiPhase::kConnecting);
      }
    }
    // A fixed pause keeps a wrong password from hammering the radio while still
    // recovering quickly from a router reboot.
    ScheduleReconnect(2000);
    return;
  }
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto* event = static_cast<ip_event_got_ip_t*>(data);
    char buffer[16] = {};
    esp_ip4addr_ntoa(&event->ip_info.ip, buffer, sizeof(buffer));
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_connected = true;
      g_failures = 0;
      g_ip = buffer;
      ESP_LOGI(kTag, "connected to %s with ip %s", g_ssid.c_str(), g_ip.c_str());
      // ForcePortal hold keeps SoftAP up for the technician even while STA is
      // online. Otherwise close SoftAP once the station is usable so a bad
      // password never locks recovery out permanently.
      if (g_portal_hold) {
        SchedulePortalTtlIfNeeded();
        PublishPhase(app::WifiPhase::kProvisioning);
      } else {
        SetApMode(false);
        PublishPhase(app::WifiPhase::kConnected);
      }
    }
    // Outside the lock: the clock service touches app::State and the I2C bus.
    clock_service::OnNetworkUp();
  }
}

esp_err_t ConfigureAp() {
  wifi_config_t ap = {};
  const std::string ssid = kSetupApSsid;
  std::memcpy(ap.ap.ssid, ssid.data(), ssid.size());
  ap.ap.ssid_len = static_cast<uint8_t>(ssid.size());
  ap.ap.channel = 1;
  ap.ap.max_connection = 2;
  // Open network on purpose: the portal is only reachable in physical range and
  // it exists precisely to receive the credentials the technician does not have
  // another way to type in. /api/setup still requires the provisioning token.
  ap.ap.authmode = WIFI_AUTH_OPEN;
  return esp_wifi_set_config(WIFI_IF_AP, &ap);
}

}  // namespace

esp_err_t Start(const viaaccess::WifiConfig& cfg) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  g_sta_netif = esp_netif_create_default_wifi_sta();
  g_ap_netif = esp_netif_create_default_wifi_ap();

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                     &OnWifiEvent, nullptr, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                     &OnWifiEvent, nullptr, nullptr));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  g_ap_active = true;
  ESP_ERROR_CHECK(ConfigureAp());

  const std::string ssid = viaaccess::Trim(cfg.ssid);
  if (!ssid.empty()) {
    wifi_config_t sta = {};
    CopySsidPassword(&sta.sta, ssid, cfg.password);
    sta.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta.sta.threshold.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
  }

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ssid = ssid;
    PublishPhase(ssid.empty() ? app::WifiPhase::kProvisioning : app::WifiPhase::kConnecting);
  }

  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(kTag, "portal ready at %s (http://192.168.4.1:%d/setup)", kSetupApSsid, 3710);
  if (ssid.empty()) {
    ESP_LOGW(kTag, "no station credentials yet, waiting for the setup portal");
  }
  return ESP_OK;
}

esp_err_t ApplyCredentials(const std::string& ssid, const std::string& password) {
  const std::string clean = viaaccess::Trim(ssid);
  if (clean.empty()) {
    return ESP_ERR_INVALID_ARG;
  }

  wifi_config_t sta = {};
  CopySsidPassword(&sta.sta, clean, password);
  sta.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  sta.sta.threshold.authmode = WIFI_AUTH_OPEN;
  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta);
  if (err != ESP_OK) {
    return err;
  }

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ssid = clean;
    g_failures = 0;
    g_connected = false;
    g_ip.clear();
    PublishPhase(app::WifiPhase::kConnecting);
  }

  esp_wifi_disconnect();
  err = esp_wifi_connect();
  ESP_LOGI(kTag, "reconnecting to %s", clean.c_str());
  return err;
}

std::vector<ScanEntry> Scan() {
  std::vector<ScanEntry> out;
  wifi_scan_config_t scan = {};
  scan.show_hidden = false;
  // Blocking scan: the portal request waits for it, which is acceptable because
  // the technician explicitly asked for the network list.
  if (esp_wifi_scan_start(&scan, true) != ESP_OK) {
    return out;
  }

  uint16_t found = kMaxScanRecords;
  wifi_ap_record_t records[kMaxScanRecords] = {};
  if (esp_wifi_scan_get_ap_records(&found, records) != ESP_OK) {
    esp_wifi_clear_ap_list();
    return out;
  }

  std::vector<ScanEntry> all;
  all.reserve(found);
  for (uint16_t i = 0; i < found; i++) {
    ScanEntry entry;
    entry.ssid = reinterpret_cast<const char*>(records[i].ssid);
    if (entry.ssid.empty()) {
      continue;
    }
    entry.rssi = records[i].rssi;
    entry.secure = records[i].authmode != WIFI_AUTH_OPEN;
    all.push_back(std::move(entry));
  }
  std::sort(all.begin(), all.end(),
            [](const ScanEntry& a, const ScanEntry& b) { return a.rssi > b.rssi; });

  // Mesh networks answer from several radios with the same SSID; keep only the
  // strongest of each so the portal list stays readable.
  std::set<std::string> seen;
  for (auto& entry : all) {
    if (seen.insert(entry.ssid).second) {
      out.push_back(std::move(entry));
    }
  }
  esp_wifi_clear_ap_list();
  return out;
}

bool connected() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_connected;
}

std::string ip() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_ip;
}

bool portal_active() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_ap_active;
}

std::string SoftApHostAddress() {
  esp_netif_ip_info_t ap_ip{};
  if (g_ap_netif != nullptr &&
      esp_netif_get_ip_info(g_ap_netif, &ap_ip) == ESP_OK && ap_ip.ip.addr != 0) {
    char buffer[16] = {};
    esp_ip4addr_ntoa(&ap_ip.ip, buffer, sizeof(buffer));
    return buffer;
  }
  return "192.168.4.1";
}

bool host_is_softap(const std::string& host_header) {
  std::string host = viaaccess::ToLower(viaaccess::Trim(host_header));
  if (host.empty()) {
    return false;
  }
  // Strip :port (IPv4 Host only — SoftAP is never IPv6 here).
  const auto colon = host.rfind(':');
  if (colon != std::string::npos) {
    host = host.substr(0, colon);
  }
  return host == SoftApHostAddress();
}

bool local_setup_writes_allowed(bool device_configured, const std::string& host_header) {
  if (!device_configured) {
    // First Wi‑Fi + claim still happen on the LAN after SoftAP drops.
    return true;
  }
  // BOOT 3-click opens SoftAP; browsers must use http://192.168.4.1:3710/…
  // so *.local / STA IP stay read-only during the portal window. SoftAP phones
  // use HTTP (self-signed HTTPS is blocked by many mobile browsers on SoftAP).
  return portal_active() && host_is_softap(host_header);
}

esp_err_t ForcePortal() {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_portal_hold = true;
  }
  const esp_err_t err = SetApMode(true);
  if (err != ESP_OK) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_portal_hold = false;
    return err;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    // Treat as provisioning so /health and the LED show SETUP / portal up even
    // if the station is still associated on the side.
    g_failures = kFailuresBeforePortal;
    PublishPhase(app::WifiPhase::kProvisioning);
    SchedulePortalTtlIfNeeded();
  }
  ESP_LOGW(kTag,
           "SoftAP forced: join %s and open http://192.168.4.1:3710/setup "
           "(writes only via SoftAP; auto-closes in %d min if STA stays up)",
           kSetupApSsid, kForcedPortalTtlMs / 60000);
  return ESP_OK;
}

}  // namespace wifi
