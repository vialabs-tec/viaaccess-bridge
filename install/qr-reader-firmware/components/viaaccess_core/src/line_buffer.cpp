#include "viaaccess/line_buffer.hpp"

#include "viaaccess/strings.hpp"

namespace viaaccess {

LineBuffer::LineBuffer(std::size_t max_bytes)
    : max_bytes_(max_bytes > 0 ? max_bytes : kDefaultMaxScanLineBytes) {}

void LineBuffer::Reset() {
  buffer_.clear();
  discarding_ = false;
}

std::vector<std::string> LineBuffer::Feed(const char* data, std::size_t length) {
  std::vector<std::string> lines;
  for (std::size_t i = 0; i < length; ++i) {
    const char c = data[i];
    if (c == '\r' || c == '\n') {
      if (discarding_) {
        discarding_ = false;
        buffer_.clear();
        continue;
      }
      const std::string line = Trim(buffer_);
      buffer_.clear();
      if (!line.empty()) {
        lines.push_back(line);
      }
      continue;
    }
    if (discarding_) {
      continue;
    }
    if (buffer_.size() >= max_bytes_) {
      discarding_ = true;
      buffer_.clear();
      ++dropped_lines_;
      continue;
    }
    buffer_.push_back(c);
  }
  return lines;
}

std::vector<std::string> LineBuffer::Feed(const std::string& chunk) {
  return Feed(chunk.data(), chunk.size());
}

}  // namespace viaaccess
