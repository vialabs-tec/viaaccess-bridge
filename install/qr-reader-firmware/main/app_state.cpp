#include "app_state.hpp"

#include <ctime>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "storage.hpp"
#include "viaaccess/time.hpp"
#include "viaaccess/version.hpp"

namespace app {
namespace {

constexpr const char* kTag = "state";

int64_t NowUnix() { return static_cast<int64_t>(std::time(nullptr)); }

int64_t UptimeSeconds(int64_t started_at) {
  if (started_at <= 0) {
    return static_cast<int64_t>(esp_timer_get_time() / 1000000);
  }
  const int64_t now = NowUnix();
  return now > started_at ? now - started_at : 0;
}

const char* WifiPhaseString(WifiPhase phase) {
  switch (phase) {
    case WifiPhase::kBooting:
      return "BOOTING";
    case WifiPhase::kProvisioning:
      return "PROVISIONING";
    case WifiPhase::kConnecting:
      return "CONNECTING";
    case WifiPhase::kConnected:
      return "CONNECTED";
  }
  return "BOOTING";
}

void AddNullableString(cJSON* parent, const char* key, const std::string& value) {
  if (value.empty()) {
    cJSON_AddNullToObject(parent, key);
    return;
  }
  cJSON_AddStringToObject(parent, key, value.c_str());
}

double RoundToTenths(double value) {
  // Rounds away from zero on both sides: the -1 sentinel used for an unknown
  // policy age survives, and a negative RTC temperature is not clamped.
  const double scaled = value < 0 ? value * 10 - 0.5 : value * 10 + 0.5;
  return static_cast<double>(static_cast<int>(scaled)) / 10;
}

std::string PrintAndDelete(cJSON* root) {
  char* printed = cJSON_PrintUnformatted(root);
  std::string out = printed != nullptr ? printed : "{}";
  cJSON_free(printed);
  cJSON_Delete(root);
  return out;
}

}  // namespace

State& State::Instance() {
  static State instance;
  return instance;
}

void State::Init(const viaaccess::RuntimeConfig& cfg) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_ = viaaccess::Normalize(cfg);
  started_at_ = NowUnix();
}

viaaccess::RuntimeConfig State::config() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_;
}

bool State::configured() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_.configured;
}

void State::set_on_became_operational(std::function<void()> hook) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_became_operational_ = std::move(hook);
}

void State::set_on_mdns_hostname_changed(
    std::function<void(const std::string&, bool)> hook) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_mdns_hostname_changed_ = std::move(hook);
}

void State::set_on_config_applied(
    std::function<void(const viaaccess::RuntimeConfig&)> hook) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_config_applied_ = std::move(hook);
}

esp_err_t State::SaveConfig(viaaccess::RuntimeConfig cfg) {
  cfg = viaaccess::Normalize(std::move(cfg));

  std::function<void()> became_operational;
  std::function<void(const std::string&, bool)> hostname_changed;
  std::function<void(const viaaccess::RuntimeConfig&)> config_applied;
  std::string new_hostname;
  bool new_mdns_enabled = false;
  bool hostname_moved = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    const esp_err_t err = storage::SaveConfig(cfg);
    if (err != ESP_OK) {
      return err;
    }

    hostname_moved = cfg.mdns.hostname != config_.mdns.hostname ||
                     cfg.mdns.enabled != config_.mdns.enabled;
    config_ = cfg;
    new_hostname = cfg.mdns.hostname;
    new_mdns_enabled = cfg.mdns.enabled;

    if (cfg.configured && !became_operational_) {
      became_operational_ = true;
      became_operational = on_became_operational_;
    }
    if (hostname_moved) {
      hostname_changed = on_mdns_hostname_changed_;
    }
    config_applied = on_config_applied_;
  }

  if (config_applied) {
    config_applied(cfg);
  }
  if (hostname_changed) {
    hostname_changed(new_hostname, new_mdns_enabled);
  }
  if (became_operational) {
    ESP_LOGI(kTag, "operational mode active, access point %s",
             cfg.access_point_slug.c_str());
    became_operational();
  }
  return ESP_OK;
}

void State::EnterSetupMode(const std::string& reason) {
  viaaccess::RuntimeConfig cfg;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!config_.configured) {
      return;
    }
    cfg = viaaccess::ResetToSetup(config_);
    const esp_err_t err = storage::SaveConfig(cfg);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "setup reset save failed: %s", esp_err_to_name(err));
      return;
    }
    config_ = cfg;
    device_config_etag_.clear();
    identity_reachable_ = false;
  }
  ESP_LOGW(kTag, "device key invalid (%s), setup mode at http://%s.local:%d/setup",
           reason.c_str(), cfg.mdns.hostname.c_str(), cfg.http_port);
}

