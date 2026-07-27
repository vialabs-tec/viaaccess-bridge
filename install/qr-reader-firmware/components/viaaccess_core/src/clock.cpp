#include "viaaccess/clock.hpp"

namespace viaaccess {

bool IsPlausibleUnixTime(int64_t unix_seconds) {
  return unix_seconds >= kMinPlausibleUnixTime;
}

bool ClockIsTrusted(const ClockState& state, int64_t now) {
  if (state.source == ClockSource::kNone) {
    return false;
  }
  return IsPlausibleUnixTime(now);
}

const char* ClockSourceString(ClockSource source) {
  switch (source) {
    case ClockSource::kNone:
      return "NONE";
    case ClockSource::kRtc:
      return "RTC";
    case ClockSource::kNetwork:
      return "NETWORK";
  }
  return "NONE";
}

const char* ClockSourceLabelPt(ClockSource source) {
  switch (source) {
    case ClockSource::kNone:
      return "Sem hora confiável";
    case ClockSource::kRtc:
      return "Relógio de bateria (DS3231)";
    case ClockSource::kNetwork:
      return "Sincronizado pela rede (SNTP)";
  }
  return "Sem hora confiável";
}

}  // namespace viaaccess
