// Offline after_hours evaluation, ported from the Go agent's
// internal/policy/after_hours.go. Matches ViaAccess lib/rules/time-window.ts.
//
// The firmware does not carry an IANA timezone database. Known zones used by
// ViaAccess (notably America/Sao_Paulo, fixed UTC-3 since 2019) are resolved by
// a small offset table; unknown zones fail open, same as Go when LoadLocation
// fails.
#pragma once

#include <cstdint>
#include <string>

namespace viaaccess {

struct AfterHoursPolicy {
  bool enabled = false;
  std::string after_time;
  std::string before_time;
  std::string timezone;
};

// AfterHoursReady is true when the rule is enabled and every param is present.
bool AfterHoursReady(const AfterHoursPolicy& policy);

// IsOutsideAllowedHours returns true when local time in the policy timezone is
// outside the allowed window (i.e. "after hours"). Incomplete or unknown
// timezone → false (fail open).
bool IsOutsideAllowedHours(int64_t unix_seconds, const AfterHoursPolicy& policy);

// LookupTimezoneOffsetSeconds returns true and writes the fixed UTC offset for
// a known IANA id. Exposed for host tests.
bool LookupTimezoneOffsetSeconds(const std::string& timezone, int* offset_seconds);

}  // namespace viaaccess
