#include "http_server.hpp"

#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#include "app_state.hpp"
#include "buzzer.hpp"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "identity_client.hpp"
#include "relay.hpp"
#include "door_contact.hpp"
#include "exit_button.hpp"
#include "scan_service.hpp"
#include "viaaccess/clock.hpp"
#include "viaaccess/config.hpp"
#include "viaaccess/hostname.hpp"
#include "viaaccess/provision.hpp"
#include "viaaccess/strings.hpp"
#include "viaaccess/time.hpp"
#include "viaaccess/version.hpp"
#include "wifi_manager.hpp"

extern const uint8_t setup_html_start[] asm("_binary_setup_html_start");
extern const uint8_t setup_html_end[] asm("_binary_setup_html_end");
extern const uint8_t wifi_html_start[] asm("_binary_wifi_html_start");
extern const uint8_t wifi_html_end[] asm("_binary_wifi_html_end");
extern const uint8_t servercert_pem_start[] asm("_binary_servercert_pem_start");
extern const uint8_t servercert_pem_end[] asm("_binary_servercert_pem_end");
extern const uint8_t prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
extern const uint8_t prvtkey_pem_end[] asm("_binary_prvtkey_pem_end");

namespace http_server {
namespace {

constexpr const char* kTag = "http";
// SoftAP phones need cleartext HTTP; self-signed HTTPS on SoftAP is blocked by
// many mobile browsers. HTTPS stays available on 443 for LAN / curl -k.
constexpr int kHttpsPort = 443;
constexpr int kCaptiveHttpPort = 80;

// A QR URL is a few hundred bytes and the setup form a couple of kilobytes;
// anything larger is a client mistake and must not be buffered on the heap.
constexpr size_t kMaxBodyBytes = 8192;

httpd_handle_t g_http_server = nullptr;
httpd_handle_t g_https_server = nullptr;
httpd_handle_t g_captive_http = nullptr;
int g_port = viaaccess::kDefaultHttpPort;
int g_pending_port = 0;
esp_timer_handle_t g_restart_timer = nullptr;

const char* StatusLine(int status) {
  switch (status) {
    case 200:
      return "200 OK";
    case 302:
      return "302 Found";
    case 400:
      return "400 Bad Request";
    case 401:
      return "401 Unauthorized";
    case 403:
      return "403 Forbidden";
    case 404:
      return "404 Not Found";
    case 409:
      return "409 Conflict";
    case 429:
      return "429 Too Many Requests";
    case 500:
      return "500 Internal Server Error";
    case 502:
      return "502 Bad Gateway";
    case 503:
      return "503 Service Unavailable";
    default:
      return "200 OK";
  }
}

esp_err_t SendJson(httpd_req_t* req, int status, const std::string& body) {
  httpd_resp_set_status(req, StatusLine(status));
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, body.data(), body.size());
}

std::string PrintAndDelete(cJSON* root) {
  char* printed = cJSON_PrintUnformatted(root);
  std::string out = printed != nullptr ? printed : "{}";
  cJSON_free(printed);
  cJSON_Delete(root);
  return out;
}

esp_err_t SendError(httpd_req_t* req, int status, const std::string& message,
                    const std::string& code = "") {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", false);
  cJSON_AddStringToObject(root, "error", message.c_str());
  if (!code.empty()) {
    cJSON_AddStringToObject(root, "code", code.c_str());
  }
  return SendJson(req, status, PrintAndDelete(root));
}

// ReadBody drains the request into memory, refusing oversized payloads instead
// of letting a bad client exhaust the heap the TLS sessions also live in.
bool ReadBody(httpd_req_t* req, std::string* out) {
  out->clear();
  const size_t total = req->content_len;
  if (total == 0) {
    return true;
  }
  if (total > kMaxBodyBytes) {
    return false;
  }
  out->resize(total);
  size_t received = 0;
  while (received < total) {
    const int chunk = httpd_req_recv(req, out->data() + received, total - received);
    if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }
    if (chunk <= 0) {
      return false;
    }
    received += static_cast<size_t>(chunk);
  }
  return true;
}

std::string HeaderValue(httpd_req_t* req, const char* name) {
  const size_t length = httpd_req_get_hdr_value_len(req, name);
  if (length == 0) {
    return "";
  }
  std::string value(length + 1, '\0');
  if (httpd_req_get_hdr_value_str(req, name, value.data(), length + 1) != ESP_OK) {
    return "";
  }
  value.resize(length);
  return value;
}

// --- JSON field readers, mirroring the pointer fields of the Go SaveRequest ----

std::string JsonString(const cJSON* root, const char* key) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
  if (cJSON_IsString(item) && item->valuestring != nullptr) {
    return viaaccess::Trim(item->valuestring);
  }
  return "";
}

bool JsonBool(const cJSON* root, const char* key, bool* value) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsBool(item)) {
    return false;
  }
  *value = cJSON_IsTrue(item);
  return true;
}

bool JsonInt(const cJSON* root, const char* key, int* value) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsNumber(item)) {
    return false;
  }
  *value = item->valueint;
  return true;
}

bool IsValidSetupPinFormat(const std::string& pin) {
  if (pin.size() < 4 || pin.size() > 8) {
    return false;
  }
  for (const char ch : pin) {
    if (ch < '0' || ch > '9') {
      return false;
    }
  }
  return true;
}

// Local setup writes after provision require the setup PIN. Unprovisioned
// appliances stay open for first Wi‑Fi + claim. Legacy units that are already
// configured without a PIN may establish one via body.setupPin.
// Five wrong PINs pause further guesses for kPinLockoutSec (SoftAP window is
// ~10 min, so this is what stops a radio-range brute force).
constexpr int kPinMaxFailures = 5;
constexpr int kPinLockoutSec = 60;

std::mutex g_pin_mutex;
int g_pin_failures = 0;
int64_t g_pin_locked_until_us = 0;

