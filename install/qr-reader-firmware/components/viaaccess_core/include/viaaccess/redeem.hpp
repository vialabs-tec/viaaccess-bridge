// Result of POST /api/bridge/intent/redeem and the classification rules the
// appliance applies to it, ported from internal/redeem of the Go agent.
#pragma once

#include <string>

namespace viaaccess {

struct RedeemResponse {
  bool ok = false;
  bool redeemed = false;
  std::string validation_id;
  std::string detection_id;
  std::string member_id;
  std::string correlation_outcome;
  std::string access_point_slug;
  std::string error;
  std::string code;
};

struct RedeemResult {
  bool ok = false;
  // HTTP status; 0 means the request never reached Identity.
  int status = 0;
  RedeemResponse data;
};

bool IsAuthorized(const RedeemResult& result);

// IsBridgeAuthFailure reports revoked or disabled device keys, which send the
// appliance back to setup mode without a reboot.
bool IsBridgeAuthFailure(int status, const std::string& code);
bool IsBridgeAuthFailure(const RedeemResult& result);

std::string FormatLog(const RedeemResult& result);

// HttpStatusForScan maps a redeem result to the status POST /scan returns.
int HttpStatusForScan(const RedeemResult& result);

}  // namespace viaaccess
