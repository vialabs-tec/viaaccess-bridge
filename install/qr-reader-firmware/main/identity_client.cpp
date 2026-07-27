#include "identity_client.hpp"

#include <cstring>
#include <ctime>

#include "cJSON.h"
#include "config_json.hpp"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "viaaccess/strings.hpp"
#include "viaaccess/time.hpp"
#include "viaaccess/version.hpp"

namespace identity {
namespace {

constexpr const char* kTag = "identity";

// The policy snapshot grows with the member grant list. PSRAM makes this
// affordable, but the cap keeps a misbehaving endpoint from exhausting the heap.
constexpr std::size_t kMaxResponseBytes = 64 * 1024;

struct ResponseSink {
  std::string body;
  std::string etag;
  bool truncated = false;
};

struct Response {
  esp_err_t err = ESP_FAIL;
  int status = 0;
  std::string body;
  std::string etag;
};

struct Request {
  std::string url;
  esp_http_client_method_t method = HTTP_METHOD_GET;
  std::string body;
  int timeout_ms = 8000;
  // bridge_headers adds the device key plus the capability headers Identity
  // stores against the reader record.
  bool bridge_headers = false;
  std::string if_none_match;
};

esp_err_t OnHttpEvent(esp_http_client_event_t* event) {
  auto* sink = static_cast<ResponseSink*>(event->user_data);
  if (sink == nullptr) {
    return ESP_OK;
  }
  switch (event->event_id) {
    case HTTP_EVENT_ON_HEADER:
      if (event->header_key != nullptr && event->header_value != nullptr &&
          strcasecmp(event->header_key, "ETag") == 0) {
        sink->etag = event->header_value;
      }
      break;
    case HTTP_EVENT_ON_DATA:
      if (sink->body.size() + event->data_len > kMaxResponseBytes) {
        sink->truncated = true;
        break;
      }
      sink->body.append(static_cast<const char*>(event->data),
                        static_cast<std::size_t>(event->data_len));
      break;
    default:
      break;
  }
  return ESP_OK;
}

std::string BaseUrl(const std::string& identity_url) {
  return viaaccess::TrimTrailingSlashes(viaaccess::Trim(identity_url));
}

// MdnsHeaderValue mirrors setMdnsHostnameHeader in the Go agent: Identity stores
// the bare label, so an accidental .local suffix is stripped here too.
std::string MdnsHeaderValue(const std::string& hostname) {
  std::string value = viaaccess::ToLower(viaaccess::Trim(hostname));
  if (viaaccess::EndsWith(value, ".local")) {
    value = value.substr(0, value.size() - 6);
  }
  while (!value.empty() && value.back() == '.') {
    value.pop_back();
  }
  if (value.size() > 63) {
    value = value.substr(0, 63);
  }
  return value;
}

void SetBridgeHeaders(esp_http_client_handle_t client,
                      const viaaccess::RuntimeConfig& cfg) {
  const std::string authorization = "Bearer " + cfg.device_key;
  esp_http_client_set_header(client, "Authorization", authorization.c_str());
  esp_http_client_set_header(client, "X-ViaAccess-Relay-Enabled",
                            cfg.relay.enabled ? "true" : "false");
  esp_http_client_set_header(client, "X-ViaAccess-Door-Contact-Enabled",
                            cfg.door_contact.enabled ? "true" : "false");
  esp_http_client_set_header(client, "X-ViaAccess-Exit-Button-Enabled",
                            cfg.exit_button.enabled ? "true" : "false");
  esp_http_client_set_header(client, "X-ViaAccess-Agent-Version",
                            viaaccess::kFirmwareVersion);
  const std::string hostname = MdnsHeaderValue(cfg.mdns.hostname);
  if (!hostname.empty()) {
    esp_http_client_set_header(client, "X-ViaAccess-Mdns-Hostname", hostname.c_str());
  }
}

Response Perform(const Request& request, const viaaccess::RuntimeConfig* cfg) {
  Response response;
  ResponseSink sink;

  esp_http_client_config_t config = {};
  config.url = request.url.c_str();
  config.method = request.method;
  config.timeout_ms = request.timeout_ms;
  config.event_handler = OnHttpEvent;
  config.user_data = &sink;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.buffer_size_tx = 1024;
  config.keep_alive_enable = true;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    response.err = ESP_ERR_NO_MEM;
    return response;
  }

