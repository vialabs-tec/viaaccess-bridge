#include "viaaccess/after_hours.hpp"

#include "viaaccess/strings.hpp"
#include "viaaccess/time.hpp"

#include <cstdlib>

namespace viaaccess {
namespace {

int ParseClockToMinutes(const std::string& value) {
  const std::string trimmed = Trim(value);
  const auto colon = trimmed.find(':');
  if (colon == std::string::npos) {
    return 0;
  }
  const int hour = std::atoi(trimmed.substr(0, colon).c_str());
  const int minute = std::atoi(trimmed.substr(colon + 1).c_str());
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    return 0;
  }
  return hour * 60 + minute;
}

}  // namespace

bool AfterHoursReady(const AfterHoursPolicy& policy) {
  return policy.enabled && !Trim(policy.after_time).empty() &&
         !Trim(policy.before_time).empty() && !Trim(policy.timezone).empty();
}

bool LookupTimezoneOffsetSeconds(const std::string& timezone, int* offset_seconds) {
  if (offset_seconds == nullptr) {
    return false;
  }
  const std::string id = Trim(timezone);
  // America/Sao_Paulo has been fixed UTC-3 since Brazil abolished DST (2019).
  if (id == "America/Sao_Paulo" || id == "Brazil/East") {
    *offset_seconds = -3 * 3600;
    return true;
  }
  if (id == "UTC" || id == "Etc/UTC" || id == "GMT" || id == "Etc/GMT") {
    *offset_seconds = 0;
    return true;
  }
  return false;
}

bool IsOutsideAllowedHours(int64_t unix_seconds, const AfterHoursPolicy& policy) {
  if (!AfterHoursReady(policy)) {
    return false;
  }

  int offset_seconds = 0;
  if (!LookupTimezoneOffsetSeconds(policy.timezone, &offset_seconds)) {
    return false;
  }

  int64_t year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  CivilFromUnix(unix_seconds + offset_seconds, &year, &month, &day, &hour, &minute,
                &second);

  const int now_minutes = hour * 60 + minute;
  const int after_minutes = ParseClockToMinutes(policy.after_time);
  const int before_minutes = ParseClockToMinutes(policy.before_time);

  if (after_minutes > before_minutes) {
    // Overnight window (e.g. 22:00–06:00): outside when now is in [after, 24h)
    // or [0, before).
    return now_minutes >= after_minutes || now_minutes < before_minutes;
  }
  return now_minutes >= after_minutes && now_minutes < before_minutes;
}

}  // namespace viaaccess
