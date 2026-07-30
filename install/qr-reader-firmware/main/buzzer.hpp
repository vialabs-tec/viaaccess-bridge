// Active buzzer on a GPIO (typically via transistor on the 5 V rail).
#pragma once

#include "esp_err.h"
#include "viaaccess/config.hpp"

namespace buzzer {

esp_err_t Start(const viaaccess::BuzzerConfig& cfg);
esp_err_t ApplyConfig(const viaaccess::BuzzerConfig& cfg);

// Non-blocking cues. Held-open repeats until Stop() (door closed).
void BeepSuccess();
void BeepFail();
void BeepHeldOpen();
void Stop();

bool available();

}  // namespace buzzer
