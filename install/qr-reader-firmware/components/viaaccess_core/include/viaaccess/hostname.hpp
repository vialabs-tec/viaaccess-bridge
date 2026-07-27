// LAN hostname rules for mDNS, ported from internal/mdns of the Go agent so a
// Pi and an ESP32-S3 claimed against the same access point resolve to the same
// <name>.local label.
#pragma once

#include <string>

namespace viaaccess {

// SanitizeHostname returns a DNS-label-safe host without the .local suffix,
// falling back to viaaccess-qr when nothing usable remains.
std::string SanitizeHostname(const std::string& raw);

// HostnameFromAccessPointSlug builds viaaccess-qr-{slug} so multiple readers on
// the same network get distinct .local names after claim.
std::string HostnameFromAccessPointSlug(const std::string& slug);

}  // namespace viaaccess
