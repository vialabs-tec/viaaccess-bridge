// Mirrors internal/scan/handler_test.go from the Go agent: debounce window, QR
// extraction precedence and the relay/unlock decisions.
#include "check.hpp"
#include "viaaccess/config.hpp"
#include "viaaccess/redeem.hpp"
#include "viaaccess/scan.hpp"

namespace {

using viaaccess::Debounce;
using viaaccess::DefaultRuntimeConfig;
using viaaccess::Mark;
using viaaccess::RedeemResult;
using viaaccess::RuntimeConfig;
using viaaccess::SelectQrUrl;
using viaaccess::ShouldIgnore;
using viaaccess::ShouldPostUnlock;
using viaaccess::ShouldPulseRelay;

RedeemResult Authorized() {
  RedeemResult result;
  result.ok = true;
  result.status = 200;
  result.data.correlation_outcome = "AUTHORIZED";
  return result;
}

RedeemResult Denied() {
  RedeemResult result;
  result.ok = true;
  result.status = 200;
  result.data.correlation_outcome = "DENIED";
  return result;
}

VA_TEST(DebounceIgnoresRepeatInsideWindow) {
  Debounce debounce;
  Mark(debounce, "https://id.example/q?st=1", 1000);
  CHECK(ShouldIgnore(debounce, "https://id.example/q?st=1", 2500, 2000));
}

VA_TEST(DebounceAllowsRepeatAfterWindow) {
  Debounce debounce;
  Mark(debounce, "https://id.example/q?st=1", 1000);
  CHECK(!ShouldIgnore(debounce, "https://id.example/q?st=1", 3001, 2000));
}

VA_TEST(DebounceAllowsDifferentQrImmediately) {
  Debounce debounce;
  Mark(debounce, "https://id.example/q?st=1", 1000);
  CHECK(!ShouldIgnore(debounce, "https://id.example/q?st=2", 1001, 2000));
}

VA_TEST(DebounceDisabledWithZeroWindow) {
  Debounce debounce;
  Mark(debounce, "https://id.example/q?st=1", 1000);
  CHECK(!ShouldIgnore(debounce, "https://id.example/q?st=1", 1000, 0));
}

VA_TEST(SelectQrUrlPrefersQrUrlField) {
  CHECK_EQ(SelectQrUrl("a", "b", "c", "d"), std::string("a"));
  CHECK_EQ(SelectQrUrl("", "b", "c", "d"), std::string("b"));
  CHECK_EQ(SelectQrUrl("", "", "c", "d"), std::string("c"));
}

// The Go agent accepts a bare URL body from simple readers.
VA_TEST(SelectQrUrlFallsBackToRawBody) {
  CHECK_EQ(SelectQrUrl("", "", "", "  https://id.example/q?st=1  "),
           std::string("https://id.example/q?st=1"));
}

VA_TEST(SelectQrUrlEmptyWhenNothingUsable) {
  CHECK_EQ(SelectQrUrl("", "", "", "   "), std::string(""));
}

VA_TEST(SelectQrUrlTrimsWhitespaceOnlyFields) {
  CHECK_EQ(SelectQrUrl("  ", "b", "", ""), std::string("b"));
}

VA_TEST(RelayPulsesOnlyOnAuthorizedByDefault) {
  const RuntimeConfig cfg = DefaultRuntimeConfig();
  CHECK(cfg.unlock_on_authorized_only);
  CHECK(ShouldPulseRelay(cfg, Authorized()));
  CHECK(!ShouldPulseRelay(cfg, Denied()));
}

VA_TEST(RelayPulsesOnAnyOkWhenAuthorizedOnlyDisabled) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  cfg.unlock_on_authorized_only = false;
  CHECK(ShouldPulseRelay(cfg, Denied()));
}

VA_TEST(RelayStaysQuietWhenDisabled) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  cfg.relay.enabled = false;
  CHECK(!ShouldPulseRelay(cfg, Authorized()));
}

VA_TEST(UnlockWebhookRequiresConfiguredUrl) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  CHECK(!ShouldPostUnlock(cfg, Authorized()));
  cfg.unlock_webhook_url = "http://192.168.0.9/unlock";
  CHECK(ShouldPostUnlock(cfg, Authorized()));
  CHECK(!ShouldPostUnlock(cfg, Denied()));
}

VA_TEST(UnlockWebhookSkippedOnFailedRedeem) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  cfg.unlock_webhook_url = "http://192.168.0.9/unlock";
  cfg.unlock_on_authorized_only = false;
  RedeemResult failed;
  failed.ok = false;
  failed.status = 502;
  CHECK(!ShouldPostUnlock(cfg, failed));
}

}  // namespace