void State::set_identity_reachable(bool reachable) {
  std::lock_guard<std::mutex> lock(mutex_);
  identity_reachable_ = reachable;
  last_identity_check_ = NowUnix();
}

bool State::identity_reachable() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return identity_reachable_;
}

void State::set_policy(const viaaccess::PolicyState& policy) {
  std::lock_guard<std::mutex> lock(mutex_);
  policy_ = viaaccess::NormalizePolicy(policy);
}

viaaccess::PolicyState State::policy() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return policy_;
}

std::string State::device_config_etag() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return device_config_etag_;
}

void State::set_device_config_etag(const std::string& etag) {
  std::lock_guard<std::mutex> lock(mutex_);
  device_config_etag_ = etag;
}

void State::set_clock(const viaaccess::ClockState& state) {
  std::lock_guard<std::mutex> lock(mutex_);
  clock_ = state;
  // started_at_ was recorded against the pre-sync counter, so uptime would read
  // as decades once the real date arrives. Rebase it on the correction.
  if (started_at_ < viaaccess::kMinPlausibleUnixTime) {
    const int64_t booted_seconds_ago = esp_timer_get_time() / 1000000;
    started_at_ = NowUnix() - booted_seconds_ago;
  }
}

void State::set_rtc_status(bool present, bool oscillator_stopped,
                           double temperature_c) {
  std::lock_guard<std::mutex> lock(mutex_);
  rtc_present_ = present;
  rtc_oscillator_stopped_ = oscillator_stopped;
  // A chip that answered but reports an implausible temperature is a wiring
  // problem, not a reading worth showing.
  rtc_temperature_valid_ = present && temperature_c > -40 && temperature_c < 90;
  rtc_temperature_c_ = temperature_c;
}

viaaccess::ClockState State::clock() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return clock_;
}

bool State::clock_trusted() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return viaaccess::ClockIsTrusted(clock_, NowUnix());
}

bool State::rtc_present() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return rtc_present_;
}

bool State::rtc_battery_lost() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return rtc_present_ && rtc_oscillator_stopped_;
}

void State::set_relay_simulated(bool simulated) {
  std::lock_guard<std::mutex> lock(mutex_);
  relay_simulated_ = simulated;
}

void State::set_wifi(WifiPhase phase, const std::string& ssid, const std::string& ip) {
  std::lock_guard<std::mutex> lock(mutex_);
  wifi_phase_ = phase;
  wifi_ssid_ = ssid;
  wifi_ip_ = ip;
}

void State::set_reader_stats(bool driver_ready, uint32_t scans, uint32_t dropped_lines) {
  std::lock_guard<std::mutex> lock(mutex_);
  reader_driver_ready_ = driver_ready;
  reader_scans_ = scans;
  reader_dropped_lines_ = dropped_lines;
}

void State::set_simulated_door_state(const std::string& state) {
  std::lock_guard<std::mutex> lock(mutex_);
  simulated_door_state_ = state;
}

void State::set_simulated_exit_state(const std::string& state) {
  std::lock_guard<std::mutex> lock(mutex_);
  simulated_exit_state_ = state;
}

viaaccess::OperationMode State::ModeLocked() const {
  viaaccess::ModeInput input;
  input.configured = config_.configured;
  input.identity_reachable = identity_reachable_;
  input.contingency_enabled = config_.contingency.enabled && kLocalContingencySupported;
  input.policy = policy_;
  input.policy.max_stale_hours = config_.contingency.max_policy_stale_hours;
  input.now = NowUnix();
  input.clock_trusted = viaaccess::ClockIsTrusted(clock_, input.now);
  return viaaccess::EvaluateOperationMode(input);
}

viaaccess::OperationMode State::operation_mode() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ModeLocked();
}

void State::RecordScan(viaaccess::ScanPath path, const viaaccess::RedeemResult& result) {
  std::lock_guard<std::mutex> lock(mutex_);
  last_scan_at_ = NowUnix();
  last_scan_path_ = path;
  last_error_.clear();
  if (result.ok) {
    last_outcome_ = result.data.correlation_outcome.empty()
                        ? "OK"
                        : result.data.correlation_outcome;
  } else {
    last_outcome_ = "ERROR";
    last_error_ = result.data.error;
  }
}

