// Lock relay driver, replacing the libgpiod path of internal/relay in the Go
// agent with plain GPIO plus a one-shot esp_timer.
//
// unlock_mode:
// - pulse / hold: release after pulse_ms
// - until_closed: release on door close (after open), or pulse_ms as max timeout
#pragma once

#include "esp_err.h"
#include "viaaccess/config.hpp"

namespace relay {

esp_err_t Init(const viaaccess::RelayConfig& cfg);

// ApplyConfig re-arms the driver after /setup or Identity device-config changes
// the pin, the polarity, the pulse width or the unlock mode.
esp_err_t ApplyConfig(const viaaccess::RelayConfig& cfg);

// Pulse drives the line and returns immediately. Timed modes release via timer;
// until_closed also waits for the reed cycle (open → close) with pulse_ms max.
// A pulse arriving while one is in flight restarts the cycle instead of stacking.
esp_err_t Pulse();

// Door-contact hooks for until_closed. No-ops in timed modes.
void OnDoorOpen();
void OnDoorClosed();

// available is false when the relay is disabled in config or the GPIO could not
// be claimed, which /health reports as relaySimulated.
bool available();

}  // namespace relay
