// Runtime configuration for the ViaAccess QR Reader appliance.
//
// Mirrors internal/config of the Go agent (qr-reader-agent) field by field so
// the persisted JSON schema and the Identity device-config contract stay
// identical across the Raspberry Pi and ESP32-S3 products. Only the transport
// bound fields (Wi-Fi credentials, UART reader) are new here: the Pi is cabled
// and reads the scanner over USB HID, the S3 is Wi-Fi only and reads it over
// TTL UART.
#pragma once

#include <string>

namespace viaaccess {

inline constexpr int kDefaultHttpPort = 3710;
inline constexpr int kDefaultDebounceMs = 2000;
inline constexpr int kDefaultRelayPulseMs = 3000;
inline constexpr int kDefaultDoorDebounceMs = 50;
inline constexpr int kDefaultDoorHeldOpenAfterMs = 60000;
inline constexpr int kDefaultExitDebounceMs = 50;
inline constexpr int kDefaultExitCooldownMs = 3000;
inline constexpr int kDefaultOnlineRedeemTimeoutMs = 3000;
inline constexpr int kDefaultMaxPolicyStaleHours = 168;
inline constexpr const char* kDefaultMdnsHostname = "viaaccess-qr";
inline constexpr const char* kDefaultHttpHost = "0.0.0.0";
inline constexpr const char* kDeviceKeyPrefix = "idb_";

// Factory GPIO map for the ESP32-S3 N16R8 appliance. It replaces the Pi BCM map
// (relay 17, reed 4, REX 18, LED 22/27/23), which is unusable here: octal PSRAM
// claims GPIO 33-37 and SPI flash claims 26-32. The map also avoids the
// strapping pins (0/3/45/46), the native USB pair (19/20) and the UART0 console
// pins (43/44).
inline constexpr int kDefaultRelayPin = 10;
inline constexpr int kDefaultDoorContactPin = 11;
inline constexpr int kDefaultExitButtonPin = 12;
inline constexpr int kDefaultStatusLedRedPin = 4;
inline constexpr int kDefaultStatusLedGreenPin = 5;
inline constexpr int kDefaultStatusLedBluePin = 6;

// EP8280L in TTL mode: two GPIOs, module decodes and emits a terminated line.
inline constexpr int kDefaultQrUartPort = 1;
inline constexpr int kDefaultQrUartRxPin = 17;
inline constexpr int kDefaultQrUartTxPin = 18;
inline constexpr int kDefaultQrUartBaud = 9600;

// DS3231 on I2C. Optional hardware: without it the appliance simply refuses the
// offline path after a power cut instead of trusting an unset clock.
inline constexpr int kDefaultRtcI2cPort = 0;
inline constexpr int kDefaultRtcSdaPin = 8;
inline constexpr int kDefaultRtcSclPin = 9;

struct RelayConfig {
  bool enabled = true;
  int gpio_pin = kDefaultRelayPin;
  int pulse_ms = kDefaultRelayPulseMs;
  bool active_high = true;
};

// KY-016 RGB module (common cathode). R = stale/contingency, G = online, B = setup.
struct StatusLedConfig {
  bool enabled = true;
  int red_pin = kDefaultStatusLedRedPin;
  int green_pin = kDefaultStatusLedGreenPin;
  int blue_pin = kDefaultStatusLedBluePin;
  bool active_high = true;
  // Deprecated alias for red_pin, accepted when loading older configs.
  int yellow_pin = 0;
};

// MC38 (or similar) reed switch. active_low: closed door pulls the line LOW.
struct DoorContactConfig {
  bool enabled = true;
  int gpio_pin = kDefaultDoorContactPin;
  bool active_low = true;
  int debounce_ms = kDefaultDoorDebounceMs;
  int held_open_after_ms = kDefaultDoorHeldOpenAfterMs;
  bool simulated = false;
};

// Momentary Request-to-Exit button. On press the appliance pulses the relay
// without a QR and notifies Identity so the door-contact opened event that
// follows is not treated as forced entry.
struct ExitButtonConfig {
  bool enabled = true;
  int gpio_pin = kDefaultExitButtonPin;
  bool active_low = true;
  int debounce_ms = kDefaultExitDebounceMs;
  int cooldown_ms = kDefaultExitCooldownMs;
  bool simulated = false;
};

struct MdnsConfig {
  bool enabled = true;
  std::string hostname = kDefaultMdnsHostname;
};

struct ContingencyConfig {
  bool enabled = true;
  int online_redeem_timeout_ms = kDefaultOnlineRedeemTimeoutMs;
  int max_policy_stale_hours = kDefaultMaxPolicyStaleHours;
};

// Wi-Fi station credentials. The Pi is cabled; the S3 has no Ethernet MAC, so
// the first boot serves a SoftAP portal to collect these before setup can run.
struct WifiConfig {
  std::string ssid;
  std::string password;
};

// TTL/UART barcode module (EP8280L configured out of USB-KBW into TTL).
struct QrReaderConfig {
  bool enabled = true;
  int uart_port = kDefaultQrUartPort;
  int rx_pin = kDefaultQrUartRxPin;
  int tx_pin = kDefaultQrUartTxPin;
  int baud = kDefaultQrUartBaud;
};

// Battery-backed clock (DS3231). enabled only means "probe the bus": the driver
// reports whether a chip actually answered, and everything degrades to the
// network clock when it did not.
struct RtcConfig {
  bool enabled = true;
  int i2c_port = kDefaultRtcI2cPort;
  int sda_pin = kDefaultRtcSdaPin;
  int scl_pin = kDefaultRtcSclPin;
};

struct RuntimeConfig {
  bool configured = false;
  std::string identity_url;
  std::string device_key;
  std::string device_id;
  std::string provisioned_at;
  std::string access_point_slug;
  bool emit_detection = true;
  int debounce_ms = kDefaultDebounceMs;