std::string State::HealthJson() const {
  std::lock_guard<std::mutex> lock(mutex_);

  const int64_t now = NowUnix();
  const viaaccess::OperationMode mode = ModeLocked();
  viaaccess::PolicyState policy = viaaccess::NormalizePolicy(policy_);
  policy.max_stale_hours = config_.contingency.max_policy_stale_hours;

  cJSON* root = cJSON_CreateObject();

  cJSON* wifi = cJSON_AddObjectToObject(root, "wifi");
  cJSON_AddStringToObject(wifi, "phase", WifiPhaseString(wifi_phase_));
  AddNullableString(wifi, "ssid", wifi_ssid_);
  AddNullableString(wifi, "ip", wifi_ip_);

  cJSON* reader = cJSON_AddObjectToObject(root, "qrReader");
  cJSON_AddBoolToObject(reader, "enabled", config_.qr_reader.enabled);
  cJSON_AddBoolToObject(reader, "driverReady", reader_driver_ready_);
  cJSON_AddNumberToObject(reader, "uartPort", config_.qr_reader.uart_port);
  cJSON_AddNumberToObject(reader, "baud", config_.qr_reader.baud);
  cJSON_AddNumberToObject(reader, "scans", reader_scans_);
  cJSON_AddNumberToObject(reader, "droppedLines", reader_dropped_lines_);

  cJSON* mdns = cJSON_AddObjectToObject(root, "mdns");
  cJSON_AddBoolToObject(mdns, "enabled", config_.mdns.enabled);
  cJSON_AddStringToObject(mdns, "hostname", config_.mdns.hostname.c_str());
  cJSON_AddNumberToObject(mdns, "port", config_.http_port);
  const std::string setup_url = "http://" + config_.mdns.hostname + ".local:" +
                                std::to_string(config_.http_port) + "/setup";
  cJSON_AddStringToObject(mdns, "url", setup_url.c_str());

  // Reported even before provisioning: during homologation this is how the
  // technician sees whether the DS3231 is wired and holding time.
  cJSON* clock_json = cJSON_AddObjectToObject(root, "clock");
  cJSON_AddStringToObject(clock_json, "source",
                          viaaccess::ClockSourceString(clock_.source));
  cJSON_AddStringToObject(clock_json, "sourceLabel",
                          viaaccess::ClockSourceLabelPt(clock_.source));
  cJSON_AddBoolToObject(clock_json, "trusted", viaaccess::ClockIsTrusted(clock_, now));
  AddNullableString(clock_json, "now", viaaccess::FormatRfc3339(now));
  AddNullableString(clock_json, "setAt", viaaccess::FormatRfc3339(clock_.set_at));

  cJSON* rtc = cJSON_AddObjectToObject(clock_json, "rtc");
  cJSON_AddBoolToObject(rtc, "configured", config_.rtc.enabled);
  cJSON_AddBoolToObject(rtc, "present", rtc_present_);
  cJSON_AddNumberToObject(rtc, "sdaPin", config_.rtc.sda_pin);
  cJSON_AddNumberToObject(rtc, "sclPin", config_.rtc.scl_pin);
  // A set stop flag means the coin cell died: the chip is there, its time is not
  // to be believed, and the cell needs replacing before the next power cut.
  cJSON_AddBoolToObject(rtc, "oscillatorStopped", rtc_oscillator_stopped_);
  if (rtc_temperature_valid_) {
    cJSON_AddNumberToObject(rtc, "temperatureC", RoundToTenths(rtc_temperature_c_));
  } else {
    cJSON_AddNullToObject(rtc, "temperatureC");
  }

  cJSON_AddStringToObject(root, "agentVersion", viaaccess::kFirmwareVersion);
  cJSON_AddStringToObject(root, "operationMode", viaaccess::ModeString(mode));
  cJSON_AddStringToObject(root, "operationModeLabel", viaaccess::ModeLabelPt(mode));
  cJSON_AddNumberToObject(root, "uptimeSec",
                          static_cast<double>(UptimeSeconds(started_at_)));

  if (!config_.configured) {
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddBoolToObject(root, "configured", false);
    cJSON_AddBoolToObject(root, "setupRequired", true);
    return PrintAndDelete(root);
  }

  cJSON_AddBoolToObject(root, "ok", viaaccess::HealthOk(mode));
  cJSON_AddBoolToObject(root, "configured", true);
  cJSON_AddBoolToObject(root, "identityReachable", identity_reachable_);
  cJSON_AddBoolToObject(root, "relaySimulated", relay_simulated_);

  // Reed switch, REX button and the RGB status LED are wired in step 5; the
  // config already carries their pins so /setup can show the factory map.
  cJSON* status_led = cJSON_AddObjectToObject(root, "statusLed");
  cJSON_AddBoolToObject(status_led, "enabled", config_.status_led.enabled);
  cJSON_AddStringToObject(status_led, "driver", "pending");

  cJSON* door = cJSON_AddObjectToObject(root, "doorContact");
  cJSON_AddBoolToObject(door, "enabled", config_.door_contact.enabled);
  cJSON_AddBoolToObject(door, "simulated", config_.door_contact.simulated);
  cJSON_AddStringToObject(door, "driver", "pending");
  AddNullableString(door, "simulatedState", simulated_door_state_);

  cJSON* exit_button = cJSON_AddObjectToObject(root, "exitButton");
  cJSON_AddBoolToObject(exit_button, "enabled", config_.exit_button.enabled);
  cJSON_AddBoolToObject(exit_button, "simulated", config_.exit_button.simulated);
  cJSON_AddStringToObject(exit_button, "driver", "pending");
  AddNullableString(exit_button, "simulatedState", simulated_exit_state_);

  cJSON* contingency = cJSON_AddObjectToObject(root, "contingency");
  cJSON_AddBoolToObject(contingency, "enabled", config_.contingency.enabled);
  cJSON_AddNumberToObject(contingency, "onlineRedeemTimeoutMs",
                          config_.contingency.online_redeem_timeout_ms);
  cJSON_AddNumberToObject(contingency, "maxPolicyStaleHours",
                          config_.contingency.max_policy_stale_hours);
  cJSON_AddStringToObject(contingency, "ticketVerify",
                          policy.ticket_verify_ready ? "ready" : "pending");
  // Offline verification itself is step 6: the snapshot is stored and reported,
  // but a scan that cannot reach Identity is refused rather than validated here.
  cJSON_AddStringToObject(contingency, "localVerify", "pending");

  cJSON* policy_sync = cJSON_AddObjectToObject(root, "policySync");
  AddNullableString(policy_sync, "syncedAt", viaaccess::FormatRfc3339(policy.synced_at));
  cJSON_AddStringToObject(policy_sync, "grantVersion", policy.grant_version.c_str());
  cJSON_AddStringToObject(policy_sync, "accessPointSlug", policy.access_point_slug.c_str());
  cJSON_AddStringToObject(policy_sync, "trustKeyId", policy.trust_key_id.c_str());
  cJSON_AddNumberToObject(policy_sync, "memberGrantCount", policy.member_grant_count);
  AddNullableString(policy_sync, "edgePolicyVersion", policy.edge_policy_version);
  cJSON_AddBoolToObject(policy_sync, "stale", !viaaccess::PolicyIsFresh(policy, now));
  cJSON_AddNumberToObject(policy_sync, "staleAgeHours",
                          RoundToTenths(viaaccess::PolicyStaleAgeHours(policy, now)));
  cJSON_AddNumberToObject(policy_sync, "maxStaleHours", policy.max_stale_hours);

  cJSON* outbox = cJSON_AddObjectToObject(root, "outbox");
  cJSON_AddNumberToObject(outbox, "pending", 0);

  if (last_identity_check_ > 0) {
    cJSON_AddStringToObject(root, "lastIdentityCheck",
                            viaaccess::FormatRfc3339(last_identity_check_).c_str());
  }
  if (last_scan_at_ > 0) {
    cJSON* last_scan = cJSON_AddObjectToObject(root, "lastScan");
    cJSON_AddStringToObject(last_scan, "at",
                            viaaccess::FormatRfc3339(last_scan_at_).c_str());
    cJSON_AddStringToObject(last_scan, "path", viaaccess::ScanPathString(last_scan_path_));
    cJSON_AddStringToObject(last_scan, "outcome", last_outcome_.c_str());
    if (!last_error_.empty()) {
      cJSON_AddStringToObject(last_scan, "error", last_error_.c_str());
    }
  }

  if (mode == viaaccess::OperationMode::kSyncStale) {
    cJSON_AddStringToObject(root, "warning",
                            "Política local desatualizada ou ausente. Passagens "
                            "bloqueadas até novo sync ou retorno da rede.");
  }
  if (mode == viaaccess::OperationMode::kContingency) {
    cJSON_AddStringToObject(root, "warning",
                            "Rede indisponível; usando contingência com último "
                            "sync. Revogações podem atrasar.");
  }

  return PrintAndDelete(root);
}

}  // namespace app
