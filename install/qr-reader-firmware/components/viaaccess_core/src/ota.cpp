#include "viaaccess/ota.hpp"

#include <cctype>

#include "viaaccess/strings.hpp"

namespace viaaccess {
namespace {

bool IsHexChar(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

}  // namespace

OtaPayloadCheck ValidateOtaPayload(const OtaPayload& payload) {
  OtaPayloadCheck check;
  const std::string version = Trim(payload.version);
  const std::string url = Trim(payload.url);
  std::string sha = ToLower(Trim(payload.sha256));

  if (version.empty() || url.empty() || sha.empty()) {
    check.error = "incomplete OTA payload";
    return check;
  }
  if (sha.size() != 64) {
    check.error = "invalid sha256 length";
    return check;
  }
  for (char c : sha) {
    if (!IsHexChar(c)) {
      check.error = "invalid sha256";
      return check;
    }
  }
  if (!StartsWith(url, "https://") && !StartsWith(url, "http://127.0.0.1") &&
      !StartsWith(url, "http://localhost")) {
    check.error = "refusing non-HTTPS OTA URL";
    return check;
  }

  check.ok = true;
  check.sha256_hex = std::move(sha);
  return check;
}

}  // namespace viaaccess
