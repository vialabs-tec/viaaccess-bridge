#include "config_json.hpp"

#include "cJSON.h"
#include "viaaccess/time.hpp"

namespace config_json {
namespace {

using viaaccess::PolicyState;
using viaaccess::RemoteDeviceConfig;
using viaaccess::RuntimeConfig;

const cJSON* Object(const cJSON* parent, const char* key) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(parent, key);
  return cJSON_IsObject(item) ? item : nullptr;
}

std::string GetString(const cJSON* parent, const char* key, const std::string& fallback) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(parent, key);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return fallback;
  }
  return item->valuestring;
}

int GetInt(const cJSON* parent, const char* key, int fallback) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(parent, key);
  if (!cJSON_IsNumber(item)) {
    return fallback;
  }
  return item->valueint;
}

bool GetBool(const cJSON* parent, const char* key, bool fallback) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(parent, key);
  if (!cJSON_IsBool(item)) {
    return fallback;
  }
  return cJSON_IsTrue(item) != 0;
}

}  // namespace

RuntimeConfig Parse(const std::string& json) {
  RuntimeConfig cfg = viaaccess::DefaultRuntimeConfig();
  cJSON* root = cJSON_Parse(json.c_str());
  if (root == nullptr) {
    return viaaccess::Normalize(cfg);
  }

  cfg.configured = GetBool(root, "configured", cfg.configured);
  cfg.identity_url = GetString(root, "identityUrl", cfg.identity_url);
  cfg.device_key = GetString(root, "deviceKey", cfg.device_key);
  cfg.device_id = GetString(root, "deviceId", cfg.device_id);
  cfg.provisioned_at = GetString(root, "provisionedAt", cfg.provisioned_at);
  cfg.access_point_slug = GetString(root, "accessPointSlug", cfg.access_point_slug);
  cfg.emit_detection = GetBool(root, "emitDetection", cfg.emit_detection);
  cfg.debounce_ms = GetInt(root, "debounceMs", cfg.debounce_ms);
  cfg.http_host = GetString(root, "httpHost", cfg.http_host);
  cfg.http_port = GetInt(root, "httpPort", cfg.http_port);
  cfg.webhook_secret = GetString(root, "webhookSecret", cfg.webhook_secret);
  cfg.unlock_webhook_url = GetString(root, "unlockWebhookUrl", cfg.unlock_webhook_url);
  cfg.unlock_on_authorized_only =
      GetBool(root, "unlockOnAuthorizedOnly", cfg.unlock_on_authorized_only);
  cfg.setup_pin = GetString(root, "setupPin", cfg.setup_pin);

  if (const cJSON* relay = Object(root, "relay")) {
    cfg.relay.enabled = GetBool(relay, "enabled", cfg.relay.enabled);
    cfg.relay.gpio_pin = GetInt(relay, "gpioPin", cfg.relay.gpio_pin);
    cfg.relay.pulse_ms = GetInt(relay, "pulseMs", cfg.relay.pulse_ms);
    cfg.relay.active_high = GetBool(relay, "activeHigh", cfg.relay.active_high);
  }
  if (const cJSON* led = Object(root, "statusLed")) {
    cfg.status_led.enabled = GetBool(led, "enabled", cfg.status_led.enabled);
    cfg.status_led.red_pin = GetInt(led, "redPin", cfg.status_led.red_pin);
    cfg.status_led.green_pin = GetInt(led, "greenPin", cfg.status_led.green_pin);
    cfg.status_led.blue_pin = GetInt(led, "bluePin", cfg.status_led.blue_pin);
    cfg.status_led.active_high = GetBool(led, "activeHigh", cfg.status_led.active_high);
    cfg.status_led.yellow_pin = GetInt(led, "yellowPin", cfg.status_led.yellow_pin);
  }
  if (const cJSON* buzzer = Object(root, "buzzer")) {
    cfg.buzzer.enabled = GetBool(buzzer, "enabled", cfg.buzzer.enabled);
    cfg.buzzer.gpio_pin = GetInt(buzzer, "gpioPin", cfg.buzzer.gpio_pin);
    cfg.buzzer.active_high = GetBool(buzzer, "activeHigh", cfg.buzzer.active_high);
  }
  if (const cJSON* door = Object(root, "doorContact")) {
    cfg.door_contact.enabled = GetBool(door, "enabled", cfg.door_contact.enabled);
    cfg.door_contact.gpio_pin = GetInt(door, "gpioPin", cfg.door_contact.gpio_pin);
    cfg.door_contact.active_low = GetBool(door, "activeLow", cfg.door_contact.active_low);
    cfg.door_contact.debounce_ms = GetInt(door, "debounceMs", cfg.door_contact.debounce_ms);
    cfg.door_contact.held_open_after_ms =
        GetInt(door, "heldOpenAfterMs", cfg.door_contact.held_open_after_ms);
    cfg.door_contact.simulated = GetBool(door, "simulated", cfg.door_contact.simulated);
  }
  if (const cJSON* exit_button = Object(root, "exitButton")) {
    cfg.exit_button.enabled = GetBool(exit_button, "enabled", cfg.exit_button.enabled);
    cfg.exit_button.gpio_pin = GetInt(exit_button, "gpioPin", cfg.exit_button.gpio_pin);
    cfg.exit_button.active_low =
        GetBool(exit_button, "activeLow", cfg.exit_button.active_low);
    cfg.exit_button.debounce_ms =
        GetInt(exit_button, "debounceMs", cfg.exit_button.debounce_ms);
    cfg.exit_button.cooldown_ms =
        GetInt(exit_button, "cooldownMs", cfg.exit_button.cooldown_ms);
    cfg.exit_button.simulated = GetBool(exit_button, "simulated", cfg.exit_button.simulated);
  }
  if (const cJSON* mdns = Object(root, "mdns")) {
    cfg.mdns.enabled = GetBool(mdns, "enabled", cfg.mdns.enabled);
    cfg.mdns.hostname = GetString(mdns, "hostname", cfg.mdns.hostname);
  }
  if (const cJSON* contingency = Object(root, "contingency")) {
    cfg.contingency.enabled = GetBool(contingency, "enabled", cfg.contingency.enabled);
    cfg.contingency.online_redeem_timeout_ms =
        GetInt(contingency, "onlineRedeemTimeoutMs", cfg.contingency.online_redeem_timeout_ms);
    cfg.contingency.max_policy_stale_hours =
        GetInt(contingency, "maxPolicyStaleHours", cfg.contingency.max_policy_stale_hours);
  }
  if (const cJSON* wifi = Object(root, "wifi")) {
    cfg.wifi.ssid = GetString(wifi, "ssid", cfg.wifi.ssid);
    cfg.wifi.password = GetString(wifi, "password", cfg.wifi.password);
  }
  if (const cJSON* reader = Object(root, "qrReader")) {
    cfg.qr_reader.enabled = GetBool(reader, "enabled", cfg.qr_reader.enabled);
    cfg.qr_reader.uart_port = GetInt(reader, "uartPort", cfg.qr_reader.uart_port);
    cfg.qr_reader.rx_pin = GetInt(reader, "rxPin", cfg.qr_reader.rx_pin);
    cfg.qr_reader.tx_pin = GetInt(reader, "txPin", cfg.qr_reader.tx_pin);
    cfg.qr_reader.baud = GetInt(reader, "baud", cfg.qr_reader.baud);
  }
  if (const cJSON* rtc = Object(root, "rtc")) {
    cfg.rtc.enabled = GetBool(rtc, "enabled", cfg.rtc.enabled);
    cfg.rtc.i2c_port = GetInt(rtc, "i2cPort", cfg.rtc.i2c_port);
    cfg.rtc.sda_pin = GetInt(rtc, "sdaPin", cfg.rtc.sda_pin);
    cfg.rtc.scl_pin = GetInt(rtc, "sclPin", cfg.rtc.scl_pin);
  }
  if (const cJSON* beacon = Object(root, "bleBeacon")) {
    cfg.ble_beacon.enabled = GetBool(beacon, "enabled", cfg.ble_beacon.enabled);
    cfg.ble_beacon.uuid = GetString(beacon, "uuid", cfg.ble_beacon.uuid);
    cfg.ble_beacon.major = GetInt(beacon, "major", cfg.ble_beacon.major);
    cfg.ble_beacon.minor = GetInt(beacon, "minor", cfg.ble_beacon.minor);
    cfg.ble_beacon.tx_power =
        GetInt(beacon, "txPower", cfg.ble_beacon.tx_power);
  }

  cJSON_Delete(root);
  return viaaccess::Normalize(cfg);
}

