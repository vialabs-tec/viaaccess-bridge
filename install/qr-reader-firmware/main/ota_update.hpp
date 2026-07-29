// HTTPS OTA apply for the inactive app slot, Go-agent parity for UPDATE.
#pragma once

#include <string>

namespace ota {

struct ApplyResult {
  bool ok = false;
  // True when the image is already current: ack success but do not reboot.
  bool already_current = false;
  std::string error;
};

// Apply downloads the app image, verifies SHA-256, and points the bootloader at
// the new slot. On success the caller must ack Identity then reboot. On failure
// the running partition is left unchanged.
ApplyResult Apply(const std::string& version, const std::string& url,
                  const std::string& sha256_hex);

}  // namespace ota
