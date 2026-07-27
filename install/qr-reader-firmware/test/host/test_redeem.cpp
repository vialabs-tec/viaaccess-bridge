// Mirrors internal/redeem/redeem_test.go from the Go agent, including the status
// mapping POST /scan applies so the HTTP contract stays identical.
#include "check.hpp"
#include "viaaccess/redeem.hpp"

namespace {

using viaaccess::FormatLog;
using viaaccess::HttpStatusForScan;
using viaaccess::IsAuthorized;
using viaaccess::IsBridgeAuthFailure;
using viaaccess::RedeemResult;

VA_TEST(IsAuthorizedRequiresOkAndOutcome) {
  RedeemResult result;
  result.ok = true;
  result.data.correlation_outcome = "AUTHORIZED";
  CHECK(IsAuthorized(result));

  result.data.correlation_outcome = "DENIED";
  CHECK(!IsAuthorized(result));

  result.ok = false;
  result.data.correlation_outcome = "AUTHORIZED";
  CHECK(!IsAuthorized(result));
}

VA_TEST(BridgeAuthFailureOn401) { CHECK(IsBridgeAuthFailure(401, "")); }

VA_TEST(BridgeAuthFailureOn403BridgeDisabled) {
  CHECK(IsBridgeAuthFailure(403, "BRIDGE_DISABLED"));
  CHECK(!IsBridgeAuthFailure(403, "FORBIDDEN"));
}

VA_TEST(BridgeAuthFailureIgnoresOtherStatuses) {
  CHECK(!IsBridgeAuthFailure(500, "BRIDGE_DISABLED"));
  CHECK(!IsBridgeAuthFailure(0, ""));
}

VA_TEST(FormatLogSuccess) {
  RedeemResult result;
  result.ok = true;
  result.status = 200;
  result.data.validation_id = "val_1";
  result.data.member_id = "mem_1";
  result.data.correlation_outcome = "AUTHORIZED";
  CHECK_EQ(FormatLog(result),
           std::string("OK validation=val_1 member=mem_1 correlation=AUTHORIZED"));
}

VA_TEST(FormatLogMarksIdempotentReplay) {
  RedeemResult result;
  result.ok = true;
  result.data.redeemed = true;
  CHECK_EQ(FormatLog(result), std::string("OK (idempotente)"));
}

VA_TEST(FormatLogFailureWithCode) {
  RedeemResult result;
  result.status = 403;
  result.data.error = "Chave desabilitada.";
  result.data.code = "BRIDGE_DISABLED";
  CHECK_EQ(FormatLog(result),
           std::string("ERRO 403: Chave desabilitada. [BRIDGE_DISABLED]"));
}

VA_TEST(FormatLogFailureWithoutMessage) {
  RedeemResult result;
  result.status = 502;
  CHECK_EQ(FormatLog(result), std::string("ERRO 502: Falha no resgate."));
}

VA_TEST(ScanStatusOkOnSuccess) {
  RedeemResult result;
  result.ok = true;
  CHECK_EQ(HttpStatusForScan(result), 200);
}

VA_TEST(ScanStatusServiceUnavailableOnStalePolicy) {
  RedeemResult result;
  result.data.code = "SYNC_STALE";
  result.status = 503;
  CHECK_EQ(HttpStatusForScan(result), 503);
}

VA_TEST(ScanStatusForwardsIdentityClientErrors) {
  RedeemResult result;
  result.status = 404;
  CHECK_EQ(HttpStatusForScan(result), 404);
}

// A transport failure has no status; the reader answers 502 so the caller can
// tell "Identity said no" from "Identity was unreachable".
VA_TEST(ScanStatusBadGatewayWhenIdentityUnreachable) {
  RedeemResult result;
  result.status = 0;
  CHECK_EQ(HttpStatusForScan(result), 502);
}

}  // namespace
