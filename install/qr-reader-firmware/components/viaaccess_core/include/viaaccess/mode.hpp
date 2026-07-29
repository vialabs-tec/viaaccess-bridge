// Appliance posture for passage decisions, ported from internal/agent and the
// freshness rules of internal/policy in the Go agent.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "viaaccess/after_hours.hpp"

namespace viaaccess {

enum class OperationMode {
  kSetup,
  kOnline,
  kContingency,
  kSyncStale,
};

enum class ScanPath {
  kOnline,
  kContingency,
  kBlocked,
};

// TicketVerify carries the HMAC key the appliance needs for offline JWT checks.
struct TicketVerify {
  std::string alg;
  std::string key_b64;
  std::string issuer;
};

// PolicyState is the Identity policy snapshot used for mode, /health and local
// contingency verification.
struct PolicyState {
  // Unix seconds; 0 means never synced.
  int64_t synced_at = 0;
  std::string grant_version;
  std::string access_point_slug;
  std::string trust_key_id;
  int member_grant_count = 0;
  int max_stale_hours = 0;
  bool ticket_verify_ready = false;
  std::string edge_policy_version;
  AfterHoursPolicy after_hours;
  TicketVerify ticket_verify;
  std::vector<std::string> member_ids;
};

struct ModeInput {
  bool configured = false;
  bool identity_reachable = false;
  bool contingency_enabled = false;
  // clock_trusted gates the offline path: ticket expiry and the audit timestamp
  // queued in the outbox are meaningless without a real clock, and a clock
  // running behind makes an expired ticket look valid.
  bool clock_trusted = false;
  PolicyState policy;
  // Unix seconds.
  int64_t now = 0;
};

PolicyState NormalizePolicy(PolicyState policy);

bool PolicyIsFresh(const PolicyState& policy, int64_t now);

// PolicyStaleAgeHours returns -1 when the appliance never synced.
double PolicyStaleAgeHours(const PolicyState& policy, int64_t now);

OperationMode EvaluateOperationMode(const ModeInput& input);

// HealthOk is true when the appliance can accept scans.
bool HealthOk(OperationMode mode);

const char* ModeString(OperationMode mode);

// ModeLabelPt is the technician-facing label surfaced by /health.
const char* ModeLabelPt(OperationMode mode);

const char* ScanPathString(ScanPath path);

}  // namespace viaaccess
