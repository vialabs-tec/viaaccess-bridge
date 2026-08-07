// Mirrors internal/config/config_test.go and remote_apply_test.go from the Go
// agent, with the ESP32-S3 factory pin map instead of the Raspberry Pi one.
#include "check.hpp"
#include "viaaccess/config.hpp"

namespace {

using viaaccess::ApplyRemoteDeviceConfig;
using viaaccess::DefaultRuntimeConfig;
using viaaccess::Normalize;
using viaaccess::NormalizeBleBeaconUuid;
using viaaccess::RemoteDeviceConfig;
using viaaccess::ResetToSetup;
using viaaccess::RuntimeConfig;
using viaaccess::ValidateOperational;

VA_TEST(NormalizeRestoresDefaultPort) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  cfg.http_port = 0;
  cfg = Normalize(cfg);
  CHECK_EQ(cfg.http_port, viaaccess::kDefaultHttpPort);
}

VA_TEST(NormalizeTrimsIdentityUrlAndKey) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  cfg.identity_url = "  https://identity.example/// ";
  cfg.device_key = "  idb_abc  ";
  cfg = Normalize(cfg);
  CHECK_EQ(cfg.identity_url, std::string("https://identity.example"));
  CHECK_EQ(cfg.device_key, std::string("idb_abc"));
}

VA_TEST(FactoryHardwareDefaultsForEsp32s3) {
  const RuntimeConfig cfg = DefaultRuntimeConfig();
  CHECK(cfg.relay.enabled);
  CHECK_EQ(cfg.relay.gpio_pin, 10);
  CHECK_EQ(cfg.relay.pulse_ms, 3000);
  // Low-level trigger, matching the reference module: the inverted setting keeps
  // the coil energized at rest, which releases the door instead of locking it.
  CHECK(!cfg.relay.active_high);
  CHECK(cfg.door_contact.enabled);
  CHECK_EQ(cfg.door_contact.gpio_pin, 11);
  CHECK(cfg.door_contact.active_low);
  CHECK(cfg.exit_button.enabled);
  CHECK_EQ(cfg.exit_button.gpio_pin, 12);
  CHECK(cfg.exit_button.active_low);
  CHECK_EQ(cfg.exit_button.cooldown_ms, 3000);
  CHECK(cfg.status_led.enabled);
  CHECK_EQ(cfg.status_led.red_pin, 4);
  CHECK_EQ(cfg.status_led.green_pin, 5);
  CHECK_EQ(cfg.status_led.blue_pin, 6);
  CHECK(cfg.buzzer.enabled);
  CHECK_EQ(cfg.buzzer.gpio_pin, 7);
  // Low-level I/O, matching the common 3-pin module (idle HIGH = silent).
  CHECK(!cfg.buzzer.active_high);
  CHECK_EQ(cfg.qr_reader.rx_pin, 17);
  CHECK_EQ(cfg.qr_reader.tx_pin, 18);
}

// No default pin may collide with SPI flash (26-32), octal PSRAM (33-37),
// strapping pins (0/3/45/46), native USB (19/20) or the UART0 console (43/44).
VA_TEST(FactoryPinsAvoidReservedEsp32s3Gpios) {
  const RuntimeConfig cfg = DefaultRuntimeConfig();
  const int pins[] = {
      cfg.relay.gpio_pin,      cfg.door_contact.gpio_pin, cfg.exit_button.gpio_pin,
      cfg.status_led.red_pin,  cfg.status_led.green_pin,  cfg.status_led.blue_pin,
      cfg.buzzer.gpio_pin,     cfg.qr_reader.rx_pin,      cfg.qr_reader.tx_pin,
  };
  for (const int pin : pins) {
    CHECK(pin > 0);
    CHECK(pin != 0 && pin != 3 && pin != 45 && pin != 46);
    CHECK(pin != 19 && pin != 20);
    CHECK(pin != 43 && pin != 44);
    CHECK(pin < 26 || pin > 37);
  }
}

VA_TEST(FactoryPinsAreUnique) {
  const RuntimeConfig cfg = DefaultRuntimeConfig();
  const int pins[] = {
      cfg.relay.gpio_pin,      cfg.door_contact.gpio_pin, cfg.exit_button.gpio_pin,
      cfg.status_led.red_pin,  cfg.status_led.green_pin,  cfg.status_led.blue_pin,
      cfg.buzzer.gpio_pin,     cfg.qr_reader.rx_pin,      cfg.qr_reader.tx_pin,
  };
  constexpr int kCount = sizeof(pins) / sizeof(pins[0]);
  for (int i = 0; i < kCount; ++i) {
    for (int j = i + 1; j < kCount; ++j) {
      CHECK(pins[i] != pins[j]);
    }
  }
}

VA_TEST(NormalizePromotesDeprecatedYellowPin) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  cfg.status_led.red_pin = 0;
  cfg.status_led.yellow_pin = 21;
  cfg = Normalize(cfg);
  CHECK_EQ(cfg.status_led.red_pin, 21);
  CHECK_EQ(cfg.status_led.yellow_pin, 0);
}