std::string Serialize(const RuntimeConfig& cfg, bool include_secrets) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "configured", cfg.configured);
  cJSON_AddStringToObject(root, "identityUrl", cfg.identity_url.c_str());
  if (include_secrets) {
    cJSON_AddStringToObject(root, "deviceKey", cfg.device_key.c_str());
  }
  cJSON_AddStringToObject(root, "deviceId", cfg.device_id.c_str());
  cJSON_AddStringToObject(root, "provisionedAt", cfg.provisioned_at.c_str());
  cJSON_AddStringToObject(root, "accessPointSlug", cfg.access_point_slug.c_str());
  cJSON_AddBoolToObject(root, "emitDetection", cfg.emit_detection);
  cJSON_AddNumberToObject(root, "debounceMs", cfg.debounce_ms);
  cJSON_AddStringToObject(root, "httpHost", cfg.http_host.c_str());
  cJSON_AddNumberToObject(root, "httpPort", cfg.http_port);
  cJSON_AddStringToObject(root, "webhookSecret", cfg.webhook_secret.c_str());
  cJSON_AddStringToObject(root, "unlockWebhookUrl", cfg.unlock_webhook_url.c_str());
  cJSON_AddBoolToObject(root, "unlockOnAuthorizedOnly", cfg.unlock_on_authorized_only);
  cJSON_AddStringToObject(root, "setupPin", cfg.setup_pin.c_str());

  cJSON* relay = cJSON_AddObjectToObject(root, "relay");
  cJSON_AddBoolToObject(relay, "enabled", cfg.relay.enabled);
  cJSON_AddNumberToObject(relay, "gpioPin", cfg.relay.gpio_pin);
  cJSON_AddNumberToObject(relay, "pulseMs", cfg.relay.pulse_ms);
  cJSON_AddBoolToObject(relay, "activeHigh", cfg.relay.active_high);

  cJSON* led = cJSON_AddObjectToObject(root, "statusLed");
  cJSON_AddBoolToObject(led, "enabled", cfg.status_led.enabled);
  cJSON_AddNumberToObject(led, "redPin", cfg.status_led.red_pin);
  cJSON_AddNumberToObject(led, "greenPin", cfg.status_led.green_pin);
  cJSON_AddNumberToObject(led, "bluePin", cfg.status_led.blue_pin);
  cJSON_AddBoolToObject(led, "activeHigh", cfg.status_led.active_high);

  cJSON* buzzer = cJSON_AddObjectToObject(root, "buzzer");
  cJSON_AddBoolToObject(buzzer, "enabled", cfg.buzzer.enabled);
  cJSON_AddNumberToObject(buzzer, "gpioPin", cfg.buzzer.gpio_pin);
  cJSON_AddBoolToObject(buzzer, "activeHigh", cfg.buzzer.active_high);

  cJSON* door = cJSON_AddObjectToObject(root, "doorContact");
  cJSON_AddBoolToObject(door, "enabled", cfg.door_contact.enabled);
  cJSON_AddNumberToObject(door, "gpioPin", cfg.door_contact.gpio_pin);
  cJSON_AddBoolToObject(door, "activeLow", cfg.door_contact.active_low);
  cJSON_AddNumberToObject(door, "debounceMs", cfg.door_contact.debounce_ms);
  cJSON_AddNumberToObject(door, "heldOpenAfterMs", cfg.door_contact.held_open_after_ms);
  cJSON_AddBoolToObject(door, "simulated", cfg.door_contact.simulated);

  cJSON* exit_button = cJSON_AddObjectToObject(root, "exitButton");
  cJSON_AddBoolToObject(exit_button, "enabled", cfg.exit_button.enabled);
  cJSON_AddNumberToObject(exit_button, "gpioPin", cfg.exit_button.gpio_pin);
  cJSON_AddBoolToObject(exit_button, "activeLow", cfg.exit_button.active_low);
  cJSON_AddNumberToObject(exit_button, "debounceMs", cfg.exit_button.debounce_ms);
  cJSON_AddNumberToObject(exit_button, "cooldownMs", cfg.exit_button.cooldown_ms);
  cJSON_AddBoolToObject(exit_button, "simulated", cfg.exit_button.simulated);

  cJSON* mdns = cJSON_AddObjectToObject(root, "mdns");
  cJSON_AddBoolToObject(mdns, "enabled", cfg.mdns.enabled);
  cJSON_AddStringToObject(mdns, "hostname", cfg.mdns.hostname.c_str());

  cJSON* contingency = cJSON_AddObjectToObject(root, "contingency");
  cJSON_AddBoolToObject(contingency, "enabled", cfg.contingency.enabled);
  cJSON_AddNumberToObject(contingency, "onlineRedeemTimeoutMs",
                          cfg.contingency.online_redeem_timeout_ms);
  cJSON_AddNumberToObject(contingency, "maxPolicyStaleHours",
                          cfg.contingency.max_policy_stale_hours);

  cJSON* wifi = cJSON_AddObjectToObject(root, "wifi");
  cJSON_AddStringToObject(wifi, "ssid", cfg.wifi.ssid.c_str());
  if (include_secrets) {
    cJSON_AddStringToObject(wifi, "password", cfg.wifi.password.c_str());
  }

  cJSON* reader = cJSON_AddObjectToObject(root, "qrReader");
  cJSON_AddBoolToObject(reader, "enabled", cfg.qr_reader.enabled);
  cJSON_AddNumberToObject(reader, "uartPort", cfg.qr_reader.uart_port);
  cJSON_AddNumberToObject(reader, "rxPin", cfg.qr_reader.rx_pin);
  cJSON_AddNumberToObject(reader, "txPin", cfg.qr_reader.tx_pin);
  cJSON_AddNumberToObject(reader, "baud", cfg.qr_reader.baud);

  cJSON* rtc = cJSON_AddObjectToObject(root, "rtc");
  cJSON_AddBoolToObject(rtc, "enabled", cfg.rtc.enabled);
  cJSON_AddNumberToObject(rtc, "i2cPort", cfg.rtc.i2c_port);
  cJSON_AddNumberToObject(rtc, "sdaPin", cfg.rtc.sda_pin);
  cJSON_AddNumberToObject(rtc, "sclPin", cfg.rtc.scl_pin);

  cJSON* beacon = cJSON_AddObjectToObject(root, "bleBeacon");
  cJSON_AddBoolToObject(beacon, "enabled", cfg.ble_beacon.enabled);
  cJSON_AddStringToObject(beacon, "uuid", cfg.ble_beacon.uuid.c_str());
  cJSON_AddNumberToObject(beacon, "major", cfg.ble_beacon.major);
  cJSON_AddNumberToObject(beacon, "minor", cfg.ble_beacon.minor);
  cJSON_AddNumberToObject(beacon, "txPower", cfg.ble_beacon.tx_power);

  char* printed = cJSON_PrintUnformatted(root);
  std::string out = printed != nullptr ? printed : "{}";
  cJSON_free(printed);
  cJSON_Delete(root);
  return out;
}

