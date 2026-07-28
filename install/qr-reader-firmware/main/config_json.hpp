// JSON binding for RuntimeConfig and for the Identity payloads the appliance
// consumes. Field names match the Go agent exactly so a config.json copied from
// a Raspberry Pi still loads here (minus the pin map, which is board specific).
#pragma once

#include <string>

#include "viaaccess/config.hpp"
#include "viaaccess/mode.hpp"

namespace config_json {

// Parse overlays the JSON document on top of the factory defaults, so keys that
// Identity or an older firmware never wrote keep their default value.
viaaccess::RuntimeConfig Parse(const std::string& json);

// Serialize omits the device key and the Wi-Fi password: those live in NVS, not
// on the filesystem. include_secrets is used only for debugging over the console.
std::string Serialize(const viaaccess::RuntimeConfig& cfg, bool include_secrets = false);

// ParseRemoteDeviceConfig reads GET /api/bridge/device-config.
bool ParseRemoteDeviceConfig(const std::string& json, viaaccess::RemoteDeviceConfig* out);

// ParsePolicySnapshot reads GET /api/bridge/policy-snapshot for operation mode,
// /health and offline contingency (member grants + HMAC ticket key).
bool ParsePolicySnapshot(const std::string& json, viaaccess::PolicyState* out);

}  // namespace config_json