VA_TEST(ValidateOperationalAcceptsProvisionedConfig) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  cfg.configured = true;
  cfg.identity_url = "http://localhost:3100";
  cfg.device_key = "idb_test";
  CHECK_EQ(ValidateOperational(cfg), std::string(""));
}

VA_TEST(ValidateOperationalRejectsBadKey) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  cfg.configured = true;
  cfg.identity_url = "http://localhost:3100";
  cfg.device_key = "vac_bad";
  CHECK(!ValidateOperational(cfg).empty());
}

VA_TEST(ValidateOperationalRejectsUnconfigured) {
  const RuntimeConfig cfg = DefaultRuntimeConfig();
  CHECK(!ValidateOperational(cfg).empty());
}

VA_TEST(ResetToSetupClearsCredentialsAndKeepsWifi) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  cfg.configured = true;
  cfg.device_key = "idb_test";
  cfg.device_id = "dev_1";
  cfg.provisioned_at = "2026-07-27T12:00:00Z";
  cfg.access_point_slug = "entrada-principal";
  cfg.identity_url = "https://identity.example";
  cfg.wifi.ssid = "portaria";
  cfg.wifi.password = "segredo";

  cfg = ResetToSetup(cfg);

  CHECK(!cfg.configured);
  CHECK_EQ(cfg.device_key, std::string(""));
  CHECK_EQ(cfg.device_id, std::string(""));
  CHECK_EQ(cfg.provisioned_at, std::string(""));
  CHECK_EQ(cfg.access_point_slug, std::string(""));
  CHECK_EQ(cfg.identity_url, std::string("https://identity.example"));
  CHECK_EQ(cfg.wifi.ssid, std::string("portaria"));
  CHECK_EQ(cfg.wifi.password, std::string("segredo"));
}

VA_TEST(ApplyRemoteDeviceConfigDetectsChange) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  cfg.debounce_ms = 2000;
  cfg.emit_detection = true;
  cfg.unlock_on_authorized_only = true;
  cfg.contingency.enabled = true;

  RemoteDeviceConfig remote;
  remote.access_point_slug = "entrada-principal";
  remote.emit_detection = true;
  remote.debounce_ms = 1500;
  remote.unlock_on_authorized_only = true;
  remote.contingency.enabled = true;
  remote.contingency.online_redeem_timeout_ms = 2500;
  remote.contingency.max_policy_stale_hours = 72;

  bool changed = false;
  cfg = ApplyRemoteDeviceConfig(cfg, remote, &changed);

  CHECK(changed);
  CHECK_EQ(cfg.debounce_ms, 1500);
  CHECK_EQ(cfg.access_point_slug, std::string("entrada-principal"));
  CHECK_EQ(cfg.contingency.online_redeem_timeout_ms, 2500);
  CHECK_EQ(cfg.contingency.max_policy_stale_hours, 72);
}

VA_TEST(ApplyRemoteDeviceConfigIsIdempotent) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  cfg.access_point_slug = "entrada-principal";

  RemoteDeviceConfig remote;
  remote.access_point_slug = "entrada-principal";
  remote.emit_detection = cfg.emit_detection;
  remote.debounce_ms = cfg.debounce_ms;
  remote.unlock_on_authorized_only = cfg.unlock_on_authorized_only;
  remote.contingency.enabled = cfg.contingency.enabled;
  remote.contingency.online_redeem_timeout_ms = cfg.contingency.online_redeem_timeout_ms;
  remote.contingency.max_policy_stale_hours = cfg.contingency.max_policy_stale_hours;

  bool changed = true;
  ApplyRemoteDeviceConfig(cfg, remote, &changed);
  CHECK(!changed);
}

// Identity may omit numeric fields; zero must not overwrite local values.
VA_TEST(ApplyRemoteDeviceConfigIgnoresZeroedNumbers) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  cfg.debounce_ms = 2000;

  RemoteDeviceConfig remote;
  remote.emit_detection = cfg.emit_detection;
  remote.unlock_on_authorized_only = cfg.unlock_on_authorized_only;
  remote.contingency.enabled = cfg.contingency.enabled;

  bool changed = true;
  cfg = ApplyRemoteDeviceConfig(cfg, remote, &changed);
  CHECK(!changed);
  CHECK_EQ(cfg.debounce_ms, 2000);
  CHECK_EQ(cfg.contingency.max_policy_stale_hours, 168);
}

VA_TEST(NormalizeBleBeaconUuidAcceptsCanonicalForm) {
  CHECK_EQ(NormalizeBleBeaconUuid("A1B2C3D4-E5F6-7890-ABCD-EF1234567890"),
           std::string("a1b2c3d4-e5f6-7890-abcd-ef1234567890"));
  CHECK_EQ(NormalizeBleBeaconUuid("  {a1b2c3d4-e5f6-7890-abcd-ef1234567890}  "),
           std::string("a1b2c3d4-e5f6-7890-abcd-ef1234567890"));
}

