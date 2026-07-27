// Frames the byte stream coming from the TTL barcode module into scan lines.
//
// Replaces internal/hidwedge of the Go agent: on the Pi the scanner is a USB
// keyboard and lines are assembled from keycodes, here the EP8280L already
// decodes and emits the payload followed by the configured suffix (CR or
// CR+LF). Everything downstream of a completed line is shared.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace viaaccess {

// Dynamic QR URLs carry a JWT in the st parameter and run long, so the guard
// sits well above the ~500 bytes seen in practice. A payload past the limit is
// dropped instead of truncated: a half URL would fail redeem in a way that
// looks like an authorization error rather than a wiring problem.
inline constexpr std::size_t kDefaultMaxScanLineBytes = 1024;

class LineBuffer {
 public:
  explicit LineBuffer(std::size_t max_bytes = kDefaultMaxScanLineBytes);

  // Feed appends a chunk and returns the lines it completed, trimmed and with
  // empty lines dropped.
  std::vector<std::string> Feed(const char* data, std::size_t length);
  std::vector<std::string> Feed(const std::string& chunk);

  void Reset();

  // dropped_lines counts payloads discarded for exceeding max_bytes, surfaced
  // in /health so a misconfigured module baud rate is visible in the field.
  std::size_t dropped_lines() const { return dropped_lines_; }

 private:
  std::string buffer_;
  std::size_t max_bytes_;
  std::size_t dropped_lines_ = 0;
  bool discarding_ = false;
};

}  // namespace viaaccess