int PinLockRemainingSecLocked() {
  const int64_t now = esp_timer_get_time();
  if (now >= g_pin_locked_until_us) {
    return 0;
  }
  return static_cast<int>((g_pin_locked_until_us - now + 999999) / 1000000);
}

int PinLockRemainingSec() {
  std::lock_guard<std::mutex> lock(g_pin_mutex);
  return PinLockRemainingSecLocked();
}

int PinAttemptsRemaining() {
  std::lock_guard<std::mutex> lock(g_pin_mutex);
  if (PinLockRemainingSecLocked() > 0) {
    return 0;
  }
  return kPinMaxFailures - g_pin_failures;
}

void NotePinSuccess() {
  std::lock_guard<std::mutex> lock(g_pin_mutex);
  g_pin_failures = 0;
  g_pin_locked_until_us = 0;
}

struct SetupWriteAuth {
  bool ok = false;
  int status = 400;
  std::string error;
  /** Non-empty when the request establishes a new PIN (legacy / first set). */
  std::string establish_pin;
};

SetupWriteAuth AuthorizeSetupWrite(const cJSON* root) {
  SetupWriteAuth auth;
  const viaaccess::RuntimeConfig& cfg = app::State::Instance().config();
  const std::string expected = viaaccess::Trim(cfg.setup_pin);
  const std::string pin = JsonString(root, "pin");
  const std::string setup_pin = viaaccess::Trim(JsonString(root, "setupPin"));

  if (!cfg.configured) {
    auth.ok = true;
    return auth;
  }

  if (!expected.empty()) {
    std::lock_guard<std::mutex> lock(g_pin_mutex);
    const int locked_sec = PinLockRemainingSecLocked();
    if (locked_sec > 0) {
      auth.status = 429;
      auth.error = "Muitas tentativas de PIN. Aguarde " + std::to_string(locked_sec) + " s.";
      return auth;
    }
    if (pin == expected) {
      g_pin_failures = 0;
      g_pin_locked_until_us = 0;
      auth.ok = true;
      return auth;
    }
    g_pin_failures++;
    if (g_pin_failures >= kPinMaxFailures) {
      g_pin_failures = 0;
      g_pin_locked_until_us = esp_timer_get_time() +
                              static_cast<int64_t>(kPinLockoutSec) * 1000000;
      auth.status = 429;
      auth.error = "Muitas tentativas de PIN. Aguarde " + std::to_string(kPinLockoutSec) + " s.";
      return auth;
    }
    const int left = kPinMaxFailures - g_pin_failures;
    auth.status = 401;
    auth.error = left == 1 ? "PIN inválido. Resta 1 tentativa."
                           : "PIN inválido. Restam " + std::to_string(left) + " tentativas.";
    return auth;
  }

  // Provisioned but no PIN yet: require establishing one on this write.
  if (!IsValidSetupPinFormat(setup_pin)) {
    auth.error = "Defina um PIN de setup numérico (4 a 8 dígitos) em setupPin.";
    return auth;
  }
  NotePinSuccess();
  auth.ok = true;
  auth.establish_pin = setup_pin;
  return auth;
}

void ApplyEstablishedPin(viaaccess::RuntimeConfig* cfg, const SetupWriteAuth& auth) {
  if (cfg != nullptr && !auth.establish_pin.empty()) {
    cfg->setup_pin = auth.establish_pin;
  }
}

// After provision, mutating /api/setup* need SoftAP portal + Host 192.168.4.1.
// Returns true and sends 403 when the channel is blocked.
bool RejectIfSetupWritesBlocked(httpd_req_t* req) {
  const bool configured = app::State::Instance().config().configured;
  const std::string host = HeaderValue(req, "Host");
  if (wifi::local_setup_writes_allowed(configured, host, httpd_req_to_sockfd(req))) {
    return false;
  }
  SendError(req, 403,
            "Alterações locais só via SoftAP. Três cliques no BOOT, conecte em "
            "viaaccess-setup; o login da rede deve abrir o setup. Se não abrir, "
            "use http://192.168.4.1/setup (não use .local).");
  return true;
}

