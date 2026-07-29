#include "check.hpp"
#include "viaaccess/ota.hpp"

namespace {

using viaaccess::OtaPayload;
using viaaccess::ValidateOtaPayload;

OtaPayload ValidPayload() {
  OtaPayload payload;
  payload.version = "1.2.3";
  payload.url = "https://example.com/viaaccess-qr-firmware.bin";
  payload.sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  return payload;
}

VA_TEST(OtaPayloadRequiresAllFields) {
  OtaPayload payload = ValidPayload();
  payload.sha256.clear();
  const auto check = ValidateOtaPayload(payload);
  CHECK(!check.ok);
  CHECK_EQ(check.error, "incomplete OTA payload");
}

VA_TEST(OtaPayloadRejectsShortSha256) {
  OtaPayload payload = ValidPayload();
  payload.sha256 = "abcd";
  const auto check = ValidateOtaPayload(payload);
  CHECK(!check.ok);
  CHECK_EQ(check.error, "invalid sha256 length");
}

VA_TEST(OtaPayloadRejectsHttpRemote) {
  OtaPayload payload = ValidPayload();
  payload.url = "http://example.com/fw.bin";
  const auto check = ValidateOtaPayload(payload);
  CHECK(!check.ok);
  CHECK_EQ(check.error, "refusing non-HTTPS OTA URL");
}

VA_TEST(OtaPayloadAllowsLocalhostHttp) {
  OtaPayload payload = ValidPayload();
  payload.url = "http://127.0.0.1:8080/fw.bin";
  const auto check = ValidateOtaPayload(payload);
  CHECK(check.ok);
}

VA_TEST(OtaPayloadNormalizesSha256Case) {
  OtaPayload payload = ValidPayload();
  payload.sha256 = "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
  const auto check = ValidateOtaPayload(payload);
  CHECK(check.ok);
  CHECK_EQ(check.sha256_hex,
           "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
}

}  // namespace