  if (cfg != nullptr && request.bridge_headers) {
    SetBridgeHeaders(client, *cfg);
  }
  if (!request.if_none_match.empty()) {
    esp_http_client_set_header(client, "If-None-Match", request.if_none_match.c_str());
  }
  if (!request.body.empty()) {
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, request.body.c_str(),
                                  static_cast<int>(request.body.size()));
  }

  response.err = esp_http_client_perform(client);
  if (response.err == ESP_OK) {
    response.status = esp_http_client_get_status_code(client);
  }
  esp_http_client_cleanup(client);

  response.body = std::move(sink.body);
  response.etag = std::move(sink.etag);
  if (sink.truncated) {
    ESP_LOGW(kTag, "response from %s exceeded %u bytes and was truncated",
             request.url.c_str(), static_cast<unsigned>(kMaxResponseBytes));
  }
  return response;
}

std::string JsonField(const std::string& body, const char* key) {
  cJSON* root = cJSON_Parse(body.c_str());
  if (root == nullptr) {
    return "";
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
  std::string out;
  if (cJSON_IsString(item) && item->valuestring != nullptr) {
    out = item->valuestring;
  }
  cJSON_Delete(root);
  return out;
}

bool IsSuccess(int status) { return status >= 200 && status < 300; }

// Classify turns a transport error or a non-2xx status into an Outcome, marking
// revoked keys so the caller can drop into setup mode.
Outcome Classify(const char* what, const Response& response) {
  Outcome outcome;
  if (response.err != ESP_OK) {
    outcome.error = std::string(what) + ": " + esp_err_to_name(response.err);
    return outcome;
  }
  if (IsSuccess(response.status)) {
    outcome.ok = true;
    return outcome;
  }
  const std::string code = JsonField(response.body, "code");
  if (viaaccess::IsBridgeAuthFailure(response.status, code)) {
    outcome.unauthorized = true;
    outcome.error = std::string(what) + ": HTTP " + std::to_string(response.status);
    return outcome;
  }
  outcome.error = std::string(what) + ": HTTP " + std::to_string(response.status);
  return outcome;
}

std::string NowRfc3339() {
  return viaaccess::FormatRfc3339(static_cast<int64_t>(std::time(nullptr)));
}

Outcome PostEvent(const viaaccess::RuntimeConfig& cfg,
                  const char* path,
                  const char* what,
                  const std::string& kind) {
  Outcome outcome;
  if (viaaccess::Trim(kind).empty()) {
    outcome.error = std::string(what) + ": kind is required";
    return outcome;
  }

  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "kind", kind.c_str());
  const std::string at = NowRfc3339();
  if (!at.empty()) {
    cJSON_AddStringToObject(root, "at", at.c_str());
  }
  char* printed = cJSON_PrintUnformatted(root);
  const std::string body = printed != nullptr ? printed : "{}";
  cJSON_free(printed);
  cJSON_Delete(root);

  Request request;
  request.url = BaseUrl(cfg.identity_url) + path;
  request.method = HTTP_METHOD_POST;
  request.body = body;
  request.bridge_headers = true;
  request.timeout_ms = 8000;

  return Classify(what, Perform(request, &cfg));
}

}  // namespace

