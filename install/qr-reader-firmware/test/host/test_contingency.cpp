// Mirrors internal/contingency/verify_test.go from the Go agent.
#include "check.hpp"
#include "viaaccess/contingency.hpp"
#include "viaaccess/crypto.hpp"
#include "viaaccess/time.hpp"

#include <string>
#include <vector>

namespace {

using viaaccess::Base64UrlEncode;
using viaaccess::MemoryNonceStore;
using viaaccess::ParseQR;
using viaaccess::ParsedQR;
using viaaccess::PolicyState;
using viaaccess::SignPassageTicketHS256;
using viaaccess::Verify;
using viaaccess::VerifyInput;
using viaaccess::VerifyResult;

constexpr const char* kIssuer = "viaaccess-identity-passage";

std::vector<uint8_t> TestSecret() {
  const std::string secret = "test-passage-ticket-secret-32chars-min";
  return std::vector<uint8_t>(secret.begin(), secret.end());
}

PolicyState TestPolicy(const std::vector<uint8_t>& secret) {
  PolicyState snap;
  snap.grant_version = "gv1";
  snap.access_point_slug = "entrada";
  snap.member_ids = {"mem_1"};
  snap.member_grant_count = 1;
  snap.ticket_verify.alg = "HS256";
  snap.ticket_verify.key_b64 = Base64UrlEncode(secret);
  snap.ticket_verify.issuer = kIssuer;
  snap.ticket_verify_ready = true;
  return snap;
}

VA_TEST(ParseQRRequiresSignedTicket) {
  ParsedQR parsed;
  CHECK(!ParseQR("http://localhost/r/i1?t=tok", &parsed));
}

VA_TEST(ParseQRExtractsIntentAndTicket) {
  ParsedQR parsed;
  CHECK(ParseQR("http://localhost:3100/r/intent_1?t=tok&st=abc.def.ghi", &parsed));
  CHECK_EQ(parsed.intent_id, "intent_1");
  CHECK_EQ(parsed.signed_ticket, "abc.def.ghi");
}

VA_TEST(VerifyAcceptsValidTicketAndBlocksReplay) {
  const std::vector<uint8_t> secret = TestSecret();
  const PolicyState snap = TestPolicy(secret);
  const int64_t now = 1785000000;
  const std::string token = SignPassageTicketHS256(secret, "mem_1", "intent_1", "entrada",
                                                   "gv1", kIssuer, now + 60);
  const std::string qr_url = "http://localhost:3100/r/intent_1?t=tok&st=" + token;

  MemoryNonceStore nonce;
  VerifyResult result = Verify(VerifyInput{qr_url, "entrada", snap, &nonce, now});
  CHECK(result.ok);
  CHECK_EQ(result.member_id, "mem_1");
  CHECK_EQ(result.intent_id, "intent_1");

  VerifyResult replay = Verify(VerifyInput{qr_url, "entrada", snap, &nonce, now});
  CHECK(!replay.ok);
  CHECK_EQ(replay.code, "INTENT_CONSUMED");
}

VA_TEST(VerifyRejectsExpiredTicket) {
  const std::vector<uint8_t> secret = TestSecret();
  const PolicyState snap = TestPolicy(secret);
  const int64_t now = 1785000000;
  const std::string token = SignPassageTicketHS256(secret, "mem_1", "intent_1", "entrada",
                                                   "gv1", kIssuer, now - 1);
  const std::string qr_url = "http://localhost:3100/r/intent_1?st=" + token;
  VerifyResult result = Verify(VerifyInput{qr_url, "entrada", snap, nullptr, now});
  CHECK(!result.ok);
  CHECK_EQ(result.code, "INTENT_EXPIRED");
}

VA_TEST(VerifyRejectsUnknownMember) {
  const std::vector<uint8_t> secret = TestSecret();
  PolicyState snap = TestPolicy(secret);
  snap.member_ids = {"mem_other"};
  const int64_t now = 1785000000;
  const std::string token = SignPassageTicketHS256(secret, "mem_1", "intent_1", "entrada",
                                                   "gv1", kIssuer, now + 60);
  const std::string qr_url = "http://localhost:3100/r/intent_1?st=" + token;
  VerifyResult result = Verify(VerifyInput{qr_url, "entrada", snap, nullptr, now});
  CHECK(!result.ok);
  CHECK_EQ(result.code, "GRANT_DENIED");
}

VA_TEST(VerifyRejectsAccessPointMismatch) {
  const std::vector<uint8_t> secret = TestSecret();
  const PolicyState snap = TestPolicy(secret);
  const int64_t now = 1785000000;
  const std::string token = SignPassageTicketHS256(secret, "mem_1", "intent_1", "entrada",
                                                   "gv1", kIssuer, now + 60);
  const std::string qr_url = "http://localhost:3100/r/intent_1?st=" + token;
  VerifyResult result = Verify(VerifyInput{qr_url, "outra", snap, nullptr, now});
  CHECK(!result.ok);
  CHECK_EQ(result.code, "ACCESS_POINT_MISMATCH");
}

VA_TEST(VerifyBlocksAfterHoursFromSnapshot) {
  const std::vector<uint8_t> secret = TestSecret();
  PolicyState snap = TestPolicy(secret);
  snap.after_hours.enabled = true;
  snap.after_hours.after_time = "22:00";
  snap.after_hours.before_time = "06:00";
  snap.after_hours.timezone = "America/Sao_Paulo";
  // 2026-06-26T02:00:00Z ≈ 23:00 America/Sao_Paulo
  const int64_t outside = viaaccess::ParseRfc3339("2026-06-26T02:00:00Z");
  const std::string token = SignPassageTicketHS256(secret, "mem_1", "intent_2", "entrada",
                                                   "gv1", kIssuer, outside + 3600);
  const std::string qr_url = "http://localhost:3100/r/intent_2?st=" + token;
  VerifyResult result = Verify(VerifyInput{qr_url, "entrada", snap, nullptr, outside});
  CHECK(!result.ok);
  CHECK_EQ(result.code, "AFTER_HOURS");
  CHECK_EQ(result.error, "Passagem fora do horário permitido.");
}

VA_TEST(VerifyAllowsInsideAfterHoursWindow) {
  const std::vector<uint8_t> secret = TestSecret();
  PolicyState snap = TestPolicy(secret);
  snap.after_hours.enabled = true;
  snap.after_hours.after_time = "22:00";
  snap.after_hours.before_time = "06:00";
  snap.after_hours.timezone = "America/Sao_Paulo";
  // 2026-06-25T15:00:00Z ≈ 12:00 America/Sao_Paulo
  const int64_t inside = viaaccess::ParseRfc3339("2026-06-25T15:00:00Z");
  const std::string token = SignPassageTicketHS256(secret, "mem_1", "intent_3", "entrada",
                                                   "gv1", kIssuer, inside + 3600);
  const std::string qr_url = "http://localhost:3100/r/intent_3?st=" + token;
  VerifyResult result = Verify(VerifyInput{qr_url, "entrada", snap, nullptr, inside});
  CHECK(result.ok);
}

VA_TEST(HmacSha256KnownVector) {
  // RFC 4231 case 1 truncated check: key=0x0b*20, data="Hi There"
  std::vector<uint8_t> key(20, 0x0b);
  const std::string data = "Hi There";
  const auto digest = viaaccess::HmacSha256(
      key.data(), key.size(), reinterpret_cast<const uint8_t*>(data.data()), data.size());
  // Matches OpenSSL/Python hmac-sha256 for this classic test vector.
  CHECK_EQ(viaaccess::Base64UrlEncode(digest),
           "sDRMYdjbOFNcqK_OrwvxK4gdwgDJgz2nJuk3bC4yz_c");
}

}  // namespace