// ApplyHardwareOverrides mirrors applyDoorContactFromRequest and its siblings:
// absent fields keep the factory value, present ones win. Returns true when the
// form actually carried wiring, which suppresses the preserve step below.
bool ApplyHardwareOverrides(const cJSON* root, viaaccess::RuntimeConfig* cfg) {
  bool touched = false;
  bool flag = false;
  int number = 0;

  if (JsonBool(root, "relayEnabled", &flag)) {
    cfg->relay.enabled = flag;
    touched = true;
  }
  if (JsonInt(root, "relayGpioPin", &number) && number > 0) {
    cfg->relay.gpio_pin = number;
    touched = true;
  }
  {
    const std::string mode = JsonString(root, "relayUnlockMode");
    if (!mode.empty()) {
      cfg->relay.unlock_mode = viaaccess::NormalizeRelayUnlockMode(mode);
      touched = true;
    }
  }
  if (JsonInt(root, "relayPulseMs", &number) && number > 0) {
    cfg->relay.pulse_ms = number;
    touched = true;
  }
  if (JsonBool(root, "relayActiveHigh", &flag)) {
    cfg->relay.active_high = flag;
    touched = true;
  }
  if (JsonBool(root, "doorContactEnabled", &flag)) {
    cfg->door_contact.enabled = flag;
    touched = true;
  }
  if (JsonInt(root, "doorContactGpioPin", &number) && number > 0) {
    cfg->door_contact.gpio_pin = number;
    touched = true;
  }
  if (JsonBool(root, "doorContactSimulated", &flag)) {
    cfg->door_contact.simulated = flag;
    touched = true;
  }
  if (JsonBool(root, "exitButtonEnabled", &flag)) {
    cfg->exit_button.enabled = flag;
    touched = true;
  }
  if (JsonInt(root, "exitButtonGpioPin", &number) && number > 0) {
    cfg->exit_button.gpio_pin = number;
    touched = true;
  }
  if (JsonBool(root, "exitButtonSimulated", &flag)) {
    cfg->exit_button.simulated = flag;
    touched = true;
  }
  if (JsonBool(root, "buzzerEnabled", &flag)) {
    cfg->buzzer.enabled = flag;
    touched = true;
  }
  if (JsonInt(root, "buzzerGpioPin", &number) && number > 0) {
    cfg->buzzer.gpio_pin = number;
    touched = true;
  }
  if (JsonBool(root, "buzzerActiveHigh", &flag)) {
    cfg->buzzer.active_high = flag;
    touched = true;
  }
  if (JsonBool(root, "statusLedEnabled", &flag)) {
    cfg->status_led.enabled = flag;
    touched = true;
  }
  {
    const cJSON* driver = cJSON_GetObjectItemCaseSensitive(root, "statusLedDriver");
    if (cJSON_IsString(driver) && driver->valuestring != nullptr) {
      cfg->status_led.driver = driver->valuestring;
      touched = true;
    }
  }
  if (JsonInt(root, "statusLedWs2812Pin", &number) && number > 0) {
    cfg->status_led.ws2812_pin = number;
    touched = true;
  }
  if (JsonInt(root, "statusLedBrightness", &number) && number > 0) {
    cfg->status_led.brightness = number;
    touched = true;
  }
  if (JsonInt(root, "statusLedRedPin", &number) && number > 0) {
    cfg->status_led.red_pin = number;
    touched = true;
  }
  if (JsonInt(root, "statusLedGreenPin", &number) && number > 0) {
    cfg->status_led.green_pin = number;
    touched = true;
  }
  if (JsonInt(root, "statusLedBluePin", &number) && number > 0) {
    cfg->status_led.blue_pin = number;
    touched = true;
  }
  if (JsonBool(root, "statusLedActiveHigh", &flag)) {
    cfg->status_led.active_high = flag;
    touched = true;
  }
  return touched;
}

// PreserveLocalHardware is the in-memory version of preserveLocalHardware: a
// reprovision must not silently move the relay back to the factory pin when the
// installer already wired something else.
void PreserveLocalHardware(viaaccess::RuntimeConfig* cfg,
                           const viaaccess::RuntimeConfig& existing,
                           bool hardware_from_request) {
  if (hardware_from_request) {
    // statusLed* now travels with the advanced wiring form; do not clobber it.
    return;
  }
  if (existing.relay.enabled || existing.door_contact.enabled ||
      existing.exit_button.enabled || existing.status_led.enabled ||
      existing.buzzer.enabled) {
    cfg->relay = existing.relay;
    cfg->door_contact = existing.door_contact;
    cfg->exit_button = existing.exit_button;
    cfg->status_led = existing.status_led;
    cfg->buzzer = existing.buzzer;
  }
}

void ApplyMdnsHostname(viaaccess::RuntimeConfig* cfg, const std::string& override_value) {
  if (!override_value.empty()) {
    cfg->mdns.hostname = override_value;
    return;
  }
  if (!cfg->access_point_slug.empty()) {
    cfg->mdns.hostname = viaaccess::HostnameFromAccessPointSlug(cfg->access_point_slug);
  }
}

// BaseConfigFrom starts every save from the factory defaults and carries over
// only what must survive: the network the appliance is on, the reader wiring and
// the setup PIN. Same contract as the Go handler, which also rebuilds from
// DefaultRuntimeConfig on each save.
viaaccess::RuntimeConfig BaseConfigFrom(const viaaccess::RuntimeConfig& existing) {
  viaaccess::RuntimeConfig cfg = viaaccess::DefaultRuntimeConfig();
  cfg.wifi = existing.wifi;
  cfg.qr_reader = existing.qr_reader;
  cfg.setup_pin = existing.setup_pin;
  cfg.webhook_secret = existing.webhook_secret;
  cfg.unlock_webhook_url = existing.unlock_webhook_url;
  return cfg;
}

// FinishLocalSave persists without touching the network. Used for wiring-only
// saves (link may be down) and after a successful claim (Identity already answered
// once; a second ping must not burn a consumed clm_ token if it times out).
esp_err_t FinishLocalSave(httpd_req_t* req, viaaccess::RuntimeConfig cfg,
                          const std::string& message_prefix, bool dismiss_portal) {
  cfg = viaaccess::Normalize(std::move(cfg));

  if (cfg.configured && viaaccess::Trim(cfg.setup_pin).empty()) {
    return SendError(req, 400, "PIN de setup obrigatório após o provisionamento.");
  }

  const std::string invalid = viaaccess::ValidateOperational(cfg);
  if (!invalid.empty()) {
    return SendError(req, 400, invalid);
  }

  const std::string slug = cfg.access_point_slug;
  const std::string device_id = cfg.device_id;
  const std::string hostname = cfg.mdns.hostname;

  const esp_err_t saved = app::State::Instance().SaveConfig(std::move(cfg));
  if (saved != ESP_OK) {
    return SendError(req, 500,
                     std::string("Falha ao gravar configuração: ") + esp_err_to_name(saved));
  }

  cJSON* root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", true);
  std::string message = message_prefix + " Leitor ativo. LAN: http://" + hostname + ".local/setup";
  if (dismiss_portal) {
    message += " Pode sair de viaaccess-setup; o portal fecha agora.";
  }
  cJSON_AddStringToObject(root, "message", message.c_str());
  cJSON_AddStringToObject(root, "mdnsHostname", hostname.c_str());
  if (!slug.empty()) {
    cJSON_AddStringToObject(root, "accessPointSlug", slug.c_str());
  }
  if (!device_id.empty()) {
    cJSON_AddStringToObject(root, "deviceId", device_id.c_str());
  }
  const esp_err_t sent = SendJson(req, 200, PrintAndDelete(root));
  if (dismiss_portal) {
    wifi::ClosePortalAfterSetup();
  }
  return sent;
}

