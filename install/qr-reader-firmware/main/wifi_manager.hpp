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
inline constexpr const char* kSetupApSsid = "viaaccess-qr-setup";

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
// viaaccess-qr-setup without waiting for station failures.
esp_err_t ForcePortal();

bool connected();

// ip is the station address, empty while not connected.
std::string ip();

bool portal_active();

}  // namespace wifi
