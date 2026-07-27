// Lock relay driver, replacing the libgpiod path of internal/relay in the Go
// agent with plain GPIO plus a one-shot esp_timer.
#pragma once

#include "esp_err.h"
#include "viaaccess/config.hpp"

namespace relay {

esp_err_t Init(const viaaccess::RelayConfig& cfg);

// ApplyConfig re-arms the driver after /setup or Identity device-config changes
// the pin, the polarity or the pulse width.
esp_err_t ApplyConfig(const viaaccess::RelayConfig& cfg);

// Pulse drives the line for pulse_ms and returns immediately: the release is
// timer driven so a 3 s door pulse never stalls the HTTP server or the sync
// task. A pulse arriving while one is in flight extends it instead of stacking.
esp_err_t Pulse();

// available is false when the relay is disabled in config or the GPIO could not
// be claimed, which /health reports as relaySimulated.
bool available();

}  // namespace relay
