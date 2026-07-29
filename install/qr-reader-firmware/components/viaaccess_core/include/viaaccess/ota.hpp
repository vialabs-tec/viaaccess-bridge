// OTA payload validation shared with host tests, ported from the Go agent
// internal/ota checks (version/url/sha256 required, HTTPS-only URL).
#pragma once

#include <string>

namespace viaaccess {

struct OtaPayload {
  std::string version;
  std::string url;
  std::string sha256;
};

struct OtaPayloadCheck {
  bool ok = false;
  std::string error;
  // Normalized lowercase hex when ok.
  std::string sha256_hex;
};

// ValidateOtaPayload mirrors Go ota.Apply's preflight checks. It does not
// download; the firmware transport layer does that after this returns ok.
OtaPayloadCheck ValidateOtaPayload(const OtaPayload& payload);

}  // namespace viaaccess
