// Local HTTP surface, port 3710 by default.
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

}  // namespace http_server