// FinishSave is the manual credential path (typed idb_ key / Identity URL). A
// fresh ping is required there because nothing has talked to Identity yet.
// Claim provision must not use this: ClaimProvision already reached Identity,
// and failing a follow-up Ping after consume leaves the token burned with no save.
esp_err_t FinishSave(httpd_req_t* req, viaaccess::RuntimeConfig cfg,
                     const std::string& message_prefix) {
  cfg = viaaccess::Normalize(std::move(cfg));

  const std::string invalid = viaaccess::ValidateOperational(cfg);
  if (!invalid.empty()) {
    return SendError(req, 400, invalid);
  }

  const esp_err_t ping = identity::Ping(cfg.identity_url, 8000);
  if (ping != ESP_OK) {
    return SendError(req, 502,
                     std::string("Identity inacessível: ") + esp_err_to_name(ping));
  }
  return FinishLocalSave(req, std::move(cfg), message_prefix, true);
}

// --- handlers -----------------------------------------------------------------

esp_err_t HandleHealth(httpd_req_t* req) {
  return SendJson(req, 200, app::State::Instance().HealthJson());
}

esp_err_t HandleScan(httpd_req_t* req) {
  std::string body;
  if (!ReadBody(req, &body)) {
    return SendError(req, 400, "Corpo inválido.");
  }
  const scan_service::HttpResult result =
      scan_service::HandleHttpRequest(body, HeaderValue(req, "X-Webhook-Secret"));
  return SendJson(req, result.status, result.body);
}

esp_err_t HandleSetupPage(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, reinterpret_cast<const char*>(setup_html_start),
                         setup_html_end - setup_html_start - 1);
}

esp_err_t HandleWifiPage(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, reinterpret_cast<const char*>(wifi_html_start),
                         wifi_html_end - wifi_html_start - 1);
}

// Serve the portal as 200 HTML. iOS captive detection ignores empty 302s and
// treats them as "no internet"; a real page (not Apple's Success body) opens
// the sign-in sheet. Android generate_204 with a body does the same.
esp_err_t ServePortalPage(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  if (wifi::connected()) {
    return HandleSetupPage(req);
  }
  return HandleWifiPage(req);
}

esp_err_t HandleRoot(httpd_req_t* req) { return ServePortalPage(req); }

esp_err_t HandleCaptive404(httpd_req_t* req, httpd_err_code_t /*err*/) {
  httpd_resp_set_status(req, StatusLine(200));
  return ServePortalPage(req);
}

esp_err_t HandleSetupStatus(httpd_req_t* req) {
  const viaaccess::RuntimeConfig cfg = app::State::Instance().config();
  cJSON* root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "setupRequired", !cfg.configured);
  cJSON_AddBoolToObject(root, "pinRequired", !viaaccess::Trim(cfg.setup_pin).empty());
  // Legacy / misconfigured units that are online without a PIN must set one
  // before any further local mutation.
  cJSON_AddBoolToObject(root, "pinSetupRequired",
                        cfg.configured && viaaccess::Trim(cfg.setup_pin).empty());
  cJSON_AddNumberToObject(root, "pinLockRemainingSec", PinLockRemainingSec());
  cJSON_AddNumberToObject(root, "pinAttemptsRemaining", PinAttemptsRemaining());
  cJSON_AddStringToObject(root, "agentVersion", viaaccess::kFirmwareVersion);
  cJSON_AddStringToObject(root, "identityUrl", cfg.identity_url.c_str());
  cJSON_AddStringToObject(root, "accessPointSlug", cfg.access_point_slug.c_str());
  cJSON_AddStringToObject(root, "deviceId", cfg.device_id.c_str());
  cJSON_AddStringToObject(root, "mdnsHostname", cfg.mdns.hostname.c_str());
  cJSON_AddNumberToObject(root, "httpPort", cfg.http_port);
  // The device key never leaves the appliance; the portal only needs to know
  // whether one is already stored.
  cJSON_AddBoolToObject(root, "deviceKeyStored", !cfg.device_key.empty());

  cJSON* network = cJSON_AddObjectToObject(root, "network");
  cJSON_AddBoolToObject(network, "connected", wifi::connected());
  cJSON_AddStringToObject(network, "ssid", cfg.wifi.ssid.c_str());
  cJSON_AddStringToObject(network, "ip", wifi::ip().c_str());
  cJSON_AddBoolToObject(network, "portalActive", wifi::portal_active());
  const std::string host = HeaderValue(req, "Host");
  const bool via_softap =
      wifi::host_is_softap(host) || wifi::peer_is_softap_client(httpd_req_to_sockfd(req));
  const bool writes_allowed =
      wifi::local_setup_writes_allowed(cfg.configured, host, httpd_req_to_sockfd(req));
  cJSON_AddBoolToObject(network, "viaSoftAp", via_softap);
  cJSON_AddBoolToObject(root, "setupWritesAllowed", writes_allowed);

  cJSON* hardware = cJSON_AddObjectToObject(root, "hardware");
  cJSON_AddBoolToObject(hardware, "relayEnabled", cfg.relay.enabled);
  cJSON_AddNumberToObject(hardware, "relayGpioPin", cfg.relay.gpio_pin);
  cJSON_AddStringToObject(hardware, "relayUnlockMode", cfg.relay.unlock_mode.c_str());
  cJSON_AddNumberToObject(hardware, "relayPulseMs", cfg.relay.pulse_ms);
  cJSON_AddBoolToObject(hardware, "relayActiveHigh", cfg.relay.active_high);
  cJSON_AddBoolToObject(hardware, "relayAvailable", relay::available());
  cJSON_AddBoolToObject(hardware, "doorContactEnabled", cfg.door_contact.enabled);
  cJSON_AddNumberToObject(hardware, "doorContactGpioPin", cfg.door_contact.gpio_pin);
  cJSON_AddBoolToObject(hardware, "doorContactSimulated", cfg.door_contact.simulated);
  cJSON_AddBoolToObject(hardware, "exitButtonEnabled", cfg.exit_button.enabled);
  cJSON_AddNumberToObject(hardware, "exitButtonGpioPin", cfg.exit_button.gpio_pin);
  cJSON_AddBoolToObject(hardware, "exitButtonSimulated", cfg.exit_button.simulated);
  cJSON_AddBoolToObject(hardware, "buzzerEnabled", cfg.buzzer.enabled);
  cJSON_AddNumberToObject(hardware, "buzzerGpioPin", cfg.buzzer.gpio_pin);
  cJSON_AddBoolToObject(hardware, "buzzerActiveHigh", cfg.buzzer.active_high);
  cJSON_AddBoolToObject(hardware, "buzzerAvailable", buzzer::available());
  cJSON_AddBoolToObject(hardware, "statusLedEnabled", cfg.status_led.enabled);
  cJSON_AddStringToObject(hardware, "statusLedDriver", cfg.status_led.driver.c_str());
  cJSON_AddNumberToObject(hardware, "statusLedWs2812Pin", cfg.status_led.ws2812_pin);
  cJSON_AddNumberToObject(hardware, "statusLedBrightness", cfg.status_led.brightness);
  cJSON_AddNumberToObject(hardware, "statusLedRedPin", cfg.status_led.red_pin);
  cJSON_AddNumberToObject(hardware, "statusLedGreenPin", cfg.status_led.green_pin);
  cJSON_AddNumberToObject(hardware, "statusLedBluePin", cfg.status_led.blue_pin);
  cJSON_AddBoolToObject(hardware, "statusLedActiveHigh", cfg.status_led.active_high);
  cJSON_AddNumberToObject(hardware, "qrUartRxPin", cfg.qr_reader.rx_pin);
  cJSON_AddNumberToObject(hardware, "qrUartTxPin", cfg.qr_reader.tx_pin);
  cJSON_AddNumberToObject(hardware, "qrUartBaud", cfg.qr_reader.baud);
  cJSON_AddNumberToObject(hardware, "rtcSdaPin", cfg.rtc.sda_pin);
  cJSON_AddNumberToObject(hardware, "rtcSclPin", cfg.rtc.scl_pin);

  // The portal shows this so a missing or flat DS3231 is caught during the visit,
  // not months later when the power fails and offline passage is refused.
  const viaaccess::ClockState clock_state = app::State::Instance().clock();
  cJSON* clock_json = cJSON_AddObjectToObject(root, "clock");
  cJSON_AddStringToObject(clock_json, "source",
                          viaaccess::ClockSourceString(clock_state.source));
  cJSON_AddStringToObject(clock_json, "sourceLabel",
                          viaaccess::ClockSourceLabelPt(clock_state.source));
  cJSON_AddBoolToObject(clock_json, "trusted", app::State::Instance().clock_trusted());
  cJSON_AddBoolToObject(clock_json, "rtcPresent", app::State::Instance().rtc_present());
  cJSON_AddBoolToObject(clock_json, "rtcBatteryLost",
                        app::State::Instance().rtc_battery_lost());

  return SendJson(req, 200, PrintAndDelete(root));
}