viaaccess::RedeemResult RedeemQrUrl(const viaaccess::RuntimeConfig& cfg,
                                    const std::string& qr_url,
                                    int timeout_ms) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "qrUrl", viaaccess::Trim(qr_url).c_str());
  cJSON_AddBoolToObject(root, "emitDetection", cfg.emit_detection);
  char* printed = cJSON_PrintUnformatted(root);
  const std::string body = printed != nullptr ? printed : "{}";
  cJSON_free(printed);
  cJSON_Delete(root);

  Request request;
  request.url = BaseUrl(cfg.identity_url) + "/api/bridge/intent/redeem";
  request.method = HTTP_METHOD_POST;
  request.body = body;
  request.bridge_headers = true;
  request.timeout_ms = timeout_ms > 0 ? timeout_ms : 3000;

  const Response response = Perform(request, &cfg);

  viaaccess::RedeemResult result;
  if (response.err != ESP_OK) {
    result.status = 0;
    result.data.error = std::string("Identity inacessível: ") + esp_err_to_name(response.err);
    return result;
  }
  result.status = response.status;

  cJSON* parsed = cJSON_Parse(response.body.c_str());
  if (parsed != nullptr) {
    const auto read_string = [&parsed](const char* key) -> std::string {
      const cJSON* item = cJSON_GetObjectItemCaseSensitive(parsed, key);
      if (cJSON_IsString(item) && item->valuestring != nullptr) {
        return item->valuestring;
      }
      return "";
    };
    const auto read_bool = [&parsed](const char* key) -> bool {
      const cJSON* item = cJSON_GetObjectItemCaseSensitive(parsed, key);
      return cJSON_IsTrue(item) != 0;
    };
    result.data.ok = read_bool("ok");
    result.data.redeemed = read_bool("redeemed");
    result.data.validation_id = read_string("validationId");
    result.data.detection_id = read_string("detectionId");
    result.data.member_id = read_string("memberId");
    result.data.correlation_outcome = read_string("correlationOutcome");
    result.data.access_point_slug = read_string("accessPointSlug");
    result.data.error = read_string("error");
    result.data.code = read_string("code");
    cJSON_Delete(parsed);
  }

  result.ok = IsSuccess(response.status);
  return result;
}

esp_err_t Ping(const std::string& identity_url, int timeout_ms) {
  Request request;
  request.url = BaseUrl(identity_url) + "/api/openapi";
  request.timeout_ms = timeout_ms > 0 ? timeout_ms : 8000;

  const Response response = Perform(request, nullptr);
  if (response.err != ESP_OK) {
    return response.err;
  }
  return IsSuccess(response.status) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

ClaimResult ClaimProvision(const std::string& identity_url,
                           const std::string& claim_token,
                           int timeout_ms) {
  ClaimResult result;
  const std::string base = BaseUrl(identity_url);
  if (base.empty()) {
    result.error = "Informe a URL do Identity.";
    return result;
  }

  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "claimToken", claim_token.c_str());
  char* printed = cJSON_PrintUnformatted(root);
  const std::string body = printed != nullptr ? printed : "{}";
  cJSON_free(printed);
  cJSON_Delete(root);

  Request request;
  request.url = base + "/api/bridge/provision/claim";
  request.method = HTTP_METHOD_POST;
  request.body = body;
  request.timeout_ms = timeout_ms > 0 ? timeout_ms : 12000;

  const Response response = Perform(request, nullptr);
  if (response.err != ESP_OK) {
    result.error = std::string("Identity inacessível: ") + esp_err_to_name(response.err);
    return result;
  }

  cJSON* parsed = cJSON_Parse(response.body.c_str());
  if (parsed == nullptr) {
    result.error = "Resposta inválida do Identity.";
    return result;
  }

  const auto read_string = [&parsed](const char* key) -> std::string {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(parsed, key);
    if (cJSON_IsString(item) && item->valuestring != nullptr) {
      return item->valuestring;
    }
    return "";
  };

  if (!IsSuccess(response.status)) {
    result.error = read_string("error");
    if (result.error.empty()) {
      result.error = "Provisionamento falhou (HTTP " + std::to_string(response.status) + ").";
    }
    cJSON_Delete(parsed);
    return result;
  }

  result.device_id = read_string("deviceId");
  result.device_key = read_string("deviceKey");
  result.identity_url = read_string("identityUrl");
  result.access_point_slug = read_string("accessPointSlug");

  const cJSON* defaults = cJSON_GetObjectItemCaseSensitive(parsed, "defaults");
  if (cJSON_IsObject(defaults)) {
    const cJSON* emit = cJSON_GetObjectItemCaseSensitive(defaults, "emitDetection");
    if (cJSON_IsBool(emit)) {
      result.emit_detection = cJSON_IsTrue(emit) != 0;
    }
    const cJSON* debounce = cJSON_GetObjectItemCaseSensitive(defaults, "debounceMs");
    if (cJSON_IsNumber(debounce)) {
      result.debounce_ms = debounce->valueint;
    }
    const cJSON* unlock_only =
        cJSON_GetObjectItemCaseSensitive(defaults, "unlockOnAuthorizedOnly");
    if (cJSON_IsBool(unlock_only)) {
      result.unlock_on_authorized_only = cJSON_IsTrue(unlock_only) != 0;
    }
    const cJSON* contingency = cJSON_GetObjectItemCaseSensitive(defaults, "contingency");
    if (cJSON_IsObject(contingency)) {
      const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(contingency, "enabled");
      if (cJSON_IsBool(enabled)) {
        result.contingency.enabled = cJSON_IsTrue(enabled) != 0;
      }
      const cJSON* timeout =
          cJSON_GetObjectItemCaseSensitive(contingency, "onlineRedeemTimeoutMs");
      if (cJSON_IsNumber(timeout)) {
        result.contingency.online_redeem_timeout_ms = timeout->valueint;
      }
      const cJSON* stale =
          cJSON_GetObjectItemCaseSensitive(contingency, "maxPolicyStaleHours");
      if (cJSON_IsNumber(stale)) {
        result.contingency.max_policy_stale_hours = stale->valueint;
      }
    }
  }

  const cJSON* ok_field = cJSON_GetObjectItemCaseSensitive(parsed, "ok");
  const bool claimed_ok = cJSON_IsTrue(ok_field) != 0;
  cJSON_Delete(parsed);

  if (!claimed_ok || result.device_key.empty()) {
    result.error = "Resposta de provisionamento incompleta.";
    return result;
  }
  result.ok = true;
  return result;
}

