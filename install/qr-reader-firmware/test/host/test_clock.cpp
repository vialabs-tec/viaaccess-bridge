// Covers the three possible origins of the wall clock and what each one is
// allowed to unlock. There is no Go counterpart: the Pi gets its date from the
// distribution, the appliance has to earn it.
#include "check.hpp"
#include "viaaccess/clock.hpp"

namespace {

using viaaccess::ClockIsTrusted;
using viaaccess::ClockSource;
using viaaccess::ClockSourceString;
using viaaccess::ClockState;
using viaaccess::IsPlausibleUnixTime;
using viaaccess::kMinPlausibleUnixTime;

constexpr int64_t kNow = 1785000000;  // 2026-07-20T09:20:00Z

// Power-up state: the counter starts near the epoch and no source has spoken.
VA_TEST(ClockWithoutSourceIsNotTrusted) {
  ClockState state;
  CHECK(!ClockIsTrusted(state, 42));
  CHECK(!ClockIsTrusted(state, kNow));
}

VA_TEST(ClockFromRtcIsTrusted) {
  ClockState state;
  state.source = ClockSource::kRtc;
  state.set_at = kNow;
  CHECK(ClockIsTrusted(state, kNow));
}

VA_TEST(ClockFromNetworkIsTrusted) {
  ClockState state;
  state.source = ClockSource::kNetwork;
  state.set_at = kNow;
  CHECK(ClockIsTrusted(state, kNow));
}

// The dangerous case a plain "did anything set the clock" flag would miss: a
// DS3231 whose cell died answers with a stale time, and a clock running behind
// makes an already expired passage ticket look valid.
VA_TEST(ClockWithImplausibleReadingIsNotTrusted) {
  ClockState state;
  state.source = ClockSource::kRtc;
  state.set_at = 1000;
  CHECK(!ClockIsTrusted(state, 1000));
}

VA_TEST(PlausibilityFloorIsInclusive) {
  CHECK(IsPlausibleUnixTime(kMinPlausibleUnixTime));
  CHECK(!IsPlausibleUnixTime(kMinPlausibleUnixTime - 1));
  CHECK(!IsPlausibleUnixTime(0));
  CHECK(!IsPlausibleUnixTime(-1));
}

VA_TEST(ClockSourceStringsAreStable) {
  CHECK_EQ(std::string(ClockSourceString(ClockSource::kNone)), std::string("NONE"));
  CHECK_EQ(std::string(ClockSourceString(ClockSource::kRtc)), std::string("RTC"));
  CHECK_EQ(std::string(ClockSourceString(ClockSource::kNetwork)),
           std::string("NETWORK"));
}

}  // namespace