bool ParseRemoteDeviceConfig(const std::string& json, RemoteDeviceConfig* out) {
  cJSON* root = cJSON_Parse(json.c_str());
  if (root == nullptr) {
    return false;
  }
  out->access_point_slug = GetString(root, "accessPointSlug", "");
  out->enabled = GetBool(root, "enabled", false);
  out->emit_detection = GetBool(root, "emitDetection", false);
  out->debounce_ms = GetInt(root, "debounceMs", 0);
  out->unlock_on_authorized_only = GetBool(root, "unlockOnAuthorizedOnly", false);
  if (const cJSON* contingency = Object(root, "contingency")) {
    out->contingency.enabled = GetBool(contingency, "enabled", false);
    out->contingency.online_redeem_timeout_ms =
        GetInt(contingency, "onlineRedeemTimeoutMs", 0);
    out->contingency.max_policy_stale_hours = GetInt(contingency, "maxPolicyStaleHours", 0);
  }
  out->ble_beacon = {};
  if (const cJSON* beacon = Object(root, "bleBeacon")) {
    out->ble_beacon.present = true;
    out->ble_beacon.enabled = GetBool(beacon, "enabled", false);
    out->ble_beacon.uuid = GetString(beacon, "uuid", "");
    out->ble_beacon.major = GetInt(beacon, "major", 0);
    out->ble_beacon.minor = GetInt(beacon, "minor", 0);
    out->ble_beacon.tx_power =
        GetInt(beacon, "txPower", viaaccess::kDefaultBleBeaconTxPower);
  }
  cJSON_Delete(root);
  return true;
}

