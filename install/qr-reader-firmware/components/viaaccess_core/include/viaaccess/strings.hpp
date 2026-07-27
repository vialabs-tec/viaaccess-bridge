// Small string helpers shared by the core modules, kept free of ESP-IDF so the
// same code compiles for the host test target.
#pragma once

#include <string>

namespace viaaccess {

std::string Trim(const std::string& value);

// TrimTrailingSlashes drops trailing '/' so base URLs concatenate cleanly.
std::string TrimTrailingSlashes(const std::string& value);

std::string ToLower(const std::string& value);

bool StartsWith(const std::string& value, const std::string& prefix);

bool EndsWith(const std::string& value, const std::string& suffix);

// Truncate cuts value to max_bytes and appends an ellipsis, for log lines.
std::string TruncateForLog(const std::string& value, std::size_t max_bytes);

}  // namespace viaaccess
