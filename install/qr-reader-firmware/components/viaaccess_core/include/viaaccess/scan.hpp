// Scan admission rules, ported from internal/scan of the Go agent: same
// per-QR debounce window and the same relay/unlock decisions, so a scan
// behaves identically whether it arrives over HTTP or over the UART reader.
#pragma once

#include <cstdint>
#include <string>

#include "viaaccess/config.hpp"
#include "viaaccess/redeem.hpp"

namespace viaaccess {

struct Debounce {
  std::string last_scan;
  int64_t last_scan_at_ms = 0;
};

bool ShouldIgnore(const Debounce& debounce,
                  const std::string& qr_url,
                  int64_t now_ms,
                  int window_ms);

void Mark(Debounce& debounce, const std::string& qr_url, int64_t now_ms);

// SelectQrUrl picks the first non-empty candidate in the order the Go agent
// reads them (qrUrl, qr, payload), falling back to a raw non-JSON body.
std::string SelectQrUrl(const std::string& qr_url,
                        const std::string& qr,
                        const std::string& payload,
                        const std::string& raw_body);

bool ShouldPulseRelay(const RuntimeConfig& cfg, const RedeemResult& result);

bool ShouldPostUnlock(const RuntimeConfig& cfg, const RedeemResult& result);

}  // namespace viaaccess
