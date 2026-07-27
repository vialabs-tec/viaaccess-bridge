// Single owner of the runtime configuration and of everything /health reports.
//
// Replaces internal/agent/state.go plus the config ownership half of
// internal/server/app.go from the Go agent. Every task (HTTP, sync, commands,
// UART reader) reads and writes through this object under one mutex.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "esp_err.h"
#include "viaaccess/clock.hpp"
#include "viaaccess/config.hpp"
#include "viaaccess/mode.hpp"
#include "viaaccess/redeem.hpp"

namespace app {

// Offline validation (HMAC ticket verify, nonce store, outbox) is step 6 of the
// port. Until it lands the appliance must never authorize a passage from the
// local snapshot, so contingency is not offered as an operating mode: with
// Identity unreachable the posture is SYNC_STALE and passage is refused, exactly
// how the Go agent behaves when contingency is disabled. Flipping this to true
// without implementing the verification would let any well-formed QR through.
inline constexpr bool kLocalContingencySupported = false;

enum class WifiPhase {
  kBooting,
  // SoftAP portal, waiting for the technician to pick a network.
  kProvisioning,
  kConnecting,
  kConnected,
};

class State {
 public:
  static State& Instance();

  void Init(const viaaccess::RuntimeConfig& cfg);

  viaaccess::RuntimeConfig config() const;
  bool configured() const;

  // SaveConfig persists and republishes the config, then fires the hooks below.
  // This is the only path that flips the appliance into operational mode.
  esp_err_t SaveConfig(viaaccess::RuntimeConfig cfg);

  // EnterSetupMode clears the credentials after Identity rejects the device key
  // (401 or BRIDGE_DISABLED) and reopens /setup without a reboot.
  void EnterSetupMode(const std::string& reason);

  // OnBecameOperational runs once, the first time the appliance is provisioned
  // or on boot when it already was; used to start the background workers.
  void set_on_became_operational(std::function<void()> hook);
  void set_on_mdns_hostname_changed(std::function<void(const std::string&, bool)> hook);
  void set_on_config_applied(std::function<void(const viaaccess::RuntimeConfig&)> hook);

  void set_identity_reachable(bool reachable);
  bool identity_reachable() const;

  void set_policy(const viaaccess::PolicyState& policy);
  viaaccess::PolicyState policy() const;

  std::string device_config_etag() const;
  void set_device_config_etag(const std::string& etag);

  // Clock ownership lives in clock_service; State only publishes what it found so
  // /health and the operation mode read the same trust level.
  void set_clock(const viaaccess::ClockState& state);
  void set_rtc_status(bool present, bool oscillator_stopped, double temperature_c);
  viaaccess::ClockState clock() const;
  bool clock_trusted() const;
  bool rtc_present() const;
  // rtc_battery_lost is the DS3231 oscillator stop flag: the chip answers, but it
  // lost power at some point, so its time is not to be believed.
  bool rtc_battery_lost() const;

  void set_relay_simulated(bool simulated);
  void set_wifi(WifiPhase phase, const std::string& ssid, const std::string& ip);
  void set_reader_stats(bool driver_ready, uint32_t scans, uint32_t dropped_lines);
  // Door contact publishes its own snapshot; HealthJson must not call back into
  // the driver (the watcher already holds that mutex when it updates State).
  void set_door_contact(bool enabled, bool simulated, bool ready, int gpio_pin,
                        const std::string& state);
  void set_simulated_door_state(const std::string& state);
  void set_simulated_exit_state(const std::string& state);

  viaaccess::OperationMode operation_mode() const;

  void RecordScan(viaaccess::ScanPath path, const viaaccess::RedeemResult& result);

  // HealthJson renders the whole GET /health body.
  std::string HealthJson() const;

 private:
  State() = default;

  viaaccess::OperationMode ModeLocked() const;

  mutable std::mutex mutex_;
  viaaccess::RuntimeConfig config_;
  viaaccess::PolicyState policy_;
  std::string device_config_etag_;

  bool identity_reachable_ = false;
  int64_t last_identity_check_ = 0;
  int64_t started_at_ = 0;
  bool relay_simulated_ = false;

  viaaccess::ClockState clock_;
  bool rtc_present_ = false;
  bool rtc_oscillator_stopped_ = false;
  bool rtc_temperature_valid_ = false;
  double rtc_temperature_c_ = 0;

  WifiPhase wifi_phase_ = WifiPhase::kBooting;
  std::string wifi_ssid_;
  std::string wifi_ip_;

  bool reader_driver_ready_ = false;
  uint32_t reader_scans_ = 0;
  uint32_t reader_dropped_lines_ = 0;

  bool door_enabled_ = false;
  bool door_simulated_ = false;
  bool door_ready_ = false;
  int door_gpio_pin_ = 0;
  std::string door_state_;

  std::string simulated_door_state_;
  std::string simulated_exit_state_;

  int64_t last_scan_at_ = 0;
  viaaccess::ScanPath last_scan_path_ = viaaccess::ScanPath::kBlocked;
  std::string last_outcome_;
  std::string last_error_;

  bool became_operational_ = false;
  std::function<void()> on_became_operational_;
  std::function<void(const std::string&, bool)> on_mdns_hostname_changed_;
  std::function<void(const viaaccess::RuntimeConfig&)> on_config_applied_;
};

}  // namespace app