esp_err_t HandleSetupSave(httpd_req_t* req) {
  if (RejectIfSetupWritesBlocked(req)) {
    return ESP_OK;
  }
  std::string body;
  if (!ReadBody(req, &body)) {
    return SendError(req, 400, "Corpo inválido.");
  }
  cJSON* root = cJSON_Parse(body.c_str());
  if (root == nullptr || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return SendError(req, 400, "JSON inválido.");
  }
  const SetupWriteAuth auth = AuthorizeSetupWrite(root);
  if (!auth.ok) {
    cJSON_Delete(root);
    return SendError(req, auth.status, auth.error);
  }

  const viaaccess::RuntimeConfig existing = app::State::Instance().config();
  viaaccess::RuntimeConfig cfg = BaseConfigFrom(existing);
  cfg.configured = true;
  cfg.identity_url = viaaccess::TrimTrailingSlashes(JsonString(root, "identityUrl"));
  cfg.device_key = JsonString(root, "deviceKey");
  cfg.access_point_slug = JsonString(root, "accessPointSlug");
  // Manual setup has no claim, so a reused device key keeps its device id.
  cfg.device_id = existing.device_id;
  cfg.provisioned_at = existing.provisioned_at;
  ApplyEstablishedPin(&cfg, auth);
  if (viaaccess::Trim(cfg.setup_pin).empty()) {
    const std::string new_pin = viaaccess::Trim(JsonString(root, "setupPin"));
    if (!IsValidSetupPinFormat(new_pin)) {
      cJSON_Delete(root);
      return SendError(req, 400,
                       "Informe um PIN de setup numérico (4 a 8 dígitos) em setupPin.");
    }
    cfg.setup_pin = new_pin;
  }

  bool flag = false;
  if (JsonBool(root, "emitDetection", &flag)) {
    cfg.emit_detection = flag;
  }
  const bool hardware_from_request = ApplyHardwareOverrides(root, &cfg);
  PreserveLocalHardware(&cfg, existing, hardware_from_request);
  ApplyMdnsHostname(&cfg, JsonString(root, "mdnsHostname"));
  cJSON_Delete(root);

  return FinishSave(req, std::move(cfg), "Configuração salva.");
}