VA_TEST(NormalizeBleBeaconUuidRejectsMalformed) {
  CHECK_EQ(NormalizeBleBeaconUuid(""), std::string(""));
  CHECK_EQ(NormalizeBleBeaconUuid("not-a-uuid"), std::string(""));
  CHECK_EQ(NormalizeBleBeaconUuid("a1b2c3d4e5f67890abcdef1234567890"), std::string(""));
  CHECK_EQ(NormalizeBleBeaconUuid("a1b2c3d4-e5f6-7890-abcd-ef123456789g"),
           std::string(""));
}

VA_TEST(ApplyRemoteDeviceConfigAppliesBleBeacon) {
  RuntimeConfig cfg = DefaultRuntimeConfig();

  RemoteDeviceConfig remote;
  remote.emit_detection = cfg.emit_detection;
  remote.unlock_on_authorized_only = cfg.unlock_on_authorized_only;
  remote.contingency.enabled = cfg.contingency.enabled;
  remote.ble_beacon.present = true;
  remote.ble_beacon.enabled = true;
  remote.ble_beacon.uuid = "A1B2C3D4-E5F6-7890-ABCD-EF1234567890";
  remote.ble_beacon.major = 1;
  remote.ble_beacon.minor = 2;
  remote.ble_beacon.tx_power = -59;

  bool changed = false;
  cfg = ApplyRemoteDeviceConfig(cfg, remote, &changed);

  CHECK(changed);
  CHECK(cfg.ble_beacon.enabled);
  CHECK_EQ(cfg.ble_beacon.uuid, std::string("a1b2c3d4-e5f6-7890-abcd-ef1234567890"));
  CHECK_EQ(cfg.ble_beacon.major, 1);
  CHECK_EQ(cfg.ble_beacon.minor, 2);
  CHECK_EQ(cfg.ble_beacon.tx_power, -59);

  changed = true;
  ApplyRemoteDeviceConfig(cfg, remote, &changed);
  CHECK(!changed);
}

VA_TEST(ApplyRemoteDeviceConfigClearsBleBeaconWhenOmitted) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  cfg.ble_beacon.enabled = true;
  cfg.ble_beacon.uuid = "a1b2c3d4-e5f6-7890-abcd-ef1234567890";
  cfg.ble_beacon.major = 1;
  cfg.ble_beacon.minor = 2;
  cfg.ble_beacon.tx_power = -59;

  RemoteDeviceConfig remote;
  remote.emit_detection = cfg.emit_detection;
  remote.unlock_on_authorized_only = cfg.unlock_on_authorized_only;
  remote.contingency.enabled = cfg.contingency.enabled;
  // ble_beacon.present stays false: Identity cleared proximity.

  bool changed = false;
  cfg = ApplyRemoteDeviceConfig(cfg, remote, &changed);

  CHECK(changed);
  CHECK(!cfg.ble_beacon.enabled);
  CHECK_EQ(cfg.ble_beacon.uuid, std::string(""));
  CHECK_EQ(cfg.ble_beacon.major, 0);
  CHECK_EQ(cfg.ble_beacon.minor, 0);
}

VA_TEST(ApplyRemoteDeviceConfigDisablesBleBeaconWithEmptyUuid) {
  RuntimeConfig cfg = DefaultRuntimeConfig();
  cfg.ble_beacon.enabled = true;
  cfg.ble_beacon.uuid = "a1b2c3d4-e5f6-7890-abcd-ef1234567890";
  cfg.ble_beacon.major = 1;
  cfg.ble_beacon.minor = 2;

  RemoteDeviceConfig remote;
  remote.emit_detection = cfg.emit_detection;
  remote.unlock_on_authorized_only = cfg.unlock_on_authorized_only;
  remote.contingency.enabled = cfg.contingency.enabled;
  remote.ble_beacon.present = true;
  remote.ble_beacon.enabled = true;
  remote.ble_beacon.uuid = "bad";
  remote.ble_beacon.major = 1;
  remote.ble_beacon.minor = 2;
  remote.ble_beacon.tx_power = -59;

  bool changed = false;
  cfg = ApplyRemoteDeviceConfig(cfg, remote, &changed);

  CHECK(changed);
  CHECK(!cfg.ble_beacon.enabled);
  CHECK_EQ(cfg.ble_beacon.uuid, std::string(""));
}

VA_TEST(ApplyRemoteDeviceConfigAcceptsZeroMajorMinor) {
  RuntimeConfig cfg = DefaultRuntimeConfig();

  RemoteDeviceConfig remote;
  remote.emit_detection = cfg.emit_detection;
  remote.unlock_on_authorized_only = cfg.unlock_on_authorized_only;
  remote.contingency.enabled = cfg.contingency.enabled;
  remote.ble_beacon.present = true;
  remote.ble_beacon.enabled = true;
  remote.ble_beacon.uuid = "a1b2c3d4-e5f6-7890-abcd-ef1234567890";
  remote.ble_beacon.major = 0;
  remote.ble_beacon.minor = 0;
  remote.ble_beacon.tx_power = -59;

  bool changed = false;
  cfg = ApplyRemoteDeviceConfig(cfg, remote, &changed);

  CHECK(changed);
  CHECK(cfg.ble_beacon.enabled);
  CHECK_EQ(cfg.ble_beacon.major, 0);
  CHECK_EQ(cfg.ble_beacon.minor, 0);
}

}  // namespace