  std::string http_host = kDefaultHttpHost;
  int http_port = kDefaultHttpPort;

  std::string webhook_secret;
  std::string unlock_webhook_url;
  bool unlock_on_authorized_only = true;

  RelayConfig relay;
  StatusLedConfig status_led;
  DoorContactConfig door_contact;
  ExitButtonConfig exit_button;
  MdnsConfig mdns;
  ContingencyConfig contingency;
  WifiConfig wifi;
  QrReaderConfig qr_reader;
  RtcConfig rtc;

  std::string setup_pin;
};

// Identity GET /api/bridge/device-config payload.
struct RemoteContingencyConfig {
  bool enabled = false;
  int online_redeem_timeout_ms = 0;
  int max_policy_stale_hours = 0;
};

struct RemoteDeviceConfig {
  std::string access_point_slug;
  bool enabled = false;
  bool emit_detection = false;
  int debounce_ms = 0;
  bool unlock_on_authorized_only = false;
  RemoteContingencyConfig contingency;
};

RuntimeConfig DefaultRuntimeConfig();

// Normalize trims strings and restores defaults for missing or invalid values.
// Applied on load, on save and after every remote overlay, like the Go agent.
RuntimeConfig Normalize(RuntimeConfig cfg);

// ValidateOperational returns an empty string when the appliance can serve
// scans, otherwise a technician-facing reason (Portuguese, shown in /setup).
std::string ValidateOperational(const RuntimeConfig& cfg);

// ResetToSetup clears device credentials so the appliance can be provisioned
// again after Identity revokes the key. Wi-Fi credentials survive: the
// technician should not have to rejoin the network to reprovision.
RuntimeConfig ResetToSetup(RuntimeConfig cfg);

// ApplyRemoteDeviceConfig overlays Identity device-config onto local settings.
// Sets changed when any operational field moved.
RuntimeConfig ApplyRemoteDeviceConfig(RuntimeConfig cfg,
                                      const RemoteDeviceConfig& remote,
                                      bool* changed);

}  // namespace viaaccess
