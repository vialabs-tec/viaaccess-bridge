// Wildcard DNS for the SoftAP captive portal.
//
// While viaaccess-setup is up, every A query is answered with 192.168.4.1 so
// phones that probe captive.apple.com / generate_204 land on the local HTTP
// server. Stopped when SoftAP goes down so LAN DNS is never hijacked.
#pragma once

#include "esp_err.h"

namespace captive_dns {

esp_err_t Start();
void Stop();

}  // namespace captive_dns
