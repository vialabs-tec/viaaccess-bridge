// Mirrors internal/agent/mode_test.go and the freshness rules of
// internal/policy/store_test.go from the Go agent.
#include "check.hpp"
#include "viaaccess/mode.hpp"

namespace {

using viaaccess::EvaluateOperationMode;
using viaaccess::HealthOk;
using viaaccess::ModeInput;
using viaaccess::ModeString;
using viaaccess::OperationMode;
using viaaccess::PolicyIsFresh;
using viaaccess::PolicyState;
using viaaccess::PolicyStaleAgeHours;

constexpr int64_t kNow = 1785000000;
constexpr int64_t kHour = 3600;

ModeInput Configured() {
  ModeInput input;
  input.configured = true;
  input.contingency_enabled = true;
  input.clock_trusted = true;
  input.now = kNow;
  return input;
}

VA_TEST(ModeSetupWhenNotConfigured) {
  ModeInput input = Configured();
  input.configured = false;
  input.identity_reachable = true;
  CHECK(EvaluateOperationMode(input) == OperationMode::kSetup);
}

VA_TEST(ModeOnlineWhenIdentityReachable) {
  ModeInput input = Configured();
  input.identity_reachable = true;
  input.policy.synced_at = kNow;
  CHECK(EvaluateOperationMode(input) == OperationMode::kOnline);
}

VA_TEST(ModeContingencyWithFreshPolicy) {
  ModeInput input = Configured();
  input.policy.synced_at = kNow - 2 * kHour;
  input.policy.max_stale_hours = 168;
  input.policy.member_grant_count = 10;
  CHECK(EvaluateOperationMode(input) == OperationMode::kContingency);
}

VA_TEST(ModeSyncStaleWithOldPolicy) {
  ModeInput input = Configured();
  input.policy.synced_at = kNow - 200 * kHour;
  input.policy.max_stale_hours = 168;
  input.policy.member_grant_count = 10;
  CHECK(EvaluateOperationMode(input) == OperationMode::kSyncStale);
}

VA_TEST(ModeSyncStaleWhenContingencyDisabled) {
  ModeInput input = Configured();
  input.contingency_enabled = false;
  input.policy.synced_at = kNow;
  input.policy.member_grant_count = 10;
  CHECK(EvaluateOperationMode(input) == OperationMode::kSyncStale);
}

// Boot without network and without a battery-backed clock: the snapshot may be
// perfectly fresh and still unusable, because ticket expiry and the audit
// timestamp would be measured against a counter that started at the epoch.
VA_TEST(ModeSyncStaleWhenClockIsNotTrusted) {
  ModeInput input = Configured();
  input.clock_trusted = false;
  input.policy.synced_at = kNow - kHour;
  input.policy.max_stale_hours = 168;
  input.policy.member_grant_count = 10;
  CHECK(EvaluateOperationMode(input) == OperationMode::kSyncStale);
}

// An untrusted clock never blocks the online path: Identity is the one deciding,
// and it validates expiry with its own clock.
VA_TEST(ModeOnlineDoesNotNeedATrustedClock) {
  ModeInput input = Configured();
  input.clock_trusted = false;
  input.identity_reachable = true;
  CHECK(EvaluateOperationMode(input) == OperationMode::kOnline);
}

// A snapshot with no grants cannot authorize anyone offline, so it is not fresh
// no matter how recent the sync was.
VA_TEST(PolicyWithoutGrantsIsNotFresh) {
  PolicyState policy;
  policy.synced_at = kNow;
  policy.member_grant_count = 0;
  CHECK(!PolicyIsFresh(policy, kNow));
}

// A snapshot timestamped in the future means the clock is wrong; refuse it
// rather than trusting an unbounded window. This matters more on the S3, where
// the battery-backed clock is optional hardware.
VA_TEST(PolicyFromTheFutureIsNotFresh) {
  PolicyState policy;
  policy.synced_at = kNow + kHour;
  policy.member_grant_count = 5;
  CHECK(!PolicyIsFresh(policy, kNow));
}

VA_TEST(PolicyNeverSyncedReportsNegativeAge) {
  PolicyState policy;
  CHECK_EQ(PolicyStaleAgeHours(policy, kNow), -1.0);
}

VA_TEST(PolicyStaleAgeInHours) {
  PolicyState policy;
  policy.synced_at = kNow - 3 * kHour;
  CHECK_EQ(PolicyStaleAgeHours(policy, kNow), 3.0);
}

VA_TEST(HealthOkOnlyWhenPassageAllowed) {
  CHECK(HealthOk(OperationMode::kOnline));
  CHECK(HealthOk(OperationMode::kContingency));
  CHECK(!HealthOk(OperationMode::kSetup));
  CHECK(!HealthOk(OperationMode::kSyncStale));
}

VA_TEST(ModeStringsMatchIdentityContract) {
  CHECK_EQ(std::string(ModeString(OperationMode::kSetup)), std::string("SETUP"));
  CHECK_EQ(std::string(ModeString(OperationMode::kOnline)), std::string("ONLINE"));
  CHECK_EQ(std::string(ModeString(OperationMode::kContingency)), std::string("CONTINGENCY"));
  CHECK_EQ(std::string(ModeString(OperationMode::kSyncStale)), std::string("SYNC_STALE"));
}

}  // namespace
