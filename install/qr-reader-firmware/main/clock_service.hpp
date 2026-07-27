// Owns the wall clock: reads the DS3231 at boot, drives SNTP once the station is
// up, and pushes the resulting trust level into app::State so /health and the
// operation mode agree on whether the offline path may run.
#pragma once

#include "viaaccess/clock.hpp"
#include "viaaccess/config.hpp"

namespace clock_service {

// Init probes the RTC and, when it holds a trustworthy time, sets the system
// clock from it. Call before the network comes up so the first TLS handshake
// already has a plausible date.
void Init(const viaaccess::RtcConfig& cfg);

// OnNetworkUp starts SNTP the first time the station gets an address.
void OnNetworkUp();

// RefreshRtcTemperature re-reads the chip. Called from the sync task rather than
// from the /health handler so an I2C stall cannot block an HTTP response.
void RefreshRtcTemperature();

}  // namespace clock_service