// HandleSetupHardware saves wiring only. Without it the sole way to move a pin or
// fix the relay polarity after provisioning is a fresh claim from the admin, which
// in the field means two people for one wire. Credentials, Identity URL, Wi-Fi and
// the mDNS hostname are left exactly as they are.
esp_err_t HandleSetupHardware(httpd_req_t* req) {
  if (RejectIfSetupWritesBlocked(req)) {
    return ESP_OK;
  }
  std::string body;
  if (!ReadBody(req, &body)) {
    return SendError(req, 400, "Corpo inválido.");
  }
  cJSON* root = cJSON_Parse(body.c_str());
  if (root == nullptr || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return SendError(req, 400, "JSON inválido.");
  }
  const SetupWriteAuth auth = AuthorizeSetupWrite(root);
  if (!auth.ok) {
    cJSON_Delete(root);
    return SendError(req, auth.status, auth.error);
  }

  viaaccess::RuntimeConfig cfg = app::State::Instance().config();
  if (!cfg.configured) {
    cJSON_Delete(root);
    return SendError(req, 409,
                     "Leitor ainda não provisionado: informe a fiação junto do claim.");
  }
  ApplyEstablishedPin(&cfg, auth);
  if (!ApplyHardwareOverrides(root, &cfg) && auth.establish_pin.empty()) {
    cJSON_Delete(root);
    return SendError(req, 400, "Nenhum campo de fiação foi enviado.");
  }
  cJSON_Delete(root);

  return FinishLocalSave(req, std::move(cfg), "Fiação salva.", false);
}

esp_err_t HandleSetupProvision(httpd_req_t* req) {
  if (RejectIfSetupWritesBlocked(req)) {
    return ESP_OK;
  }
  std::string body;
  if (!ReadBody(req, &body)) {
    return SendError(req, 400, "Corpo inválido.");
  }
  cJSON* root = cJSON_Parse(body.c_str());
  if (root == nullptr || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return SendError(req, 400, "JSON inválido.");
  }
  const SetupWriteAuth auth = AuthorizeSetupWrite(root);
  if (!auth.ok) {
    cJSON_Delete(root);
    return SendError(req, auth.status, auth.error);
  }

  const viaaccess::ProvisionInput input = viaaccess::ParseProvisionInput(
      JsonString(root, "claimInput"), JsonString(root, "identityUrl"));
  if (!input.ok) {
    cJSON_Delete(root);
    return SendError(req, 400, input.error);
  }

  const identity::ClaimResult claimed =
      identity::ClaimProvision(input.identity_url, input.claim_token, 12000);
  if (!claimed.ok) {
    cJSON_Delete(root);
    return SendError(req, 502, claimed.error);
  }

  const viaaccess::RuntimeConfig existing = app::State::Instance().config();
  viaaccess::RuntimeConfig cfg = BaseConfigFrom(existing);
  cfg.configured = true;
  cfg.identity_url =
      viaaccess::PreferReachableIdentityURL(input.identity_url, claimed.identity_url);
  cfg.device_key = claimed.device_key;
  cfg.device_id = claimed.device_id;
  cfg.access_point_slug = claimed.access_point_slug;
  cfg.provisioned_at = viaaccess::FormatRfc3339(static_cast<int64_t>(time(nullptr)));
  cfg.emit_detection = claimed.emit_detection;
  cfg.debounce_ms = claimed.debounce_ms;
  cfg.unlock_on_authorized_only = claimed.unlock_on_authorized_only;
  cfg.contingency = claimed.contingency;
  // Claim PIN wins over any previous local PIN (reprovision rotates setup PIN).
  if (!viaaccess::Trim(claimed.setup_pin).empty()) {
    cfg.setup_pin = viaaccess::Trim(claimed.setup_pin);
  } else {
    ApplyEstablishedPin(&cfg, auth);
    if (viaaccess::Trim(cfg.setup_pin).empty()) {
      const std::string new_pin = viaaccess::Trim(JsonString(root, "setupPin"));
      if (!IsValidSetupPinFormat(new_pin)) {
        cJSON_Delete(root);
        return SendError(req, 400,
                         "Identity não enviou setupPin; informe um PIN (4 a 8 dígitos).");
      }
      cfg.setup_pin = new_pin;
    }
  }

  const bool hardware_from_request = ApplyHardwareOverrides(root, &cfg);
  PreserveLocalHardware(&cfg, existing, hardware_from_request);
  ApplyMdnsHostname(&cfg, JsonString(root, "mdnsHostname"));
  cJSON_Delete(root);

  // Persist immediately: claim already proved reachability and consumed the token.
  return FinishLocalSave(req, std::move(cfg), "Provisionamento concluído.", true);
}

esp_err_t HandleWifiScan(httpd_req_t* req) {
  if (RejectIfSetupWritesBlocked(req)) {
    return ESP_OK;
  }
  const std::vector<wifi::ScanEntry> networks = wifi::Scan();
  cJSON* root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON* list = cJSON_AddArrayToObject(root, "networks");
  for (const wifi::ScanEntry& entry : networks) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "ssid", entry.ssid.c_str());
    cJSON_AddNumberToObject(item, "rssi", entry.rssi);
    cJSON_AddBoolToObject(item, "secure", entry.secure);
    cJSON_AddItemToArray(list, item);
  }
  return SendJson(req, 200, PrintAndDelete(root));
}

