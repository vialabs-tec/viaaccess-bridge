// HTTP client for the Identity bridge API.
//
// Ports internal/redeem, internal/syncclient and internal/unlock from the Go
// agent onto esp_http_client. Endpoints, headers, JSON shapes and the revoked
// key rules (401 or 403 BRIDGE_DISABLED) are unchanged, so Identity cannot tell
// a Raspberry Pi from an ESP32-S3 apart from X-ViaAccess-Agent-Version.
#pragma once

#include <string>
#include <vector>

#include "esp_err.h"
#include "viaaccess/config.hpp"
#include "viaaccess/mode.hpp"
#include "viaaccess/outbox.hpp"
#include "viaaccess/redeem.hpp"

namespace identity {

// Outcome carries the two failure modes the callers branch on: a revoked device
// key sends the appliance back to setup, anything else is retried next cycle.
struct Outcome {
  bool ok = false;
  bool unauthorized = false;
  std::string error;
};

struct ClaimResult {
  bool ok = false;
  std::string error;
  std::string device_id;
  std::string device_key;
  std::string identity_url;
  std::string access_point_slug;
  /** Local /setup PIN from Identity; persist and require on later writes. */
  std::string setup_pin;
  bool emit_detection = true;
  int debounce_ms = 0;
  bool unlock_on_authorized_only = true;
  viaaccess::ContingencyConfig contingency;
};

struct PolicyFetch {
  Outcome outcome;
  std::string raw_json;
  viaaccess::PolicyState policy;
};

struct DeviceConfigFetch {
  Outcome outcome;
  bool not_modified = false;
  std::string etag;
  viaaccess::RemoteDeviceConfig config;
};

struct PendingCommand {
  std::string id;
  std::string type;
  std::string reason;
  bool has_ota_payload = false;
  std::string ota_version;
  std::string ota_url;
  std::string ota_sha256;
};

struct CommandsFetch {
  Outcome outcome;
  std::vector<PendingCommand> commands;
  int poll_after_ms = 0;
};

struct UnlockWebhookPayload {
  std::string member_id;
  std::string validation_id;
  std::string detection_id;
  std::string correlation_outcome;
  std::string access_point_slug;
};

struct UnlockWebhookResult {
  bool ok = false;
  int status = 0;
  std::string error;
};

struct FlushResult {
  Outcome outcome;
  int flushed = 0;
  int skipped = 0;
};

// RedeemQrUrl posts the scanned URL to /api/bridge/intent/redeem. A status of 0
// means the request never reached Identity, which is what triggers the
// contingency path in the Go agent.
viaaccess::RedeemResult RedeemQrUrl(const viaaccess::RuntimeConfig& cfg,
                                    const std::string& qr_url,
                                    int timeout_ms);

// Ping checks reachability the same way the Go agent does, with GET /api/openapi.
esp_err_t Ping(const std::string& identity_url, int timeout_ms);

// ClaimProvision exchanges a one-time clm_ token for device credentials.
ClaimResult ClaimProvision(const std::string& identity_url,
                           const std::string& claim_token,
                           int timeout_ms);

PolicyFetch FetchPolicySnapshot(const viaaccess::RuntimeConfig& cfg);

DeviceConfigFetch FetchDeviceConfig(const viaaccess::RuntimeConfig& cfg,
                                    const std::string& if_none_match);

CommandsFetch FetchCommands(const viaaccess::RuntimeConfig& cfg);

Outcome AckCommand(const viaaccess::RuntimeConfig& cfg,
                   const std::string& command_id,
                   bool ok,
                   const std::string& error);

// kind is opened, closed or held_open.
Outcome PostDoorContactEvent(const viaaccess::RuntimeConfig& cfg, const std::string& kind);

// kind is pressed. Identity opens a grace window so the door-contact opened
// that follows is not classified as forced entry.
Outcome PostExitButtonEvent(const viaaccess::RuntimeConfig& cfg, const std::string& kind);

// PostUnlockWebhook notifies a third-party controller when the integrator wired
// one instead of the local relay.
UnlockWebhookResult PostUnlockWebhook(const std::string& url,
                                      const UnlockWebhookPayload& payload,
                                      int timeout_ms);

// FlushOutbox posts offline contingency passages to Identity.
FlushResult FlushOutbox(const viaaccess::RuntimeConfig& cfg,
                        const std::vector<viaaccess::OutboxEvent>& events);

}  // namespace identity
