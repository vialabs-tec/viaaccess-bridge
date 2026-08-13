// Mirrors internal/mdns/mdns_test.go from the Go agent.
#include "check.hpp"
#include "viaaccess/config.hpp"
#include "viaaccess/hostname.hpp"

namespace {

using viaaccess::HostnameFromAccessPointSlug;
using viaaccess::MigrateLegacyMdnsHostname;
using viaaccess::SanitizeHostname;

const std::string kDefault = viaaccess::kDefaultMdnsHostname;

VA_TEST(SanitizeHostnameNormalizesLabels) {
  CHECK_EQ(SanitizeHostname(""), kDefault);
  CHECK_EQ(SanitizeHostname("ViaAccess-QR"), std::string("viaaccess-qr"));
  CHECK_EQ(SanitizeHostname("viaaccess-qr.local"), std::string("viaaccess-qr"));
  CHECK_EQ(SanitizeHostname("entrada_1"), std::string("entrada-1"));
  CHECK_EQ(SanitizeHostname("---"), kDefault);
  CHECK_EQ(SanitizeHostname("ok"), std::string("ok"));
}

VA_TEST(SanitizeHostnameCapsLabelLength) {
  const std::string long_label(80, 'a');
  const std::string got = SanitizeHostname(long_label);
  CHECK_EQ(got.size(), static_cast<std::size_t>(63));
}

VA_TEST(HostnameDerivedFromAccessPointSlug) {
  CHECK_EQ(HostnameFromAccessPointSlug(""), kDefault);
  CHECK_EQ(HostnameFromAccessPointSlug("entrada-principal"),
           std::string("viaaccess-entrada-principal"));
  CHECK_EQ(HostnameFromAccessPointSlug("Entrada_Principal"),
           std::string("viaaccess-entrada-principal"));
  CHECK_EQ(HostnameFromAccessPointSlug("viaaccess-qr"), kDefault);
  CHECK_EQ(HostnameFromAccessPointSlug("viaaccess"), kDefault);
  CHECK_EQ(HostnameFromAccessPointSlug("viaaccess-qr-porta"),
           std::string("viaaccess-qr-porta"));
  CHECK_EQ(HostnameFromAccessPointSlug("viaaccess-porta"),
           std::string("viaaccess-porta"));
}

VA_TEST(MigrateLegacyMdnsHostnameRewritesQrPrefix) {
  CHECK_EQ(MigrateLegacyMdnsHostname("viaaccess-qr"), kDefault);
  CHECK_EQ(MigrateLegacyMdnsHostname("viaaccess-qr.local"), kDefault);
  CHECK_EQ(MigrateLegacyMdnsHostname("viaaccess-qr-entrada-principal"),
           std::string("viaaccess-entrada-principal"));
  CHECK_EQ(MigrateLegacyMdnsHostname("viaaccess-entrada"),
           std::string("viaaccess-entrada"));
}

}  // namespace
