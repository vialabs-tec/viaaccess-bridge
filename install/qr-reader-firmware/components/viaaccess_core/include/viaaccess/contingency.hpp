// Offline passage verification for CONTINGENCY mode, ported from
// internal/contingency in the Go agent.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "viaaccess/mode.hpp"

namespace viaaccess {

struct ParsedQR {
  std::string intent_id;
  std::string signed_ticket;
};

bool ParseQR(const std::string& qr_url, ParsedQR* out);

// NonceStore tracks consumed intent IDs so a ticket cannot be replayed while
// Identity is unreachable. The firmware persists this to LittleFS; host tests
// use the in-memory implementation below.
class NonceStore {
 public:
  virtual ~NonceStore() = default;
  virtual bool IsConsumed(const std::string& intent_id) const = 0;
  // MarkConsumed returns false when persistence fails; Verify then refuses.
  virtual bool MarkConsumed(const std::string& intent_id, int64_t now_unix) = 0;
};

class MemoryNonceStore : public NonceStore {
 public:
  static constexpr int64_t kRetentionSeconds = 7 * 24 * 3600;

  bool IsConsumed(const std::string& intent_id) const override;
  bool MarkConsumed(const std::string& intent_id, int64_t now_unix) override;

  void Prune(int64_t now_unix);
  void Clear();
  const std::unordered_map<std::string, int64_t>& consumed() const { return consumed_; }
  void Load(std::unordered_map<std::string, int64_t> consumed);

 private:
  std::unordered_map<std::string, int64_t> consumed_;
};

struct VerifyInput {
  std::string qr_url;
  std::string access_point_slug;
  PolicyState policy;
  NonceStore* nonce = nullptr;
  int64_t now = 0;
};

struct VerifyResult {
  bool ok = false;
  std::string member_id;
  std::string intent_id;
  std::string code;
  std::string error;
};

// Verify performs local passage validation during CONTINGENCY mode.
// after_hours is deferred: snapshots without a timezone database cannot evaluate
// local windows the way the Pi does, so this path matches Go when the rule is
// absent (fail open on hours).
VerifyResult Verify(const VerifyInput& input);

// SignPassageTicketHS256 builds a compact JWT for host tests. Not used on device.
std::string SignPassageTicketHS256(const std::vector<uint8_t>& key,
                                   const std::string& subject,
                                   const std::string& intent_id,
                                   const std::string& access_point_slug,
                                   const std::string& grant_version,
                                   const std::string& issuer,
                                   int64_t expires_at_unix);

bool PolicyHasMember(const PolicyState& policy, const std::string& member_id);
bool PolicyTicketVerifyReady(const PolicyState& policy);

}  // namespace viaaccess
