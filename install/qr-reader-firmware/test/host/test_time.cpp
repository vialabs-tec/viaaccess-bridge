// Covers RFC 3339 handling, which decides policy freshness and therefore
// whether the appliance lets anyone through while offline.
#include "check.hpp"
#include "viaaccess/time.hpp"

namespace {

using viaaccess::FormatRfc3339;
using viaaccess::ParseRfc3339;

VA_TEST(ParseRfc3339Epoch) {
  CHECK_EQ(ParseRfc3339("1970-01-01T00:00:00Z"), static_cast<int64_t>(0));
}

VA_TEST(ParseRfc3339KnownInstant) {
  CHECK_EQ(ParseRfc3339("2026-07-27T12:00:00Z"), static_cast<int64_t>(1785153600));
}

VA_TEST(ParseRfc3339HandlesLeapDay) {
  CHECK_EQ(ParseRfc3339("2024-02-29T00:00:00Z"), static_cast<int64_t>(1709164800));
}

VA_TEST(ParseRfc3339TruncatesFractionalSeconds) {
  CHECK_EQ(ParseRfc3339("2026-07-27T12:00:00.512Z"), static_cast<int64_t>(1785153600));
}

VA_TEST(ParseRfc3339AppliesNumericOffset) {
  CHECK_EQ(ParseRfc3339("2026-07-27T09:00:00-03:00"), static_cast<int64_t>(1785153600));
  CHECK_EQ(ParseRfc3339("2026-07-27T09:00:00-0300"), static_cast<int64_t>(1785153600));
  CHECK_EQ(ParseRfc3339("2026-07-27T15:00:00+03:00"), static_cast<int64_t>(1785153600));
}

VA_TEST(ParseRfc3339RejectsGarbage) {
  CHECK_EQ(ParseRfc3339(""), static_cast<int64_t>(0));
  CHECK_EQ(ParseRfc3339("not-a-timestamp"), static_cast<int64_t>(0));
  CHECK_EQ(ParseRfc3339("2026-07-27"), static_cast<int64_t>(0));
  CHECK_EQ(ParseRfc3339("2026-13-01T00:00:00Z"), static_cast<int64_t>(0));
  CHECK_EQ(ParseRfc3339("2026-07-27T12:00:00 BRT"), static_cast<int64_t>(0));
}

VA_TEST(FormatRfc3339RoundTrips) {
  const std::string value = "2026-07-27T12:34:56Z";
  CHECK_EQ(FormatRfc3339(ParseRfc3339(value)), value);
}

VA_TEST(FormatRfc3339EmptyWhenUnset) {
  CHECK_EQ(FormatRfc3339(0), std::string(""));
  CHECK_EQ(FormatRfc3339(-1), std::string(""));
}

}  // namespace
