// Where the wall clock came from, and whether a passage decision may rely on it.
//
// The appliance has no clock of its own at power-up. SNTP fixes that while the
// network is there, but contingency exists precisely for when it is not, and
// every offline decision is time dependent: the passage ticket expires 45 s
// after it was issued, the policy snapshot has a maximum age, and the audit
// timestamp queued in the outbox is what Identity will record for that passage.
//
// So a clock is either trusted or it is not, and an untrusted clock must block
// the offline path instead of guessing. A clock running behind real time is the
// dangerous case: it makes an expired ticket look valid.
#pragma once

#include <cstdint>

namespace viaaccess {

enum class ClockSource {
  // Power-up state: the chip counts from the epoch and knows nothing.
  kNone,
  // Battery-backed DS3231 read at boot.
  kRtc,
  // SNTP, which also refreshes the RTC when one is fitted.
  kNetwork,
};

struct ClockState {
  ClockSource source = ClockSource::kNone;
  // Unix seconds when the system clock was last set from that source.
  int64_t set_at = 0;
};

// Any timestamp before 2026-01-01 predates this firmware, so it cannot be a real
// reading: it is an unset counter, a DS3231 that lost its cell, or a corrupt
// register read. Cheaper and more robust than trusting a specific sentinel.
inline constexpr int64_t kMinPlausibleUnixTime = 1767225600;

bool IsPlausibleUnixTime(int64_t unix_seconds);

// ClockIsTrusted requires both a real source and a plausible reading, because a
// DS3231 with a flat battery answers happily with garbage.
bool ClockIsTrusted(const ClockState& state, int64_t now);

const char* ClockSourceString(ClockSource source);

// ClockSourceLabelPt is the technician-facing text surfaced by /health.
const char* ClockSourceLabelPt(ClockSource source);

}  // namespace viaaccess
