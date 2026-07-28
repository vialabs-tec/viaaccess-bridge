#include "scan_service.hpp"

#include <ctime>
#include <mutex>
#include <utility>

#include "app_state.hpp"
#include "cJSON.h"
#include "contingency_store.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "identity_client.hpp"
#include "relay.hpp"
#include "viaaccess/contingency.hpp"
#include "viaaccess/mode.hpp"
#include "viaaccess/outbox.hpp"
#include "viaaccess/redeem.hpp"
#include "viaaccess/scan.hpp"
#include "viaaccess/strings.hpp"

namespace scan_service {
namespace {

constexpr const char* kTag = "scan";

std::mutex g_mutex;
viaaccess::Debounce g_debounce;

int64_t NowMs() { return esp_timer_get_time() / 1000; }

std::string PrintAndDelete(cJSON* root) {
  char* printed = cJSON_PrintUnformatted(root);
  std::string out = printed != nullptr ? printed : "{}";
  cJSON_free(printed);
  cJSON_Delete(root);
  return out;
}

std::string ErrorBody(const std::string& message, const std::string& code = "") {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", false);
  cJSON_AddStringToObject(root, "error", message.c_str());
  if (!code.empty()) {
    cJSON_AddStringToObject(root, "code", code.c_str());
  }
  return PrintAndDelete(root);
}

// ExtractQrUrl accepts the JSON shapes the Go agent accepts, and falls back to a
// bare URL body for readers that POST plain text.
std::string ExtractQrUrl(const std::string& raw_body) {
  const std::string trimmed = viaaccess::Trim(raw_body);
  if (trimmed.empty()) {
    return "";
  }

  cJSON* root = cJSON_Parse(trimmed.c_str());
  if (root == nullptr) {
    return trimmed;
  }
  if (cJSON_IsString(root) && root->valuestring != nullptr) {
    const std::string value = viaaccess::Trim(root->valuestring);
    cJSON_Delete(root);
    return value;
  }
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return trimmed;
  }

  const auto read = [root](const char* key) -> std::string {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring != nullptr) {
      return item->valuestring;
    }
    return "";
  };
  const std::string selected =
      viaaccess::SelectQrUrl(read("qrUrl"), read("qr"), read("payload"), "");
  cJSON_Delete(root);
  return selected;
}

viaaccess::RedeemResult BlockedByStalePolicy(const std::string& online_hint) {
  viaaccess::RedeemResult result;
  result.ok = false;
  result.status = 503;
  std::string message =
      "Política local desatualizada ou contingência desabilitada. Aguarde retorno da rede.";
  if (!online_hint.empty()) {
    message = online_hint + " " + message;
  }
  result.data.error = message;
  result.data.code = "SYNC_STALE";
  return result;
}

// TryContingency mirrors internal/scan/handler.go: local HMAC verify, nonce,
// outbox enqueue, then a synthetic AUTHORIZED redeem for relay/unlock.
std::pair<viaaccess::ScanPath, viaaccess::RedeemResult> TryContingency(
    const viaaccess::RuntimeConfig& cfg, const std::string& qr_url,
    const std::string& online_hint) {
  if (app::State::Instance().operation_mode() != viaaccess::OperationMode::kContingency) {
    return {viaaccess::ScanPath::kBlocked, BlockedByStalePolicy(online_hint)};
  }

  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  viaaccess::VerifyInput input;
  input.qr_url = qr_url;
  input.access_point_slug = cfg.access_point_slug;
  input.policy = app::State::Instance().policy();
  input.nonce = &contingency_store::Nonce();
  input.now = now;

  const viaaccess::VerifyResult verify = viaaccess::Verify(input);
  if (!verify.ok) {
    viaaccess::RedeemResult result;
    result.ok = false;
    result.status = 503;
    result.data.error = verify.error;
    result.data.code = verify.code;
    return {viaaccess::ScanPath::kContingency, result};
  }

  viaaccess::OutboxEvent event;
  event.intent_id = verify.intent_id;
  event.member_id = verify.member_id;
  event.access_point_slug = cfg.access_point_slug;
  event.qr_url = qr_url;
  event.scanned_at = now;
  contingency_store::EnqueueOutbox(std::move(event));

  viaaccess::RedeemResult result;
  result.ok = true;
  result.data.member_id = verify.member_id;
  result.data.correlation_outcome = "AUTHORIZED";
  result.data.access_point_slug = cfg.access_point_slug;
  return {viaaccess::ScanPath::kContingency, result};
}

struct Executed {
  viaaccess::ScanPath path = viaaccess::ScanPath::kBlocked;
  viaaccess::RedeemResult result;
  bool relay_attempted = false;
  esp_err_t relay_err = ESP_OK;
  bool unlock_attempted = false;
  identity::UnlockWebhookResult unlock;
};

