// Mirrors internal/setup/provision_test.go from the Go agent.
#include "check.hpp"
#include "viaaccess/provision.hpp"

namespace {

using viaaccess::ParseProvisionInput;
using viaaccess::PreferReachableIdentityURL;
using viaaccess::ProvisionInput;

VA_TEST(ParseProvisionInputRawToken) {
  const ProvisionInput got = ParseProvisionInput("clm_abc123", "https://identity.example");
  CHECK(got.ok);
  CHECK_EQ(got.identity_url, std::string("https://identity.example"));
  CHECK_EQ(got.claim_token, std::string("clm_abc123"));
}

VA_TEST(ParseProvisionInputRawTokenTrimsFallbackSlash) {
  const ProvisionInput got = ParseProvisionInput("clm_abc", " https://identity.example/ ");
  CHECK(got.ok);
  CHECK_EQ(got.identity_url, std::string("https://identity.example"));
}

VA_TEST(ParseProvisionInputUrl) {
  const ProvisionInput got =
      ParseProvisionInput("http://localhost:3100/bridge/provision?t=clm_xyz", "");
  CHECK(got.ok);
  CHECK_EQ(got.identity_url, std::string("http://localhost:3100"));
  CHECK_EQ(got.claim_token, std::string("clm_xyz"));
}

VA_TEST(ParseProvisionInputUrlWithExtraQueryParams) {
  const ProvisionInput got = ParseProvisionInput(
      "https://identity.example/bridge/provision?src=qr&t=clm_xyz&v=2", "");
  CHECK(got.ok);
  CHECK_EQ(got.identity_url, std::string("https://identity.example"));
  CHECK_EQ(got.claim_token, std::string("clm_xyz"));
}

VA_TEST(ParseProvisionInputUrlIgnoresFragment) {
  const ProvisionInput got =
      ParseProvisionInput("https://identity.example/bridge/provision?t=clm_xyz#done", "");
  CHECK(got.ok);
  CHECK_EQ(got.claim_token, std::string("clm_xyz"));
}

VA_TEST(ParseProvisionInputRejectsDeviceKey) {
  const ProvisionInput got = ParseProvisionInput("idb_wrong", "https://identity.example");
  CHECK(!got.ok);
  CHECK(!got.error.empty());
}

VA_TEST(ParseProvisionInputRejectsTokenWithoutIdentityUrl) {
  const ProvisionInput got = ParseProvisionInput("clm_only", "");
  CHECK(!got.ok);
  CHECK(!got.error.empty());
}

VA_TEST(ParseProvisionInputRejectsUrlWithoutToken) {
  const ProvisionInput got =
      ParseProvisionInput("https://identity.example/bridge/provision", "");
  CHECK(!got.ok);
  CHECK(!got.error.empty());
}

VA_TEST(ParseProvisionInputRejectsEmpty) {
  const ProvisionInput got = ParseProvisionInput("   ", "https://identity.example");
  CHECK(!got.ok);
  CHECK(!got.error.empty());
}

VA_TEST(PreferReachableIdentityUrlKeepsLanWhenApiIsLoopback) {
  CHECK_EQ(PreferReachableIdentityURL("http://192.168.5.10:3100", "http://localhost:3100"),
           std::string("http://192.168.5.10:3100"));
}

VA_TEST(PreferReachableIdentityUrlTakesPublicApiUrl) {
  CHECK_EQ(PreferReachableIdentityURL("http://192.168.5.10:3100", "https://identity.example"),
           std::string("https://identity.example"));
}

VA_TEST(PreferReachableIdentityUrlFallsBackToApiWhenNoLocalUrl) {
  CHECK_EQ(PreferReachableIdentityURL("", "http://localhost:3100"),
           std::string("http://localhost:3100"));
}

VA_TEST(PreferReachableIdentityUrlFallsBackToLocalWhenApiEmpty) {
  CHECK_EQ(PreferReachableIdentityURL("http://192.168.5.10:3100", ""),
           std::string("http://192.168.5.10:3100"));
}

VA_TEST(PreferReachableIdentityUrlTreats127AsLoopback) {
  CHECK_EQ(PreferReachableIdentityURL("http://viaaccess-qr.local:3100", "http://127.0.0.1:3100"),
           std::string("http://viaaccess-qr.local:3100"));
}

}  // namespace
