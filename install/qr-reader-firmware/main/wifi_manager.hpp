// Wi-Fi bring-up and the SoftAP provisioning portal.
//
// This step has no equivalent in the Go agent: the Raspberry Pi arrives on the
// LAN over Ethernet and /setup is reachable immediately. The ESP32-S3 has no
// Ethernet MAC, so the appliance first raises its own access point, collects the
// network credentials, and only then can reach Identity.
#pragma once

#include <string>
#include <vector>

#include "esp_err.h"
#include "viaaccess/config.hpp"

namespace wifi {

// SoftAP SSID raised while the appliance has no usable station connection.
inline constexpr const char* kSetupApSsid = "viaaccess-setup";

struct ScanEntry {
  std::string ssid;
  int rssi = 0;
  bool secure = false;
};

esp_err_t Start(const viaaccess::WifiConfig& cfg);

// ApplyCredentials stores nothing: the caller persists them through app::State.
// It only re-points the station and restarts the connection attempt.
esp_err_t ApplyCredentials(const std::string& ssid, const std::string& password);

// Scan lists nearby networks for the portal, strongest first.
std::vector<ScanEntry> Scan();

// ForcePortal raises SoftAP immediately so a technician can join
// viaaccess-setup without waiting for station failures.
esp_err_t ForcePortal();

// ClosePortalAfterSetup drops SoftAP ~1.5 s after a successful /setup save so
// the HTTP 200 still reaches the phone. No-op if the station is still offline
// (recovery SoftAP must stay up). 10 min TTL remains only if setup is abandoned.
void ClosePortalAfterSetup();

// DismissPortal closes SoftAP immediately when the station is online (BOOT
// abort, or the delayed close timer). Returns true if the portal actually went
// down. Keeps SoftAP if STA is offline so recovery is not bricked.
bool DismissPortal();

// ReleasePortalHold lets the next STA GOT_IP close SoftAP (Wi-Fi credentials
// just saved; do not tear the portal down before the station associates).
void ReleasePortalHold();

bool connected();

// ip is the station address, empty while not connected.
std::string ip();

// True while the SoftAP interface is up (first boot, STA failures, or ForcePortal).
bool portal_active();

// True when the HTTP Host is the SoftAP address (192.168.4.1[:port]).
// Browser tabs on *.local stay read-only even while SoftAP is up (APSTA).
bool host_is_softap(const std::string& host_header);

// True when the TCP peer is on the SoftAP subnet (192.168.4.0/24). Captive
// probes arrive with Host: captive.apple.com etc.; the client IP still counts.
bool peer_is_softap_client(int sockfd);

// Local setup POSTs: always allowed before provision; after provision only when
// SoftAP is up AND (Host is 192.168.4.1 or the peer is on the SoftAP subnet).
bool local_setup_writes_allowed(bool device_configured, const std::string& host_header,
                                int sockfd = -1);

}  // namespace wifi
