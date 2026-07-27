// Register level tests for the battery-backed clock. Everything here is what the
// firmware cannot check on a bench without a soldered module.
#include "check.hpp"
#include "viaaccess/ds3231.hpp"
#include "viaaccess/time.hpp"

namespace {

using viaaccess::DecodeDs3231Temperature;
using viaaccess::DecodeDs3231Time;
using viaaccess::Ds3231ClearOscillatorStopFlag;
using viaaccess::Ds3231OscillatorStopped;
using viaaccess::EncodeDs3231Time;
using viaaccess::kDs3231TimeRegCount;
using viaaccess::UnixFromCivil;

// 2026-07-27T14:35:09Z, a Monday.
constexpr int64_t kReference = 1785162909;

VA_TEST(DecodeTimeFrom24HourRegisters) {
  const uint8_t regs[kDs3231TimeRegCount] = {0x09, 0x35, 0x14, 0x02, 0x27, 0x07, 0x26};
  CHECK_EQ(DecodeDs3231Time(regs), kReference);
}

// A module that arrives from an Arduino project may still be in 12 hour mode, and
// silently reading bit 5 as part of the hour would put the clock 20 hours off.
VA_TEST(DecodeTimeFrom12HourAfternoon) {
  const uint8_t regs[kDs3231TimeRegCount] = {0x09, 0x35, 0x62, 0x02, 0x27, 0x07, 0x26};
  CHECK_EQ(DecodeDs3231Time(regs), kReference);
}

// Midnight and noon are the two values the naive 12 hour conversion gets wrong.
VA_TEST(DecodeTimeHandlesTwelveAmAndPm) {
  const uint8_t midnight[kDs3231TimeRegCount] = {0x00, 0x00, 0x52, 0x02, 0x27, 0x07, 0x26};
  CHECK_EQ(DecodeDs3231Time(midnight), UnixFromCivil(2026, 7, 27, 0, 0, 0));

  const uint8_t noon[kDs3231TimeRegCount] = {0x00, 0x00, 0x72, 0x02, 0x27, 0x07, 0x26};
  CHECK_EQ(DecodeDs3231Time(noon), UnixFromCivil(2026, 7, 27, 12, 0, 0));
}

VA_TEST(DecodeTimeHonoursCenturyBit) {
  const uint8_t regs[kDs3231TimeRegCount] = {0x09, 0x35, 0x14, 0x02, 0x27, 0x87, 0x26};
  CHECK_EQ(DecodeDs3231Time(regs), UnixFromCivil(2126, 7, 27, 14, 35, 9));
}

// Garbage registers must read as "no time", not as some date in year 2000: the
// caller decides to wait for the network instead of trusting this.
VA_TEST(DecodeTimeRejectsOutOfRangeFields) {
  const uint8_t bad_month[kDs3231TimeRegCount] = {0x09, 0x35, 0x14, 0x02, 0x27, 0x13, 0x26};
  CHECK_EQ(DecodeDs3231Time(bad_month), 0);

  const uint8_t bad_second[kDs3231TimeRegCount] = {0x60, 0x35, 0x14, 0x02, 0x27, 0x07, 0x26};
  CHECK_EQ(DecodeDs3231Time(bad_second), 0);

  const uint8_t zero_day[kDs3231TimeRegCount] = {0x09, 0x35, 0x14, 0x02, 0x00, 0x07, 0x26};
  CHECK_EQ(DecodeDs3231Time(zero_day), 0);

  CHECK_EQ(DecodeDs3231Time(nullptr), 0);
}

VA_TEST(EncodeTimeWrites24HourBcd) {
  uint8_t regs[kDs3231TimeRegCount] = {};
  CHECK(EncodeDs3231Time(kReference, regs));
  CHECK_EQ(static_cast<int>(regs[0]), 0x09);
  CHECK_EQ(static_cast<int>(regs[1]), 0x35);
  CHECK_EQ(static_cast<int>(regs[2]), 0x14);
  CHECK_EQ(static_cast<int>(regs[3]), 0x02);
  CHECK_EQ(static_cast<int>(regs[4]), 0x27);
  CHECK_EQ(static_cast<int>(regs[5]), 0x07);
  CHECK_EQ(static_cast<int>(regs[6]), 0x26);
}

VA_TEST(EncodeThenDecodeRoundTrips) {
  const int64_t samples[] = {
      UnixFromCivil(2000, 1, 1, 0, 0, 0),
      UnixFromCivil(2026, 2, 28, 23, 59, 59),
      UnixFromCivil(2028, 2, 29, 12, 0, 0),  // Leap day.
      UnixFromCivil(2099, 12, 31, 23, 59, 59),
      kReference,
  };
  for (const int64_t sample : samples) {
    uint8_t regs[kDs3231TimeRegCount] = {};
    CHECK(EncodeDs3231Time(sample, regs));
    CHECK_EQ(DecodeDs3231Time(regs), sample);
  }
}

VA_TEST(EncodeTimeRefusesUnrepresentableDates) {
  uint8_t regs[kDs3231TimeRegCount] = {};
  CHECK(!EncodeDs3231Time(0, regs));  // 1970, before the chip epoch.
  CHECK(!EncodeDs3231Time(UnixFromCivil(2200, 1, 1, 0, 0, 0), regs));
  CHECK(!EncodeDs3231Time(kReference, nullptr));
}

VA_TEST(OscillatorStopFlag) {
  CHECK(Ds3231OscillatorStopped(0x88));
  CHECK(!Ds3231OscillatorStopped(0x08));
  // Clearing keeps the alarm flags in the same register untouched.
  CHECK_EQ(static_cast<int>(Ds3231ClearOscillatorStopFlag(0x88)), 0x08);
  CHECK_EQ(static_cast<int>(Ds3231ClearOscillatorStopFlag(0x08)), 0x08);
}

VA_TEST(TemperatureInQuarterDegrees) {
  CHECK_EQ(DecodeDs3231Temperature(0x19, 0x00), 25.0);
  CHECK_EQ(DecodeDs3231Temperature(0x19, 0x40), 25.25);
  CHECK_EQ(DecodeDs3231Temperature(0x19, 0xC0), 25.75);
  // Negative readings count upward from the signed integer part.
  CHECK_EQ(DecodeDs3231Temperature(0xFF, 0x40), -0.75);
  CHECK_EQ(DecodeDs3231Temperature(0xEC, 0x00), -20.0);
}

}  // namespace
