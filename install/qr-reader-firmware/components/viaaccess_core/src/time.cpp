#include "viaaccess/time.hpp"

#include <cctype>
#include <cstdio>

#include "viaaccess/strings.hpp"

namespace viaaccess {
namespace {

// DaysFromCivil is Howard Hinnant's algorithm: days since 1970-01-01 for a
// proleptic Gregorian date, valid well past any appliance lifetime.
int64_t DaysFromCivil(int64_t year, int month, int day) {
  year -= month <= 2 ? 1 : 0;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const int64_t year_of_era = year - era * 400;
  const int64_t day_of_year =
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int64_t day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146097 + day_of_era - 719468;
}

void CivilFromDays(int64_t days, int64_t* year, int* month, int* day) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const int64_t day_of_era = days - era * 146097;
  const int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 -
                               day_of_era / 146096) /
                              365;
  const int64_t y = year_of_era + era * 400;
  const int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 -
                                            year_of_era / 100);
  const int64_t mp = (5 * day_of_year + 2) / 153;
  *day = static_cast<int>(day_of_year - (153 * mp + 2) / 5 + 1);
  *month = static_cast<int>(mp + (mp < 10 ? 3 : -9));
  *year = y + (*month <= 2 ? 1 : 0);
}

bool ReadInt(const std::string& value, std::size_t offset, std::size_t width, int* out) {
  if (offset + width > value.size()) {
    return false;
  }
  int result = 0;
  for (std::size_t i = 0; i < width; ++i) {
    const char c = value[offset + i];
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
      return false;
    }
    result = result * 10 + (c - '0');
  }
  *out = result;
  return true;
}

}  // namespace

int64_t ParseRfc3339(const std::string& raw) {
  const std::string value = Trim(raw);
  // Shortest accepted form is 1970-01-01T00:00:00Z.
  if (value.size() < 20) {
    return 0;
  }

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (!ReadInt(value, 0, 4, &year) || !ReadInt(value, 5, 2, &month) ||
      !ReadInt(value, 8, 2, &day) || !ReadInt(value, 11, 2, &hour) ||
      !ReadInt(value, 14, 2, &minute) || !ReadInt(value, 17, 2, &second)) {
    return 0;
  }
  if (value[4] != '-' || value[7] != '-' || value[13] != ':' || value[16] != ':') {
    return 0;
  }
  const char date_time_separator = value[10];
  if (date_time_separator != 'T' && date_time_separator != 't' &&
      date_time_separator != ' ') {
    return 0;
  }
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
      second > 60) {
    return 0;
  }

  std::size_t cursor = 19;
  if (cursor < value.size() && value[cursor] == '.') {
    ++cursor;
    while (cursor < value.size() &&
           std::isdigit(static_cast<unsigned char>(value[cursor])) != 0) {
      ++cursor;
    }
  }

  int64_t offset_seconds = 0;
  if (cursor < value.size()) {
    const char zone = value[cursor];
    if (zone == 'Z' || zone == 'z') {
      // UTC.
    } else if (zone == '+' || zone == '-') {
      int offset_hour = 0;
      int offset_minute = 0;
      if (!ReadInt(value, cursor + 1, 2, &offset_hour)) {
        return 0;
      }
      // Both +HH:MM and +HHMM appear in the wild.
      const std::size_t minute_offset = (cursor + 3 < value.size() &&
                                         value[cursor + 3] == ':')
                                            ? cursor + 4
                                            : cursor + 3;
      if (!ReadInt(value, minute_offset, 2, &offset_minute)) {
        return 0;
      }
      offset_seconds = static_cast<int64_t>(offset_hour) * 3600 + offset_minute * 60;
      if (zone == '+') {
        offset_seconds = -offset_seconds;
      }
    } else {
      return 0;
    }
  }

  return UnixFromCivil(year, month, day, hour, minute, second) + offset_seconds;
}

int64_t UnixFromCivil(int64_t year, int month, int day, int hour, int minute,
                      int second) {
  return DaysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
}

void CivilFromUnix(int64_t unix_seconds, int64_t* year, int* month, int* day,
                   int* hour, int* minute, int* second) {
  int64_t days = unix_seconds / 86400;
  int64_t remainder = unix_seconds % 86400;
  if (remainder < 0) {
    remainder += 86400;
    --days;
  }
  CivilFromDays(days, year, month, day);
  *hour = static_cast<int>(remainder / 3600) % 24;
  *minute = static_cast<int>(remainder / 60) % 60;
  *second = static_cast<int>(remainder) % 60;
}

std::string FormatRfc3339(int64_t unix_seconds) {
  if (unix_seconds <= 0) {
    return "";
  }

  int64_t year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  CivilFromUnix(unix_seconds, &year, &month, &day, &hour, &minute, &second);
  // RFC 3339 only has room for a four digit year, and bounding every component
  // explicitly is also what lets the compiler prove the buffer cannot overflow.
  if (year < 0 || year > 9999) {
    return "";
  }
  hour %= 24;
  minute %= 60;
  second %= 60;

  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                static_cast<int>(year), month, day, hour, minute, second);
  return buffer;
}

}  // namespace viaaccess
