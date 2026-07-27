#include "viaaccess/ds3231.hpp"

#include "viaaccess/time.hpp"

namespace viaaccess {
namespace {

constexpr uint8_t kOscillatorStopFlag = 0x80;
constexpr uint8_t kHour12Mode = 0x40;
constexpr uint8_t kHour12Pm = 0x20;
constexpr uint8_t kCentury = 0x80;

int FromBcd(uint8_t value, uint8_t mask) {
  const uint8_t masked = value & mask;
  return (masked >> 4) * 10 + (masked & 0x0F);
}

uint8_t ToBcd(int value) {
  return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

}  // namespace

bool Ds3231OscillatorStopped(uint8_t status) {
  return (status & kOscillatorStopFlag) != 0;
}

uint8_t Ds3231ClearOscillatorStopFlag(uint8_t status) {
  return static_cast<uint8_t>(status & ~kOscillatorStopFlag);
}

int64_t DecodeDs3231Time(const uint8_t* regs) {
  if (regs == nullptr) {
    return 0;
  }

  const int second = FromBcd(regs[0], 0x7F);
  const int minute = FromBcd(regs[1], 0x7F);

  int hour = 0;
  if ((regs[2] & kHour12Mode) != 0) {
    // 12 hour mode, as an Arduino sketch may have left it: 12 AM is hour 0 and
    // 12 PM is hour 12, which the naive conversion gets wrong.
    hour = FromBcd(regs[2], 0x1F);
    const bool pm = (regs[2] & kHour12Pm) != 0;
    if (hour == 12) {
      hour = pm ? 12 : 0;
    } else if (pm) {
      hour += 12;
    }
  } else {
    hour = FromBcd(regs[2], 0x3F);
  }

  const int day = FromBcd(regs[4], 0x3F);
  const int month = FromBcd(regs[5], 0x1F);
  const int64_t year =
      2000 + FromBcd(regs[6], 0xFF) + ((regs[5] & kCentury) != 0 ? 100 : 0);

  if (second > 59 || minute > 59 || hour > 23 || day < 1 || day > 31 || month < 1 ||
      month > 12) {
    return 0;
  }
  return UnixFromCivil(year, month, day, hour, minute, second);
}

bool EncodeDs3231Time(int64_t unix_seconds, uint8_t* regs) {
  if (regs == nullptr) {
    return false;
  }

  int64_t year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  CivilFromUnix(unix_seconds, &year, &month, &day, &hour, &minute, &second);
  if (year < 2000 || year > 2199) {
    return false;
  }

  // The chip keeps a day of week it never validates, and nothing in the
  // appliance reads it. Deriving it anyway keeps the registers coherent for
  // whatever tool a technician points at the module.
  const int64_t days = UnixFromCivil(year, month, day, 0, 0, 0) / 86400;
  const int weekday = static_cast<int>((days + 4) % 7) + 1;  // 1970-01-01 was Thursday.

  regs[0] = ToBcd(second);
  regs[1] = ToBcd(minute);
  regs[2] = ToBcd(hour);  // Bit 6 clear selects 24 hour mode.
  regs[3] = static_cast<uint8_t>(weekday);
  regs[4] = ToBcd(day);
  regs[5] = ToBcd(month);
  if (year >= 2100) {
    regs[5] |= kCentury;
  }
  regs[6] = ToBcd(static_cast<int>(year % 100));
  return true;
}

double DecodeDs3231Temperature(uint8_t msb, uint8_t lsb) {
  const double degrees = static_cast<double>(static_cast<int8_t>(msb));
  const double fraction = static_cast<double>((lsb >> 6) & 0x03) * 0.25;
  // The fractional part counts upward from the signed integer part, so a
  // negative reading of -0.75 C is stored as msb -1 plus 0.25.
  return degrees + fraction;
}

}  // namespace viaaccess
