// Mirrors internal/policy/after_hours_test.go from the Go agent.
#include "check.hpp"
#include "viaaccess/after_hours.hpp"
#include "viaaccess/time.hpp"

namespace {

using viaaccess::AfterHoursPolicy;
using viaaccess::IsOutsideAllowedHours;
using viaaccess::LookupTimezoneOffsetSeconds;
using viaaccess::ParseRfc3339;

AfterHoursPolicy SaoPauloOvernight() {
  AfterHoursPolicy policy;
  policy.enabled = true;
  policy.after_time = "22:00";
  policy.before_time = "06:00";
  policy.timezone = "America/Sao_Paulo";
  return policy;
}

VA_TEST(AfterHoursOvernightWindow) {
  const AfterHoursPolicy policy = SaoPauloOvernight();
  // 2026-06-26T02:00:00Z ≈ 23:00 in São Paulo (outside 06:00–22:00)
  CHECK(IsOutsideAllowedHours(ParseRfc3339("2026-06-26T02:00:00Z"), policy));
  // 2026-06-25T15:00:00Z ≈ 12:00 in São Paulo (inside window)
  CHECK(!IsOutsideAllowedHours(ParseRfc3339("2026-06-25T15:00:00Z"), policy));
}

VA_TEST(AfterHoursInvalidTimezoneDoesNotBlock) {
  AfterHoursPolicy policy = SaoPauloOvernight();
  policy.timezone = "Not/A_Timezone";
  CHECK(!IsOutsideAllowedHours(ParseRfc3339("2026-06-26T02:00:00Z"), policy));
}

VA_TEST(AfterHoursDisabledDoesNotBlock) {
  AfterHoursPolicy policy = SaoPauloOvernight();
  policy.enabled = false;
  CHECK(!IsOutsideAllowedHours(ParseRfc3339("2026-06-26T02:00:00Z"), policy));
}

VA_TEST(AfterHoursSameDayRestrictedWindow) {
  // Same-day params mean the restricted (blocked) window is [after, before).
  AfterHoursPolicy policy;
  policy.enabled = true;
  policy.after_time = "09:00";
  policy.before_time = "17:00";
  policy.timezone = "UTC";
  CHECK(!IsOutsideAllowedHours(ParseRfc3339("2026-06-25T08:00:00Z"), policy));
  CHECK(IsOutsideAllowedHours(ParseRfc3339("2026-06-25T12:00:00Z"), policy));
  CHECK(!IsOutsideAllowedHours(ParseRfc3339("2026-06-25T17:00:00Z"), policy));
}

VA_TEST(TimezoneOffsetSaoPaulo) {
  int offset = 0;
  CHECK(LookupTimezoneOffsetSeconds("America/Sao_Paulo", &offset));
  CHECK_EQ(offset, -3 * 3600);
  CHECK(!LookupTimezoneOffsetSeconds("Europe/Berlin", &offset));
}

}  // namespace
