// KY-016 RGB status LED (common cathode). R/G/B pins follow the factory map
// (4/5/6 on the S3). Pattern selection is in viaaccess_core; this file owns
// the GPIO outputs and the blink poll loop.
#pragma once

#include "esp_err.h"
#include "viaaccess/config.hpp"

namespace status_led {

esp_err_t Start(const viaaccess::StatusLedConfig& cfg);

// ApplyConfig re-arms after /setup changes pins, polarity or the enabled flag.
esp_err_t ApplyConfig(const viaaccess::StatusLedConfig& cfg);

bool enabled();
bool ready();

}  // namespace status_led