Executed Execute(const viaaccess::RuntimeConfig& cfg, const std::string& qr_url) {
  Executed executed;
  executed.result =
      identity::RedeemQrUrl(cfg, qr_url, cfg.contingency.online_redeem_timeout_ms);

  if (executed.result.ok) {
    executed.path = viaaccess::ScanPath::kOnline;
  } else if (executed.result.status == 0 || executed.result.status >= 500) {
    const auto contingency = TryContingency(
        cfg, qr_url,
        executed.result.status == 0 ? "Rede indisponível para redeem online."
                                    : executed.result.data.error);
    executed.path = contingency.first;
    executed.result = contingency.second;
  } else {
    executed.path = viaaccess::ScanPath::kBlocked;
  }

  if (viaaccess::ShouldPostUnlock(cfg, executed.result)) {
    identity::UnlockWebhookPayload payload;
    payload.member_id = executed.result.data.member_id;
    payload.validation_id = executed.result.data.validation_id;
    payload.detection_id = executed.result.data.detection_id;
    payload.correlation_outcome = executed.result.data.correlation_outcome;
    payload.access_point_slug = executed.result.data.access_point_slug;
    executed.unlock_attempted = true;
    executed.unlock = identity::PostUnlockWebhook(cfg.unlock_webhook_url, payload, 8000);
    if (!executed.unlock.ok) {
      ESP_LOGW(kTag, "unlock webhook failed: %s", executed.unlock.error.c_str());
    }
  }

  if (viaaccess::ShouldPulseRelay(cfg, executed.result)) {
    executed.relay_attempted = true;
    executed.relay_err = relay::Pulse();
  }

  app::State::Instance().RecordScan(executed.path, executed.result);
  ESP_LOGI(kTag, "[%s] %s", viaaccess::ScanPathString(executed.path),
           viaaccess::FormatLog(executed.result).c_str());

  if (viaaccess::IsBridgeAuthFailure(executed.result)) {
    app::State::Instance().EnterSetupMode("redeem rejected device key");
  }
  return executed;
}

std::string ResponseBody(const Executed& executed) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", executed.result.ok);
  cJSON_AddStringToObject(root, "scanPath", viaaccess::ScanPathString(executed.path));

  cJSON* redeem = cJSON_AddObjectToObject(root, "redeem");
  if (executed.result.ok) {
    cJSON_AddBoolToObject(redeem, "ok", executed.result.data.ok);
    cJSON_AddBoolToObject(redeem, "redeemed", executed.result.data.redeemed);
    cJSON_AddStringToObject(redeem, "validationId",
                            executed.result.data.validation_id.c_str());
    cJSON_AddStringToObject(redeem, "detectionId",
                            executed.result.data.detection_id.c_str());
    cJSON_AddStringToObject(redeem, "memberId", executed.result.data.member_id.c_str());
    cJSON_AddStringToObject(redeem, "correlationOutcome",
                            executed.result.data.correlation_outcome.c_str());
    cJSON_AddStringToObject(redeem, "accessPointSlug",
                            executed.result.data.access_point_slug.c_str());
  } else {
    cJSON_AddStringToObject(redeem, "error", executed.result.data.error.c_str());
    cJSON_AddStringToObject(redeem, "code", executed.result.data.code.c_str());
  }

  if (executed.unlock_attempted) {
    cJSON* unlock = cJSON_AddObjectToObject(root, "unlock");
    cJSON_AddBoolToObject(unlock, "ok", executed.unlock.ok);
    if (executed.unlock.status > 0) {
      cJSON_AddNumberToObject(unlock, "status", executed.unlock.status);
    }
    if (!executed.unlock.error.empty()) {
      cJSON_AddStringToObject(unlock, "error", executed.unlock.error.c_str());
    }
  }

  if (executed.relay_attempted) {
    cJSON* relay_json = cJSON_AddObjectToObject(root, "relay");
    const bool relay_ok = executed.relay_err == ESP_OK;
    cJSON_AddBoolToObject(relay_json, "ok", relay_ok);
    if (!relay_ok) {
      cJSON_AddStringToObject(relay_json, "error", esp_err_to_name(executed.relay_err));
    }
  }

  return PrintAndDelete(root);
}

}  // namespace

HttpResult HandleHttpRequest(const std::string& raw_body,
                             const std::string& webhook_secret_header) {
  const viaaccess::RuntimeConfig cfg = app::State::Instance().config();

  if (!cfg.configured) {
    return {503, ErrorBody("Appliance em modo setup. Provisione em /setup antes de escanear.",
                           "SETUP_REQUIRED")};
  }
  if (!cfg.webhook_secret.empty() && webhook_secret_header != cfg.webhook_secret) {
    return {401, ErrorBody("Webhook não autorizado.")};
  }

  const std::string qr_url = ExtractQrUrl(raw_body);
  if (qr_url.empty()) {
    return {400, ErrorBody("Informe qrUrl, qr ou payload com a URL do QR.")};
  }

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const int64_t now_ms = NowMs();
    if (viaaccess::ShouldIgnore(g_debounce, qr_url, now_ms, cfg.debounce_ms)) {
      cJSON* root = cJSON_CreateObject();
      cJSON_AddBoolToObject(root, "ok", true);
      cJSON_AddBoolToObject(root, "ignored", true);
      cJSON_AddStringToObject(root, "reason", "debounce");
      return {200, PrintAndDelete(root)};
    }
    viaaccess::Mark(g_debounce, qr_url, now_ms);
  }

  const Executed executed = Execute(cfg, qr_url);
  return {viaaccess::HttpStatusForScan(executed.result), ResponseBody(executed)};
}

void HandleReaderLine(const std::string& line) {
  const std::string qr_url = viaaccess::Trim(line);
  if (qr_url.empty()) {
    return;
  }

  const viaaccess::RuntimeConfig cfg = app::State::Instance().config();
  if (!cfg.configured) {
    ESP_LOGW(kTag, "scan ignored, appliance in setup mode");
    return;
  }

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const int64_t now_ms = NowMs();
    if (viaaccess::ShouldIgnore(g_debounce, qr_url, now_ms, cfg.debounce_ms)) {
      ESP_LOGI(kTag, "scan ignored by debounce");
      return;
    }
    viaaccess::Mark(g_debounce, qr_url, now_ms);
  }

  Execute(cfg, qr_url);
}

}  // namespace scan_service
