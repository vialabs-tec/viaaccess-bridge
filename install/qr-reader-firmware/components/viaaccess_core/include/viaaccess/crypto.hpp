// Minimal crypto helpers for offline passage tickets (HMAC-SHA256 + base64url).
// Kept transport-free so host tests exercise the same path as the firmware.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace viaaccess {

// Base64UrlDecode accepts standard or raw URL encoding (with or without padding).
bool Base64UrlDecode(const std::string& input, std::vector<uint8_t>* out);

std::string Base64UrlEncode(const uint8_t* data, std::size_t length);

inline std::string Base64UrlEncode(const std::vector<uint8_t>& data) {
  return Base64UrlEncode(data.data(), data.size());
}

std::vector<uint8_t> Sha256(const uint8_t* data, std::size_t length);

std::vector<uint8_t> HmacSha256(const uint8_t* key, std::size_t key_length,
                                const uint8_t* data, std::size_t data_length);

inline std::vector<uint8_t> HmacSha256(const std::vector<uint8_t>& key,
                                       const std::string& data) {
  return HmacSha256(key.data(), key.size(),
                    reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

// ConstantTimeEqual returns true when both buffers have the same length and
// contents; comparison always walks the full length of the longer buffer so a
// short signature cannot be timed out early.
bool ConstantTimeEqual(const uint8_t* a, std::size_t a_len, const uint8_t* b,
                       std::size_t b_len);

}  // namespace viaaccess
