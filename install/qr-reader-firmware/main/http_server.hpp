// Local HTTP surface: :80 (captive portal + phones), :3710 by default
// (homologate / Identity scripts), optional HTTPS on :443 with a factory
// self-signed cert for LAN (`curl -k`) only while SoftAP is down. SoftAP
// phones must use HTTP — a dead TLS handshake on :443 makes iOS skip the
// captive sheet.
//
// Ports internal/server/http.go: same routes, same status codes and the same
// Portuguese error bodies, so scripts/homologate.sh and the dashboard cannot
// tell the two appliances apart. Two routes are new and specific to the S3:
// GET /api/setup/wifi/scan and POST /api/setup/wifi, which back the SoftAP
// portal the Pi does not need.
#pragma once

#include "esp_err.h"

namespace http_server {

esp_err_t Start();

// ApplyPort restarts the listener when Identity or /setup changes httpPort.
esp_err_t ApplyPort(int port);

// RefreshLanHttps stops :443 while SoftAP is up and starts it again on STA-only
// LAN. Deferred so Wi-Fi event / HTTP workers are not blocked on mbedtls.
esp_err_t RefreshLanHttps();

}  // namespace http_server