PolicyFetch FetchPolicySnapshot(const viaaccess::RuntimeConfig& cfg) {
  PolicyFetch fetch;

  Request request;
  request.url = BaseUrl(cfg.identity_url) + "/api/bridge/policy-snapshot";
  request.bridge_headers = true;
  request.timeout_ms = 20000;

  const Response response = Perform(request, &cfg);
  fetch.outcome = Classify("policy sync", response);
  if (!fetch.outcome.ok) {
    return fetch;
  }
  if (!config_json::ParsePolicySnapshot(response.body, &fetch.policy)) {
    fetch.outcome.ok = false;
    fetch.outcome.error = "policy sync: resposta não é JSON válido";
    return fetch;
  }
  fetch.raw_json = response.body;
  return fetch;
}

DeviceConfigFetch FetchDeviceConfig(const viaaccess::RuntimeConfig& cfg,
                                    const std::string& if_none_match) {
  DeviceConfigFetch fetch;

  Request request;
  request.url = BaseUrl(cfg.identity_url) + "/api/bridge/device-config";
  request.bridge_headers = true;
  request.if_none_match = viaaccess::Trim(if_none_match);
  request.timeout_ms = 12000;

  const Response response = Perform(request, &cfg);
  if (response.err == ESP_OK && response.status == 304) {
    fetch.not_modified = true;
    fetch.outcome.ok = true;
    fetch.etag = request.if_none_match;
    return fetch;
  }

  fetch.outcome = Classify("device config", response);
  if (!fetch.outcome.ok) {
    return fetch;
  }
  if (!config_json::ParseRemoteDeviceConfig(response.body, &fetch.config)) {
    fetch.outcome.ok = false;
    fetch.outcome.error = "device config: resposta não é JSON válido";
    return fetch;
  }
  fetch.etag = response.etag;
  return fetch;
}

