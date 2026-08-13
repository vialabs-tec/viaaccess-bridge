// DevKit BOOT button (GPIO 0): multi-gesture service control.
//
// 1 click  — announce operation mode (buzzer); while SoftAP is up and STA is
//            online, any click burst dismisses the portal instead
// 2 clicks — synthetic REX (only when Fiação has REX + Simular enabled)
// 3 clicks — force SoftAP setup portal (toggles off if already up and STA online)
// Long press (5 s, warn at 2 s) — clear credentials + Wi-Fi and reboot
#pragma once

#include "esp_err.h"

namespace service_button {

esp_err_t Start();

}  // namespace service_button
