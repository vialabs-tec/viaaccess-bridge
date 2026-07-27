// Persistence for the appliance: LittleFS for documents, NVS for secrets.
//
// The Pi keeps everything in /etc/viaaccess-qr-reader/config.json on a removable
// SD card. Here the device key and the Wi-Fi password go to NVS instead, so the
// filesystem image can be dumped without leaking credentials, and NVS
// encryption (step 7) protects them without touching this API.
#pragma once

#include <string>

#include "esp_err.h"
#include "viaaccess/config.hpp"

namespace storage {

// Mount registers LittleFS and formats the partition on first boot.
esp_err_t Mount();

// LoadConfig returns factory defaults when nothing is persisted yet.
viaaccess::RuntimeConfig LoadConfig();

// SaveConfig writes config.json atomically (temp file plus rename) and mirrors
// the secrets into NVS.
esp_err_t SaveConfig(const viaaccess::RuntimeConfig& cfg);

// SavePolicySnapshot keeps the raw Identity document: the fields contingency
// needs (member grants, HMAC ticket key, edge policy) are preserved even though
// the firmware currently parses only the subset that drives /health.
esp_err_t SavePolicySnapshot(const std::string& json);
std::string LoadPolicySnapshot();

}  // namespace storage