bool ParsePolicySnapshot(const std::string& json, PolicyState* out) {
  cJSON* root = cJSON_Parse(json.c_str());
  if (root == nullptr) {
    return false;
  }
  out->synced_at = viaaccess::ParseRfc3339(GetString(root, "syncedAt", ""));
  out->grant_version = GetString(root, "grantVersion", "");
  out->access_point_slug = GetString(root, "accessPointSlug", "");
  out->trust_key_id = GetString(root, "trustKeyId", "");
  out->member_grant_count = GetInt(root, "memberGrantCount", 0);
  out->max_stale_hours = GetInt(root, "maxStaleHours", 0);
  out->member_ids.clear();
  out->ticket_verify = {};
  out->edge_policy_version.clear();
  out->after_hours = {};

  if (const cJSON* members = cJSON_GetObjectItemCaseSensitive(root, "memberIds")) {
    if (cJSON_IsArray(members)) {
      const cJSON* item = nullptr;
      cJSON_ArrayForEach(item, members) {
        if (cJSON_IsString(item) && item->valuestring != nullptr &&
            item->valuestring[0] != '\0') {
          out->member_ids.emplace_back(item->valuestring);
        }
      }
    }
  }
  if (out->member_grant_count <= 0 && !out->member_ids.empty()) {
    out->member_grant_count = static_cast<int>(out->member_ids.size());
  }

  if (const cJSON* ticket = Object(root, "ticketVerify")) {
    out->ticket_verify.alg = GetString(ticket, "alg", "");
    out->ticket_verify.key_b64 = GetString(ticket, "keyB64", "");
    out->ticket_verify.issuer = GetString(ticket, "issuer", "");
    out->ticket_verify_ready = out->ticket_verify.alg == "HS256" &&
                               !out->ticket_verify.key_b64.empty() &&
                               !out->ticket_verify.issuer.empty();
  } else {
    out->ticket_verify_ready = false;
  }
  if (const cJSON* edge = Object(root, "edgePolicy")) {
    out->edge_policy_version = GetString(edge, "version", "");
    if (const cJSON* rules = Object(edge, "rules")) {
      if (const cJSON* after_hours = Object(rules, "after_hours")) {
        out->after_hours.enabled = GetBool(after_hours, "enabled", false);
        if (const cJSON* params = Object(after_hours, "params")) {
          out->after_hours.after_time = GetString(params, "afterTime", "");
          out->after_hours.before_time = GetString(params, "beforeTime", "");
          out->after_hours.timezone = GetString(params, "timezone", "");
        }
      }
    }
  }

  cJSON_Delete(root);
  *out = viaaccess::NormalizePolicy(*out);
  return true;
}

}  // namespace config_json