CommandsFetch FetchCommands(const viaaccess::RuntimeConfig& cfg) {
  CommandsFetch fetch;

  Request request;
  request.url = BaseUrl(cfg.identity_url) + "/api/bridge/commands";
  request.bridge_headers = true;
  request.timeout_ms = 8000;

  const Response response = Perform(request, &cfg);
  fetch.outcome = Classify("commands poll", response);
  if (!fetch.outcome.ok) {
    return fetch;
  }

  cJSON* root = cJSON_Parse(response.body.c_str());
  if (root == nullptr) {
    fetch.outcome.ok = false;
    fetch.outcome.error = "commands poll: resposta não é JSON válido";
    return fetch;
  }

  const cJSON* poll_after = cJSON_GetObjectItemCaseSensitive(root, "pollAfterMs");
  if (cJSON_IsNumber(poll_after)) {
    fetch.poll_after_ms = poll_after->valueint;
  }

  const cJSON* commands = cJSON_GetObjectItemCaseSensitive(root, "commands");
  if (cJSON_IsArray(commands)) {
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, commands) {
      if (!cJSON_IsObject(item)) {
        continue;
      }
      const auto read_string = [item](const char* key) -> std::string {
        const cJSON* field = cJSON_GetObjectItemCaseSensitive(item, key);
        if (cJSON_IsString(field) && field->valuestring != nullptr) {
          return field->valuestring;
        }
        return "";
      };
      PendingCommand command;
      command.id = read_string("id");
      command.type = read_string("type");
      command.reason = read_string("reason");
      const cJSON* payload = cJSON_GetObjectItemCaseSensitive(item, "payload");
      if (cJSON_IsObject(payload)) {
        const auto read_payload = [payload](const char* key) -> std::string {
          const cJSON* field = cJSON_GetObjectItemCaseSensitive(payload, key);
          if (cJSON_IsString(field) && field->valuestring != nullptr) {
            return field->valuestring;
          }
          return "";
        };
        command.has_ota_payload = true;
        command.ota_version = read_payload("version");
        command.ota_url = read_payload("url");
        command.ota_sha256 = read_payload("sha256");
      }
      if (!command.id.empty()) {
        fetch.commands.push_back(std::move(command));
      }
    }
  }

  cJSON_Delete(root);
  return fetch;
}

Outcome AckCommand(const viaaccess::RuntimeConfig& cfg,
                   const std::string& command_id,
                   bool ok,
                   const std::string& error) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", ok);
  cJSON_AddStringToObject(root, "error", error.c_str());
  char* printed = cJSON_PrintUnformatted(root);
  const std::string body = printed != nullptr ? printed : "{}";
  cJSON_free(printed);
  cJSON_Delete(root);

  Request request;
  request.url = BaseUrl(cfg.identity_url) + "/api/bridge/commands/" + command_id + "/ack";
  request.method = HTTP_METHOD_POST;
  request.body = body;
  request.bridge_headers = true;
  request.timeout_ms = 12000;

  return Classify("ack", Perform(request, &cfg));
}

Outcome PostDoorContactEvent(const viaaccess::RuntimeConfig& cfg, const std::string& kind) {
  return PostEvent(cfg, "/api/bridge/door-contact/events", "door-contact", kind);
}

Outcome PostExitButtonEvent(const viaaccess::RuntimeConfig& cfg, const std::string& kind) {
  return PostEvent(cfg, "/api/bridge/exit-button/events", "exit-button", kind);
}

UnlockWebhookResult PostUnlockWebhook(const std::string& url,
                                      const UnlockWebhookPayload& payload,
                                      int timeout_ms) {
  UnlockWebhookResult result;
  if (viaaccess::Trim(url).empty()) {
    result.error = "unlock webhook not configured";
    return result;
  }

  cJSON* root = cJSON_CreateObject();
  const auto add_if_set = [root](const char* key, const std::string& value) {
    if (!value.empty()) {
      cJSON_AddStringToObject(root, key, value.c_str());
    }
  };
  add_if_set("memberId", payload.member_id);
  add_if_set("validationId", payload.validation_id);
  add_if_set("detectionId", payload.detection_id);
  add_if_set("correlationOutcome", payload.correlation_outcome);
  add_if_set("accessPointSlug", payload.access_point_slug);
  char* printed = cJSON_PrintUnformatted(root);
  const std::string body = printed != nullptr ? printed : "{}";
  cJSON_free(printed);
  cJSON_Delete(root);

  Request request;
  request.url = viaaccess::Trim(url);
  request.method = HTTP_METHOD_POST;
  request.body = body;
  request.timeout_ms = timeout_ms > 0 ? timeout_ms : 8000;

  const Response response = Perform(request, nullptr);
  if (response.err != ESP_OK) {
    result.error = esp_err_to_name(response.err);
    return result;
  }
  result.status = response.status;
  if (!IsSuccess(response.status)) {
    result.error = "unlock webhook HTTP " + std::to_string(response.status);
    return result;
  }
  result.ok = true;
  return result;
}

}  // namespace identity
