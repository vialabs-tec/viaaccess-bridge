#include "viaaccess/crypto.hpp"

#include <array>
#include <cstring>

namespace viaaccess {
namespace {

constexpr uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

uint32_t Rotr(uint32_t value, uint32_t bits) {
  return (value >> bits) | (value << (32 - bits));
}

void Sha256Compress(uint32_t state[8], const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
           (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
           static_cast<uint32_t>(block[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];
  uint32_t e = state[4];
  uint32_t f = state[5];
  uint32_t g = state[6];
  uint32_t h = state[7];

  for (int i = 0; i < 64; ++i) {
    const uint32_t s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
    const uint32_t ch = (e & f) ^ ((~e) & g);
    const uint32_t temp1 = h + s1 + ch + kSha256K[i] + w[i];
    const uint32_t s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

int DecodeBase64Char(char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  if (c == '+' || c == '-') {
    return 62;
  }
  if (c == '/' || c == '_') {
    return 63;
  }
  return -1;
}

}  // namespace

bool Base64UrlDecode(const std::string& input, std::vector<uint8_t>* out) {
  if (out == nullptr) {
    return false;
  }
  out->clear();
  std::string normalized;
  normalized.reserve(input.size() + 3);
  for (char c : input) {
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') {
      continue;
    }
    normalized.push_back(c);
  }
  const std::size_t pad = (4 - (normalized.size() % 4)) % 4;
  for (std::size_t i = 0; i < pad; ++i) {
    normalized.push_back('A');
  }

  out->reserve((normalized.size() / 4) * 3);
  for (std::size_t i = 0; i < normalized.size(); i += 4) {
    const int v0 = DecodeBase64Char(normalized[i]);
    const int v1 = DecodeBase64Char(normalized[i + 1]);
    const int v2 = DecodeBase64Char(normalized[i + 2]);
    const int v3 = DecodeBase64Char(normalized[i + 3]);
    if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) {
      out->clear();
      return false;
    }
    out->push_back(static_cast<uint8_t>((v0 << 2) | (v1 >> 4)));
    out->push_back(static_cast<uint8_t>(((v1 & 0x0f) << 4) | (v2 >> 2)));
    out->push_back(static_cast<uint8_t>(((v2 & 0x03) << 6) | v3));
  }
  if (pad > 0) {
    if (out->size() < pad) {
      out->clear();
      return false;
    }
    out->resize(out->size() - pad);
  }
  return true;
}

std::string Base64UrlEncode(const uint8_t* data, std::size_t length) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  out.reserve(((length + 2) / 3) * 4);
  for (std::size_t i = 0; i < length; i += 3) {
    const uint32_t b0 = data[i];
    const uint32_t b1 = (i + 1 < length) ? data[i + 1] : 0;
    const uint32_t b2 = (i + 2 < length) ? data[i + 2] : 0;
    const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
    out.push_back(kAlphabet[(triple >> 18) & 63]);
    out.push_back(kAlphabet[(triple >> 12) & 63]);
    if (i + 1 < length) {
      out.push_back(kAlphabet[(triple >> 6) & 63]);
    }
    if (i + 2 < length) {
      out.push_back(kAlphabet[triple & 63]);
    }
  }
  return out;
}

std::vector<uint8_t> Sha256(const uint8_t* data, std::size_t length) {
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t block[64];
  std::size_t offset = 0;
  while (offset + 64 <= length) {
    Sha256Compress(state, data + offset);
    offset += 64;
  }

  std::size_t remaining = length - offset;
  std::memcpy(block, data + offset, remaining);
  block[remaining++] = 0x80;
  if (remaining > 56) {
    std::memset(block + remaining, 0, 64 - remaining);
    Sha256Compress(state, block);
    remaining = 0;
  }
  std::memset(block + remaining, 0, 56 - remaining);
  const uint64_t bit_length = static_cast<uint64_t>(length) * 8;
  for (int i = 0; i < 8; ++i) {
    block[63 - i] = static_cast<uint8_t>((bit_length >> (8 * i)) & 0xff);
  }
  Sha256Compress(state, block);

  std::vector<uint8_t> digest(32);
  for (int i = 0; i < 8; ++i) {
    digest[i * 4] = static_cast<uint8_t>((state[i] >> 24) & 0xff);
    digest[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xff);
    digest[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xff);
    digest[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xff);
  }
  return digest;
}

std::vector<uint8_t> HmacSha256(const uint8_t* key, std::size_t key_length,
                                const uint8_t* data, std::size_t data_length) {
  std::array<uint8_t, 64> key_block{};
  if (key_length > 64) {
    const std::vector<uint8_t> hashed = Sha256(key, key_length);
    std::memcpy(key_block.data(), hashed.data(), hashed.size());
  } else {
    std::memcpy(key_block.data(), key, key_length);
  }

  std::array<uint8_t, 64> o_key{};
  std::array<uint8_t, 64> i_key{};
  for (std::size_t i = 0; i < 64; ++i) {
    o_key[i] = static_cast<uint8_t>(key_block[i] ^ 0x5c);
    i_key[i] = static_cast<uint8_t>(key_block[i] ^ 0x36);
  }

  std::vector<uint8_t> inner;
  inner.reserve(64 + data_length);
  inner.insert(inner.end(), i_key.begin(), i_key.end());
  inner.insert(inner.end(), data, data + data_length);
  const std::vector<uint8_t> inner_hash = Sha256(inner.data(), inner.size());

  std::vector<uint8_t> outer;
  outer.reserve(64 + inner_hash.size());
  outer.insert(outer.end(), o_key.begin(), o_key.end());
  outer.insert(outer.end(), inner_hash.begin(), inner_hash.end());
  return Sha256(outer.data(), outer.size());
}

bool ConstantTimeEqual(const uint8_t* a, std::size_t a_len, const uint8_t* b,
                       std::size_t b_len) {
  const std::size_t max_len = a_len > b_len ? a_len : b_len;
  uint8_t diff = static_cast<uint8_t>(a_len ^ b_len);
  for (std::size_t i = 0; i < max_len; ++i) {
    const uint8_t av = i < a_len ? a[i] : 0;
    const uint8_t bv = i < b_len ? b[i] : 0;
    diff = static_cast<uint8_t>(diff | (av ^ bv));
  }
  return diff == 0;
}

}  // namespace viaaccess
