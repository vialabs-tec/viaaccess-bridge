// Command poll backoff, ported from the Go agent command loop. Identity runs on
// Vercel with no long-poll, so the appliance idles slowly and speeds up only
// while Identity signals activity through pollAfterMs.
#pragma once

namespace viaaccess {

// Floors for Identity pollAfterMs hints. Keep in sync with
// viaaccess-identity BRIDGE_COMMAND_POLL_* constants.
inline constexpr int kCommandPollIdleMs = 3000;
inline constexpr int kCommandPollFastMs = 1000;
inline constexpr int kCommandPollMaxMs = 60000;
inline constexpr int kPolicySyncIntervalMs = 60000;
inline constexpr int kIdentityProbeIntervalMs = 30000;

int NextCommandPollDelayMs(int poll_after_ms,
                           int command_count,
                           int idle_ms = kCommandPollIdleMs,
                           int fast_ms = kCommandPollFastMs,
                           int max_ms = kCommandPollMaxMs);

}  // namespace viaaccess
