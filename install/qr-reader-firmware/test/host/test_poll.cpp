// Covers the command poll backoff ported from the Go agent command loop.
#include "check.hpp"
#include "viaaccess/poll.hpp"

namespace {

using viaaccess::NextCommandPollDelayMs;

VA_TEST(PollIdlesWhenIdentityHasNothing) {
  CHECK_EQ(NextCommandPollDelayMs(0, 0), viaaccess::kCommandPollIdleMs);
}

// After executing a command the appliance stays hot: the next UNLOCK from the
// dashboard should not wait a full idle cycle.
VA_TEST(PollSpeedsUpAfterCommands) {
  CHECK_EQ(NextCommandPollDelayMs(0, 1), viaaccess::kCommandPollFastMs);
}

VA_TEST(PollHonoursIdentityHint) {
  CHECK_EQ(NextCommandPollDelayMs(5000, 0), 5000);
}

VA_TEST(PollIgnoresHintBelowFastFloor) {
  CHECK_EQ(NextCommandPollDelayMs(500, 1), viaaccess::kCommandPollFastMs);
  CHECK_EQ(NextCommandPollDelayMs(500, 0), viaaccess::kCommandPollIdleMs);
}

VA_TEST(PollClampsLongHint) {
  CHECK_EQ(NextCommandPollDelayMs(600000, 0), viaaccess::kCommandPollMaxMs);
}

VA_TEST(PollIgnoresNegativeHint) {
  CHECK_EQ(NextCommandPollDelayMs(-1, 0), viaaccess::kCommandPollIdleMs);
}

}  // namespace
