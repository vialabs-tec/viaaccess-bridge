#include "viaaccess/mode.hpp"

#include "viaaccess/config.hpp"

namespace viaaccess {
namespace {

constexpr int64_t kSecondsPerHour = 3600;

}  // namespace

PolicyState NormalizePolicy(PolicyState policy) {
  if (policy.max_stale_hours <= 0) {
    policy.max_stale_hours = kDefaultMaxPolicyStaleHours;
  }
  return policy;
}

bool PolicyIsFresh(const PolicyState& policy, int64_t now) {
  const PolicyState p = NormalizePolicy(policy);
  if (p.synced_at <= 0 || p.member_grant_count <= 0) {
    return false;
  }
  const int64_t age = now - p.synced_at;
  return age >= 0 && age <= static_cast<int64_t>(p.max_stale_hours) * kSecondsPerHour;
}

double PolicyStaleAgeHours(const PolicyState& policy, int64_t now) {
  if (policy.synced_at <= 0) {
    return -1;
  }
  return static_cast<double>(now - policy.synced_at) / static_cast<double>(kSecondsPerHour);
}

OperationMode EvaluateOperationMode(const ModeInput& input) {
  if (!input.configured) {
    return OperationMode::kSetup;
  }
  if (input.identity_reachable) {
    return OperationMode::kOnline;
  }
  if (!input.contingency_enabled || !input.clock_trusted) {
    return OperationMode::kSyncStale;
  }
  if (PolicyIsFresh(input.policy, input.now)) {
    return OperationMode::kContingency;
  }
  return OperationMode::kSyncStale;
}

bool HealthOk(OperationMode mode) {
  return mode == OperationMode::kOnline || mode == OperationMode::kContingency;
}

const char* ModeString(OperationMode mode) {
  switch (mode) {
    case OperationMode::kSetup:
      return "SETUP";
    case OperationMode::kOnline:
      return "ONLINE";
    case OperationMode::kContingency:
      return "CONTINGENCY";
    case OperationMode::kSyncStale:
      return "SYNC_STALE";
  }
  return "SYNC_STALE";
}

const char* ModeLabelPt(OperationMode mode) {
  switch (mode) {
    case OperationMode::kSetup:
      return "Aguardando provisionamento";
    case OperationMode::kOnline:
      return "Online (redeem em tempo real)";
    case OperationMode::kContingency:
      return "Contingência (validação local, último sync)";
    case OperationMode::kSyncStale:
      return "Sync desatualizado, passagem bloqueada";
  }
  return "Sync desatualizado, passagem bloqueada";
}

const char* ScanPathString(ScanPath path) {
  switch (path) {
    case ScanPath::kOnline:
      return "ONLINE";
    case ScanPath::kContingency:
      return "CONTINGENCY";
    case ScanPath::kBlocked:
      return "BLOCKED";
  }
  return "BLOCKED";
}

}  // namespace viaaccess
