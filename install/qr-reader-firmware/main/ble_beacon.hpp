// BLE proximity advertiser (iBeacon-compatible layout, ViaAccess company ID)
// driven by Identity device-config (bleBeacon).
//
// Uses ESP-IDF NimBLE when CONFIG_BT_NIMBLE_ENABLED is set. When BLE is
// unavailable at compile time or fails at runtime, ApplyConfig logs a warning
// and no-ops so passage keeps working without a beacon.
#pragma once

#include "esp_err.h"
#include "viaaccess/config.hpp"

namespace ble_beacon {

// ApplyConfig starts, restarts or stops advertising to match cfg. Safe to call
// repeatedly; idempotent when the payload is unchanged.
esp_err_t ApplyConfig(const viaaccess::BleBeaconConfig& cfg);

// advertising is true only while a non-connectable iBeacon ADV is running.
bool advertising();

}  // namespace ble_beacon