esp_err_t HandleWifiSave(httpd_req_t* req) {
  if (RejectIfSetupWritesBlocked(req)) {
    return ESP_OK;
  }
  std::string body;
  if (!ReadBody(req, &body)) {
    return SendError(req, 400, "Corpo inválido.");
  }
  cJSON* root = cJSON_Parse(body.c_str());
  if (root == nullptr || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return SendError(req, 400, "JSON inválido.");
  }
  const SetupWriteAuth auth = AuthorizeSetupWrite(root);
  if (!auth.ok) {
    cJSON_Delete(root);
    return SendError(req, auth.status, auth.error);
  }

  const std::string ssid = JsonString(root, "ssid");
  const cJSON* password_item = cJSON_GetObjectItemCaseSensitive(root, "password");
  const std::string password =
      cJSON_IsString(password_item) && password_item->valuestring != nullptr
          ? password_item->valuestring
          : "";

  if (ssid.empty()) {
    cJSON_Delete(root);
    return SendError(req, 400, "Informe o nome da rede (ssid).");
  }

  // Credentials are persisted before the radio retries: a failed association
  // must not lose what the technician typed, and NVS survives the reboot the
  // installer will probably try next.
  viaaccess::RuntimeConfig cfg = app::State::Instance().config();
  ApplyEstablishedPin(&cfg, auth);
  cfg.wifi.ssid = ssid;
  cfg.wifi.password = password;
  cJSON_Delete(root);
  const esp_err_t saved = app::State::Instance().SaveConfig(std::move(cfg));
  if (saved != ESP_OK) {
    return SendError(req, 500,
                     std::string("Falha ao gravar credenciais: ") + esp_err_to_name(saved));
  }

  const esp_err_t applied = wifi::ApplyCredentials(ssid, password);
  if (applied != ESP_OK) {
    return SendError(req, 500,
                     std::string("Falha ao aplicar credenciais: ") + esp_err_to_name(applied));
  }
  // Drop the 10 min hold: GOT_IP will close SoftAP once the station is up.
  wifi::ReleasePortalHold();

  cJSON* response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "ok", true);
  cJSON_AddStringToObject(response, "ssid", ssid.c_str());
  cJSON_AddStringToObject(
      response, "message",
      "Credenciais salvas. Conectando à rede; acompanhe em /api/setup.");
  return SendJson(req, 200, PrintAndDelete(response));
}

// SimState reads the shared {"state":"..."} body of both simulation endpoints.
bool SimState(httpd_req_t* req, std::string* state, esp_err_t* sent) {
  std::string body;
  if (!ReadBody(req, &body)) {
    *sent = SendError(req, 400, "Corpo inválido.");
    return false;
  }
  cJSON* root = cJSON_Parse(body.c_str());
  if (root == nullptr || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    *sent = SendError(req, 400, "JSON inválido.");
    return false;
  }
  *state = viaaccess::ToLower(JsonString(root, "state"));
  cJSON_Delete(root);
  return true;
}

esp_err_t HandleDoorContactSim(httpd_req_t* req) {
  const viaaccess::RuntimeConfig cfg = app::State::Instance().config();
  if (!cfg.door_contact.enabled || !cfg.door_contact.simulated) {
    return SendError(req, 409, "Contato de porta simulado não está habilitado.");
  }
  std::string state;
  esp_err_t sent = ESP_OK;
  if (!SimState(req, &state, &sent)) {
    return sent;
  }
  if (state != "open" && state != "closed") {
    return SendError(req, 400, "state deve ser open ou closed.");
  }

  // Same as the Go agent: flip the virtual reed and let the watcher debounce
  // and POST. Posting here would skip held_open and race the real path.
  const esp_err_t set = door_contact::SetSimOpen(state == "open");
  if (set != ESP_OK) {
    return SendError(req, 409, "Simulador de porta indisponível.");
  }

  cJSON* root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddStringToObject(root, "state", state.c_str());
  return SendJson(req, 200, PrintAndDelete(root));
}

esp_err_t HandleExitButtonSim(httpd_req_t* req) {
  const viaaccess::RuntimeConfig cfg = app::State::Instance().config();
  if (!cfg.exit_button.enabled || !cfg.exit_button.simulated) {
    return SendError(req, 409, "Botoeira simulada não está habilitada.");
  }
  std::string state;
  esp_err_t sent = ESP_OK;
  if (!SimState(req, &state, &sent)) {
    return sent;
  }
  const bool pressed = state == "pressed" || state == "press";
  const bool released = state == "idle" || state == "released" || state == "release";
  if (!pressed && !released) {
    return SendError(req, 400, "state deve ser pressed ou idle.");
  }

  // Same as the Go agent: flip the virtual button and let the watcher debounce,
  // notify Identity and pulse. Posting here would skip cooldown and arming.
  const esp_err_t set = exit_button::SetSimPressed(pressed);
  if (set != ESP_OK) {
    return SendError(req, 409, "Simulador de botoeira indisponível.");
  }

  cJSON* root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddStringToObject(root, "state", pressed ? "pressed" : "idle");
  return SendJson(req, 200, PrintAndDelete(root));
}

struct Route {
  const char* uri;
  httpd_method_t method;
  esp_err_t (*handler)(httpd_req_t*);
};

constexpr Route kRoutes[] = {
    {"/health", HTTP_GET, HandleHealth},
    {"/scan", HTTP_POST, HandleScan},
    {"/", HTTP_GET, HandleRoot},
    {"/setup", HTTP_GET, HandleSetupPage},
    {"/wifi", HTTP_GET, HandleWifiPage},
    {"/api/setup", HTTP_GET, HandleSetupStatus},
    {"/api/setup", HTTP_POST, HandleSetupSave},
    {"/api/setup/provision", HTTP_POST, HandleSetupProvision},
    {"/api/setup/hardware", HTTP_POST, HandleSetupHardware},
    {"/api/setup/wifi/scan", HTTP_GET, HandleWifiScan},
    {"/api/setup/wifi", HTTP_POST, HandleWifiSave},
    {"/api/door-contact/sim", HTTP_POST, HandleDoorContactSim},
    {"/api/exit-button/sim", HTTP_POST, HandleExitButtonSim},
};

esp_err_t RegisterRoutes(httpd_handle_t server) {
  for (const Route& route : kRoutes) {
    const httpd_uri_t uri = {
        .uri = route.uri,
        .method = route.method,
        .handler = route.handler,
        .user_ctx = nullptr,
    };
    const esp_err_t err = httpd_register_uri_handler(server, &uri);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "cannot register %s: %s", route.uri, esp_err_to_name(err));
      return err;
    }
  }
  return ESP_OK;
}

esp_err_t RegisterCaptiveRoutes(httpd_handle_t server) {
  const esp_err_t err = RegisterRoutes(server);
  if (err != ESP_OK) {
    return err;
  }
  return httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, HandleCaptive404);
}

