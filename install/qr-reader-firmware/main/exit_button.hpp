// Request-to-Exit (REX) driver: GPIO input with pull-up, or an in-memory sim.
// Debounce, arming and cooldown live in viaaccess_core; this file owns the pin,
// the watcher, the Identity notify and the local relay pulse.
#pragma once

#include <string>

#include "esp_err.h"
#include "viaaccess/config.hpp"

namespace exit_button {

esp_err_t Start(const viaaccess::ExitButtonConfig& cfg);
esp_err_t ApplyConfig(const viaaccess::ExitButtonConfig& cfg);

// SetSimPressed forces pressed/idle when simulated is on. The HTTP sim endpoint
// only flips the virtual button; the watcher posts and pulses after debounce.
esp_err_t SetSimPressed(bool pressed);

bool enabled();
bool simulated();
bool ready();
int gpio_pin();
std::string state();

}  // namespace exit_button
