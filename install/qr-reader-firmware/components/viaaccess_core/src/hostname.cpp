#include "viaaccess/hostname.hpp"

#include <cctype>

#include "viaaccess/config.hpp"
#include "viaaccess/strings.hpp"

namespace viaaccess {
namespace {

constexpr std::size_t kMaxLabelBytes = 63;

bool IsLabelSafe(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
}

std::string TrimDashes(const std::string& value) {
  std::size_t begin = 0;
  while (begin < value.size() && value[begin] == '-') {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin && value[end - 1] == '-') {
    --end;
  }
  return value.substr(begin, end - begin);
}

}  // namespace

std::string SanitizeHostname(const std::string& raw) {
  std::string s = ToLower(Trim(raw));
  if (EndsWith(s, ".local")) {
    s = s.substr(0, s.size() - 6);
  }
  if (EndsWith(s, ".")) {
    s = s.substr(0, s.size() - 1);
  }
  for (char& c : s) {
    if (!IsLabelSafe(c)) {
      c = '-';
    }
  }
  s = TrimDashes(s);
  if (s.empty()) {
    return kDefaultMdnsHostname;
  }
  if (s.size() > kMaxLabelBytes) {
    s = TrimDashes(s.substr(0, kMaxLabelBytes));
  }
  if (s.empty() || std::isalnum(static_cast<unsigned char>(s[0])) == 0) {
    return kDefaultMdnsHostname;
  }
  return s;
}

std::string HostnameFromAccessPointSlug(const std::string& slug) {
  const std::string s = SanitizeHostname(slug);
  // Factory default (current and pre-rename) must not become viaaccess-viaaccess-qr.
  if (s == kDefaultMdnsHostname || s == "viaaccess-qr") {
    return kDefaultMdnsHostname;
  }
  const std::string prefix = std::string(kDefaultMdnsHostname) + "-";
  if (StartsWith(s, prefix)) {
    return s;
  }
  return SanitizeHostname(prefix + s);
}

std::string MigrateLegacyMdnsHostname(const std::string& hostname) {
  const std::string s = SanitizeHostname(hostname);
  if (s == "viaaccess-qr") {
    return kDefaultMdnsHostname;
  }
  constexpr const char* kLegacyPrefix = "viaaccess-qr-";
  if (StartsWith(s, kLegacyPrefix)) {
    return SanitizeHostname(std::string(kDefaultMdnsHostname) + "-" + s.substr(13));
  }
  return s;
}

}  // namespace viaaccess
