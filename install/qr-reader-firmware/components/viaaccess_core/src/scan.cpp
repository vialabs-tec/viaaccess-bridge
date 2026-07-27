#include "viaaccess/scan.hpp"

#include "viaaccess/strings.hpp"

namespace viaaccess {

bool ShouldIgnore(const Debounce& debounce,
                  const std::string& qr_url,
                  int64_t now_ms,
                  int window_ms) {
  return qr_url == debounce.last_scan &&
         (now_ms - debounce.last_scan_at_ms) < static_cast<int64_t>(window_ms);
}

void Mark(Debounce& debounce, const std::string& qr_url, int64_t now_ms) {
  debounce.last_scan = qr_url;
  debounce.last_scan_at_ms = now_ms;
}

std::string SelectQrUrl(const std::string& qr_url,
                        const std::string& qr,
                        const std::string& payload,
                        const std::string& raw_body) {
  for (const std::string* candidate : {&qr_url, &qr, &payload}) {
    const std::string trimmed = Trim(*candidate);
    if (!trimmed.empty()) {
      return trimmed;
    }
  }
  return Trim(raw_body);
}

bool ShouldPulseRelay(const RuntimeConfig& cfg, const RedeemResult& result) {
  if (!cfg.relay.enabled) {
    return false;
  }
  if (!cfg.unlock_on_authorized_only) {
    return result.ok;
  }
  return IsAuthorized(result);
}

bool ShouldPostUnlock(const RuntimeConfig& cfg, const RedeemResult& result) {
  if (cfg.unlock_webhook_url.empty() || !result.ok) {
    return false;
  }
  if (!cfg.unlock_on_authorized_only) {
    return true;
  }
  return IsAuthorized(result);
}

}  // namespace viaaccess
