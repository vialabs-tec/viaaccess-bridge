#include "viaaccess/redeem.hpp"

#include "viaaccess/strings.hpp"

namespace viaaccess {

bool IsAuthorized(const RedeemResult& result) {
  return result.ok && result.data.correlation_outcome == "AUTHORIZED";
}

bool IsBridgeAuthFailure(int status, const std::string& code) {
  if (status == 401) {
    return true;
  }
  return status == 403 && Trim(code) == "BRIDGE_DISABLED";
}

bool IsBridgeAuthFailure(const RedeemResult& result) {
  return IsBridgeAuthFailure(result.status, result.data.code);
}

std::string FormatLog(const RedeemResult& result) {
  if (result.ok) {
    std::string out = "OK";
    if (!result.data.validation_id.empty()) {
      out += " validation=" + result.data.validation_id;
    }
    if (!result.data.member_id.empty()) {
      out += " member=" + result.data.member_id;
    }
    if (!result.data.correlation_outcome.empty()) {
      out += " correlation=" + result.data.correlation_outcome;
    }
    if (result.data.redeemed) {
      out += " (idempotente)";
    }
    return out;
  }

  std::string message = result.data.error;
  if (message.empty()) {
    message = "Falha no resgate.";
  }
  std::string out = "ERRO " + std::to_string(result.status) + ": " + message;
  if (!result.data.code.empty()) {
    out += " [" + result.data.code + "]";
  }
  return out;
}

int HttpStatusForScan(const RedeemResult& result) {
  if (result.ok) {
    return 200;
  }
  if (result.data.code == "SYNC_STALE") {
    return 503;
  }
  if (result.status >= 400) {
    return result.status;
  }
  return 502;
}

}  // namespace viaaccess
