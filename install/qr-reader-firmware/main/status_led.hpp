// Status RGB LED: DevKitC-1 onboard WS2812 (default) or optional KY-016
// common-cathode module on three GPIOs. Pattern selection is in viaaccess_core;
// this file owns the outputs and the blink poll loop.
#pragma once

#include "esp_err.h"
#include "viaaccess/config.hpp"

namespace status_led {

esp_err_t Start(const viaaccess::StatusLedConfig& cfg);

// ApplyConfig re-arms after /setup changes driver, pins, polarity or enabled.
esp_err_t ApplyConfig(const viaaccess::StatusLedConfig& cfg);

bool enabled();
bool ready();

}  // namespace status_led
