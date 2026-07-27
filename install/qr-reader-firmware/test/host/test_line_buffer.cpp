// Covers the UART framing that replaces internal/hidwedge on the ESP32-S3.
#include "check.hpp"
#include "viaaccess/line_buffer.hpp"

namespace {

using viaaccess::LineBuffer;

VA_TEST(LineBufferEmitsOnCarriageReturn) {
  LineBuffer buffer;
  const auto lines = buffer.Feed("https://id.example/q?st=1\r");
  CHECK_EQ(lines.size(), static_cast<std::size_t>(1));
  CHECK_EQ(lines[0], std::string("https://id.example/q?st=1"));
}

// The EP8280L suffix is configurable; CR+LF must not produce an empty second line.
VA_TEST(LineBufferHandlesCrLfSuffix) {
  LineBuffer buffer;
  const auto lines = buffer.Feed("https://id.example/q?st=1\r\n");
  CHECK_EQ(lines.size(), static_cast<std::size_t>(1));
  CHECK_EQ(lines[0], std::string("https://id.example/q?st=1"));
}

// A long dynamic QR arrives split across UART reads.
VA_TEST(LineBufferAssemblesAcrossChunks) {
  LineBuffer buffer;
  CHECK(buffer.Feed("https://id.exa").empty());
  CHECK(buffer.Feed("mple/q?st=abc").empty());
  const auto lines = buffer.Feed("def\r\n");
  CHECK_EQ(lines.size(), static_cast<std::size_t>(1));
  CHECK_EQ(lines[0], std::string("https://id.example/q?st=abcdef"));
}

VA_TEST(LineBufferEmitsMultipleLinesInOneChunk) {
  LineBuffer buffer;
  const auto lines = buffer.Feed("first\r\nsecond\r\n");
  CHECK_EQ(lines.size(), static_cast<std::size_t>(2));
  CHECK_EQ(lines[0], std::string("first"));
  CHECK_EQ(lines[1], std::string("second"));
}

VA_TEST(LineBufferDropsEmptyLines) {
  LineBuffer buffer;
  CHECK(buffer.Feed("\r\n\r\n").empty());
}

VA_TEST(LineBufferTrimsSurroundingWhitespace) {
  LineBuffer buffer;
  const auto lines = buffer.Feed("  payload  \r");
  CHECK_EQ(lines.size(), static_cast<std::size_t>(1));
  CHECK_EQ(lines[0], std::string("payload"));
}

// A truncated URL would fail redeem as if the member were unauthorized, so an
// oversized payload is dropped whole and counted instead.
VA_TEST(LineBufferDropsOversizedPayload) {
  LineBuffer buffer(8);
  const auto dropped = buffer.Feed("0123456789abcdef\r\n");
  CHECK(dropped.empty());
  CHECK_EQ(buffer.dropped_lines(), static_cast<std::size_t>(1));

  const auto recovered = buffer.Feed("short\r\n");
  CHECK_EQ(recovered.size(), static_cast<std::size_t>(1));
  CHECK_EQ(recovered[0], std::string("short"));
}

VA_TEST(LineBufferResetDiscardsPartialLine) {
  LineBuffer buffer;
  buffer.Feed("partial");
  buffer.Reset();
  const auto lines = buffer.Feed("whole\r");
  CHECK_EQ(lines.size(), static_cast<std::size_t>(1));
  CHECK_EQ(lines[0], std::string("whole"));
}

}  // namespace
