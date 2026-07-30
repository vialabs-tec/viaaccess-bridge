#include "viaaccess/config.hpp"

#include "viaaccess/hostname.hpp"
#include "viaaccess/strings.hpp"

namespace viaaccess {

RuntimeConfig DefaultRuntimeConfig() { return RuntimeConfig{}; }

RuntimeConfig Normalize(RuntimeConfig cfg) {
  cfg.identity_url = TrimTrailingSlashes(Trim(cfg.identity_url));
  cfg.device_key = Trim(cfg.device_key);
  cfg.device_id = Trim(cfg.device_id);
  cfg.provisioned_at = Trim(cfg.provisioned_at);
  cfg.access_point_slug = Trim(cfg.access_point_slug);

  cfg.http_host = Trim(cfg.http_host);
  if (cfg.http_host.empty()) {
    cfg.http_host = kDefaultHttpHost;
  }
  if (cfg.http_port <= 0) {
    cfg.http_port = kDefaultHttpPort;
  }
  if (cfg.debounce_ms < 0) {
    cfg.debounce_ms = kDefaultDebounceMs;
  }

  if (cfg.relay.pulse_ms <= 0) {
    cfg.relay.pulse_ms = kDefaultRelayPulseMs;
  }
  if (cfg.relay.gpio_pin <= 0) {
    cfg.relay.gpio_pin = kDefaultRelayPin;
  }

  if (cfg.status_led.red_pin <= 0 && cfg.status_led.yellow_pin > 0) {
    cfg.status_led.red_pin = cfg.status_led.yellow_pin;
  }
  if (cfg.status_led.red_pin <= 0) {
    cfg.status_led.red_pin = kDefaultStatusLedRedPin;
  }
  if (cfg.status_led.green_pin <= 0) {
    cfg.status_led.green_pin = kDefaultStatusLedGreenPin;
  }
  if (cfg.status_led.blue_pin <= 0) {
    cfg.status_led.blue_pin = kDefaultStatusLedBluePin;
  }
  cfg.status_led.yellow_pin = 0;

  if (cfg.buzzer.gpio_pin <= 0) {
    cfg.buzzer.gpio_pin = kDefaultBuzzerPin;
  }

  if (cfg.door_contact.gpio_pin <= 0) {
    cfg.door_contact.gpio_pin = kDefaultDoorContactPin;
  }
  if (cfg.door_contact.debounce_ms <= 0) {
    cfg.door_contact.debounce_ms = kDefaultDoorDebounceMs;
  }
  if (cfg.door_contact.held_open_after_ms <= 0) {
    cfg.door_contact.held_open_after_ms = kDefaultDoorHeldOpenAfterMs;
  }

  if (cfg.exit_button.gpio_pin <= 0) {
    cfg.exit_button.gpio_pin = kDefaultExitButtonPin;
  }
  if (cfg.exit_button.debounce_ms <= 0) {
    cfg.exit_button.debounce_ms = kDefaultExitDebounceMs;
  }
  if (cfg.exit_button.cooldown_ms <= 0) {
    cfg.exit_button.cooldown_ms = kDefaultExitCooldownMs;
  }

  cfg.mdns.hostname = SanitizeHostname(cfg.mdns.hostname);

  if (cfg.contingency.online_redeem_timeout_ms <= 0) {
    cfg.contingency.online_redeem_timeout_ms = kDefaultOnlineRedeemTimeoutMs;
  }
  if (cfg.contingency.max_policy_stale_hours <= 0) {
    cfg.contingency.max_policy_stale_hours = kDefaultMaxPolicyStaleHours;
  }

  cfg.wifi.ssid = Trim(cfg.wifi.ssid);

  if (cfg.qr_reader.uart_port <= 0) {
    cfg.qr_reader.uart_port = kDefaultQrUartPort;
  }
  if (cfg.qr_reader.rx_pin <= 0) {
    cfg.qr_reader.rx_pin = kDefaultQrUartRxPin;
  }
  if (cfg.qr_reader.tx_pin <= 0) {
    cfg.qr_reader.tx_pin = kDefaultQrUartTxPin;
  }
  if (cfg.qr_reader.baud <= 0) {
    cfg.qr_reader.baud = kDefaultQrUartBaud;
  }

  if (cfg.rtc.i2c_port < 0) {
    cfg.rtc.i2c_port = kDefaultRtcI2cPort;
  }
  if (cfg.rtc.sda_pin <= 0) {
    cfg.rtc.sda_pin = kDefaultRtcSdaPin;
  }
  if (cfg.rtc.scl_pin <= 0) {
    cfg.rtc.scl_pin = kDefaultRtcSclPin;
  }

  return cfg;
}

std::string ValidateOperational(const RuntimeConfig& cfg) {
  if (!cfg.configured) {
    return "Appliance ainda não provisionado.";
  }
  if (cfg.identity_url.empty()) {
    return "Informe a URL do Identity.";
  }
  if (!StartsWith(cfg.device_key, kDeviceKeyPrefix)) {
    return "A device key deve começar com idb_.";
  }
  return "";
}

RuntimeConfig ResetToSetup(RuntimeConfig cfg) {
  cfg.configured = false;
  cfg.device_key.clear();
  cfg.device_id.clear();
  cfg.provisioned_at.clear();
  cfg.access_point_slug.clear();
  return Normalize(std::move(cfg));
}

RuntimeConfig ApplyRemoteDeviceConfig(RuntimeConfig cfg,
                                      const RemoteDeviceConfig& remote,
                                      bool* changed) {
  bool moved = false;

  if (!remote.access_point_slug.empty() && cfg.access_point_slug != remote.access_point_slug) {
    cfg.access_point_slug = remote.access_point_slug;
    moved = true;
  }
  if (cfg.emit_detection != remote.emit_detection) {
    cfg.emit_detection = remote.emit_detection;
    moved = true;
  }
  if (remote.debounce_ms > 0 && cfg.debounce_ms != remote.debounce_ms) {
    cfg.debounce_ms = remote.debounce_ms;
    moved = true;
  }
  if (cfg.unlock_on_authorized_only != remote.unlock_on_authorized_only) {
    cfg.unlock_on_authorized_only = remote.unlock_on_authorized_only;
    moved = true;
  }
  if (cfg.contingency.enabled != remote.contingency.enabled) {
    cfg.contingency.enabled = remote.contingency.enabled;
    moved = true;
  }
  if (remote.contingency.online_redeem_timeout_ms > 0 &&
      cfg.contingency.online_redeem_timeout_ms != remote.contingency.online_redeem_timeout_ms) {
    cfg.contingency.online_redeem_timeout_ms = remote.contingency.online_redeem_timeout_ms;
    moved = true;
  }
  if (remote.contingency.max_policy_stale_hours > 0 &&
      cfg.contingency.max_policy_stale_hours != remote.contingency.max_policy_stale_hours) {
    cfg.contingency.max_policy_stale_hours = remote.contingency.max_policy_stale_hours;
    moved = true;
  }

  if (changed != nullptr) {
    *changed = moved;
  }
  return Normalize(std::move(cfg));
}

}  // namespace viaaccess
