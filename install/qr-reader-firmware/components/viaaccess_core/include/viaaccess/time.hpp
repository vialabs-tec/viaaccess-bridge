// RFC 3339 conversion in UTC.
//
// Identity timestamps (policy syncedAt, command expiresAt) and the timestamps
// the appliance publishes in /health are RFC 3339. This is implemented here
// rather than through libc so it is unit tested on the host and independent of
// the timezone database, which the firmware does not carry.
#pragma once

#include <cstdint>
#include <string>

namespace viaaccess {

// ParseRfc3339 returns Unix seconds, or 0 when the value is empty or malformed.
// Fractional seconds are accepted and truncated; a numeric offset is applied.
int64_t ParseRfc3339(const std::string& value);

// FormatRfc3339 renders Unix seconds as YYYY-MM-DDTHH:MM:SSZ. Returns an empty
// string for non-positive input so callers can emit JSON null.
std::string FormatRfc3339(int64_t unix_seconds);

// UnixFromCivil converts a UTC calendar instant to Unix seconds. Exposed for the
// RTC path, which reads calendar fields straight out of the chip registers
// instead of parsing a string.
int64_t UnixFromCivil(int64_t year, int month, int day, int hour, int minute, int second);

// CivilFromUnix is the inverse, used to fill those same registers.
void CivilFromUnix(int64_t unix_seconds,
                   int64_t* year,
                   int* month,
                   int* day,
                   int* hour,
                   int* minute,
                   int* second);

}  // namespace viaaccess
