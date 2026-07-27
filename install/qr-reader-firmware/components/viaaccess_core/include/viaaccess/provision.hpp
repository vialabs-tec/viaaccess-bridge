// Zero-touch claim input handling, ported from internal/setup of the Go agent.
// The technician pastes either the full provisioning URL from the admin QR or
// just the clm_ token.
#pragma once

#include <string>

namespace viaaccess {

struct ProvisionInput {
  bool ok = false;
  std::string identity_url;
  std::string claim_token;
  // Technician-facing reason when ok is false.
  std::string error;
};

ProvisionInput ParseProvisionInput(const std::string& raw,
                                   const std::string& fallback_identity_url);

// PreferReachableIdentityURL keeps the URL that already reached Identity on the
// LAN when the claim API answers with a loopback APP_URL, which is common in
// local and dev deployments.
std::string PreferReachableIdentityURL(const std::string& used_for_claim,
                                       const std::string& from_api);

}  // namespace viaaccess