void StopServers() {
  if (g_https_server != nullptr) {
    httpd_ssl_stop(g_https_server);
    g_https_server = nullptr;
  }
  if (g_captive_http != nullptr) {
    httpd_stop(g_captive_http);
    g_captive_http = nullptr;
  }
  if (g_http_server != nullptr) {
    httpd_stop(g_http_server);
    g_http_server = nullptr;
  }
}

esp_err_t StartHttpsServer() {
  if (g_https_server != nullptr) {
    return ESP_OK;
  }
  httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
  conf.port_secure = static_cast<uint16_t>(kHttpsPort);
  conf.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
  conf.servercert = servercert_pem_start;
  conf.servercert_len = servercert_pem_end - servercert_pem_start;
  conf.prvtkey_pem = prvtkey_pem_start;
  conf.prvtkey_len = prvtkey_pem_end - prvtkey_pem_start;
  conf.httpd.max_uri_handlers = sizeof(kRoutes) / sizeof(kRoutes[0]) + 2;
  conf.httpd.stack_size = 16384;
  conf.httpd.recv_wait_timeout = 15;
  conf.httpd.send_wait_timeout = 15;
  conf.httpd.lru_purge_enable = true;
  conf.httpd.max_open_sockets = 2;
  // Avoid colliding with the plain HTTP control port.
  conf.httpd.ctrl_port = ESP_HTTPD_DEF_CTRL_PORT + 1;

  esp_err_t err = httpd_ssl_start(&g_https_server, &conf);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "HTTPS :%d unavailable: %s (SoftAP still uses HTTP :%d)", kHttpsPort,
             esp_err_to_name(err), g_port);
    g_https_server = nullptr;
    return ESP_OK;
  }
  err = RegisterRoutes(g_https_server);
  if (err != ESP_OK) {
    httpd_ssl_stop(g_https_server);
    g_https_server = nullptr;
    return ESP_OK;
  }
  ESP_LOGI(kTag, "HTTPS listening on port %d (self-signed; SoftAP phones should use HTTP)",
           kHttpsPort);
  return ESP_OK;
}

esp_err_t StartCaptiveHttp() {
  if (g_captive_http != nullptr) {
    return ESP_OK;
  }
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = kCaptiveHttpPort;
  config.ctrl_port = ESP_HTTPD_DEF_CTRL_PORT + 2;
  config.max_uri_handlers = sizeof(kRoutes) / sizeof(kRoutes[0]) + 2;
  config.stack_size = 8192;
  config.recv_wait_timeout = 15;
  config.send_wait_timeout = 15;
  config.lru_purge_enable = true;
  config.max_open_sockets = 7;

  // Captive probes generate a lot of unknown Host/URI noise.
  esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
  esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
  esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

  esp_err_t err = httpd_start(&g_captive_http, &config);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "HTTP :%d unavailable: %s (captive portal needs this port)", kCaptiveHttpPort,
             esp_err_to_name(err));
    g_captive_http = nullptr;
    return ESP_OK;
  }
  err = RegisterCaptiveRoutes(g_captive_http);
  if (err != ESP_OK) {
    httpd_stop(g_captive_http);
    g_captive_http = nullptr;
    return ESP_OK;
  }
  ESP_LOGI(kTag, "HTTP listening on port %d (captive portal)", kCaptiveHttpPort);
  return ESP_OK;
}

esp_err_t StartOn(int port) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = static_cast<uint16_t>(port);
  config.ctrl_port = ESP_HTTPD_DEF_CTRL_PORT;
  config.max_uri_handlers = sizeof(kRoutes) / sizeof(kRoutes[0]) + 2;
  // Provision talks to Identity inside the handler (claim, then ping), so the
  // worker needs both a generous stack and a receive timeout above those calls.
  config.stack_size = 8192;
  config.recv_wait_timeout = 15;
  config.send_wait_timeout = 15;
  config.lru_purge_enable = true;
  config.max_open_sockets = 4;

  // Captive :80 first so phones still get a portal if later listeners fail.
  StartCaptiveHttp();

  esp_err_t err = httpd_start(&g_http_server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "httpd_start on port %d failed: %s", port, esp_err_to_name(err));
    g_http_server = nullptr;
    return err;
  }
  err = RegisterRoutes(g_http_server);
  if (err != ESP_OK) {
    StopServers();
    return err;
  }
  g_port = port;
  ESP_LOGI(kTag, "HTTP listening on port %d (scripts / homologate)", port);
  StartHttpsServer();
  return ESP_OK;
}

void RestartOnPendingPort(void* /*argument*/) {
  const int port = g_pending_port;
  if (port <= 0 || port == g_port) {
    return;
  }
  const int previous = g_port;
  StopServers();
  if (StartOn(port) != ESP_OK) {
    // Falling back to the previous port keeps the appliance reachable instead of
    // leaving it with no local server at all.
    ESP_LOGE(kTag, "port %d unavailable, restoring %d", port, previous);
    StartOn(previous);
  }
}

}  // namespace

esp_err_t Start() {
  if (g_http_server != nullptr) {
    return ESP_OK;
  }
  return StartOn(app::State::Instance().config().http_port);
}

esp_err_t ApplyPort(int port) {
  if (port <= 0 || port == g_port) {
    return ESP_OK;
  }
  // The caller is usually a request handler running on the server's own worker,
  // so httpd_stop cannot happen inline: it waits for that very task to finish.
  // Restarting from a timer lets the response for /api/setup go out first.
  g_pending_port = port;
  if (g_restart_timer == nullptr) {
    const esp_timer_create_args_t args = {
        .callback = RestartOnPendingPort,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "httpd_restart",
        .skip_unhandled_events = true,
    };
    const esp_err_t err = esp_timer_create(&args, &g_restart_timer);
    if (err != ESP_OK) {
      return err;
    }
  }
  esp_timer_stop(g_restart_timer);
  return esp_timer_start_once(g_restart_timer, 500 * 1000);
}

}  // namespace http_server
