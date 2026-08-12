// Door-contact (reed) driver: GPIO input with pull-up, or an in-memory sim for
// homologation without the MC38. Debounce and held_open live in viaaccess_core;
// this file owns the pin, the FreeRTOS watcher and the Identity POST.
#pragma once

#include <string>

#include "esp_err.h"
#include "viaaccess/config.hpp"

namespace door_contact {

esp_err_t Start(const viaaccess::DoorContactConfig& cfg);

// ApplyConfig re-arms after /setup changes the pin, the polarity, the sim flag
// or the timers. A disabled config stops emitting without killing the task.
esp_err_t ApplyConfig(const viaaccess::DoorContactConfig& cfg);

// SetSimOpen forces open/closed when simulated is on. Matches the Go agent: the
// HTTP sim endpoint only flips the virtual reed; the watcher posts to Identity
// after debounce, so held_open still fires.
esp_err_t SetSimOpen(bool open);

bool enabled();
bool simulated();
bool ready();
int gpio_pin();
std::string state();
/** True when the debounced reed is open (false if disabled / unknown). */
bool is_open();

}  // namespace door_contact
