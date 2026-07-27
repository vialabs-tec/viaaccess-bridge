// Firmware version reported to Identity in X-ViaAccess-Agent-Version and in
// /health. Identity compares it against the UPDATE command payload, so it must
// match the version stamped on the OTA artifact.
#pragma once

namespace viaaccess {

#ifndef VIAACCESS_FIRMWARE_VERSION
#define VIAACCESS_FIRMWARE_VERSION "dev"
#endif

inline constexpr const char* kFirmwareVersion = VIAACCESS_FIRMWARE_VERSION;

}  // namespace viaaccess
