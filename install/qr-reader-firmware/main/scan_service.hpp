// Single passage pipeline, ported from internal/scan/handler.go.
//
// Both entry points land here: POST /scan from an integrator or the homologation
// script, and a line decoded by the TTL barcode module. Debounce, redeem, relay
// and unlock webhook behave identically either way, which is what makes the
// curl based homologation meaningful before any reader is wired.
#pragma once

#include <string>

namespace scan_service {

struct HttpResult {
  int status = 200;
  std::string body;
};

// HandleHttpRequest parses the request body (JSON with qrUrl / qr / payload, or
// a bare URL) and returns the response POST /scan should send.
HttpResult HandleHttpRequest(const std::string& raw_body,
                             const std::string& webhook_secret_header);

// HandleReaderLine runs the same pipeline for a scan that arrived over UART.
// SoftAP setup ignores UART scans: the phone screen in front of the module is
// not a passage, and a floating RX must not redeem or beep during commissioning.
void HandleReaderLine(const std::string& line);

}  // namespace scan_service
