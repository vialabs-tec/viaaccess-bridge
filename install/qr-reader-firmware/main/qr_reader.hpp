// EP8280L barcode module reader over TTL UART.
//
// The Go agent reads the same module as a USB HID keyboard (internal/hidwedge)
// because the Pi has USB host. The S3 does not, so the module is switched out of
// USB-KBW into TTL mode and wired RX/TX; the framing logic is the same
// LineBuffer, and both paths end in scan_service.
#pragma once

#include "esp_err.h"
#include "viaaccess/config.hpp"

namespace qr_reader {

esp_err_t Start(const viaaccess::QrReaderConfig& cfg);

// ApplyConfig re-opens the port when /setup changes enable, pins or baud.
// The UART driver is torn down on the reader task, not the HTTP handler.
esp_err_t ApplyConfig(const viaaccess::QrReaderConfig& cfg);

}  // namespace qr_reader
