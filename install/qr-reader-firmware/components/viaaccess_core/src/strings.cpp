#include "viaaccess/strings.hpp"

#include <algorithm>
#include <cctype>

namespace viaaccess {
namespace {

bool IsSpace(unsigned char c) { return std::isspace(c) != 0; }

}  // namespace

std::string Trim(const std::string& value) {
  std::size_t begin = 0;
  while (begin < value.size() && IsSpace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin && IsSpace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::string TrimTrailingSlashes(const std::string& value) {
  std::size_t end = value.size();
  while (end > 0 && value[end - 1] == '/') {
    --end;
  }
  return value.substr(0, end);
}

std::string ToLower(const std::string& value) {
  std::string out = value;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

bool StartsWith(const std::string& value, const std::string& prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string TruncateForLog(const std::string& value, std::size_t max_bytes) {
  if (value.size() <= max_bytes) {
    return value;
  }
  return value.substr(0, max_bytes) + "...";
}

}  // namespace viaaccess
