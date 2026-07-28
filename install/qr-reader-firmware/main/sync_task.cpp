#include "sync_task.hpp"

#include <atomic>
#include <string>

#include "app_state.hpp"
#include "clock_service.hpp"
#include "contingency_store.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "identity_client.hpp"
#include "relay.hpp"
#include "storage.hpp"
#include "viaaccess/commands.hpp"
#include "viaaccess/poll.hpp"
#include "wifi_manager.hpp"

#include <ctime>
#include <vector>

namespace sync_task {
namespace {

constexpr const char* kTag = "sync";

std::atomic<bool> g_started{false};

// HandleUnauthorized is the shared reaction to a revoked device key: Identity
// answered 401 or BRIDGE_DISABLED, so the appliance drops its credentials and
// reopens /setup instead of retrying with a key that will never work again.
bool HandleUnauthorized(const identity::Outcome& outcome, const char* what) {
  if (!outcome.unauthorized) {
    return false;
  }
  ESP_LOGE(kTag, "%s rejected the device key, returning to setup", what);
  app::State::Instance().EnterSetupMode(what);
  return true;
}

void SyncPolicy(const viaaccess::RuntimeConfig& cfg) {
  identity::PolicyFetch fetch = identity::FetchPolicySnapshot(cfg);
  if (HandleUnauthorized(fetch.outcome, "policy sync")) {
    return;
  }
  if (!fetch.outcome.ok) {
    app::State::Instance().set_identity_reachable(false);
    ESP_LOGW(kTag, "policy sync failed: %s", fetch.outcome.error.c_str());
    return;
  }

  app::State::Instance().set_identity_reachable(true);
  app::State::Instance().set_policy(fetch.policy);
  const esp_err_t saved = storage::SavePolicySnapshot(fetch.raw_json);
  if (saved != ESP_OK) {
    ESP_LOGW(kTag, "policy snapshot not persisted: %s", esp_err_to_name(saved));
  }
  ESP_LOGI(kTag, "policy synced (grantVersion=%s, members=%d)",
           fetch.policy.grant_version.c_str(), fetch.policy.member_grant_count);
}

void FlushContingencyOutbox(const viaaccess::RuntimeConfig& cfg) {
  const std::vector<viaaccess::OutboxEvent> pending =
      contingency_store::PendingOutboxEvents();
  if (pending.empty()) {
    return;
  }

  identity::FlushResult flush = identity::FlushOutbox(cfg, pending);
  if (HandleUnauthorized(flush.outcome, "contingency flush")) {
    return;
  }
  if (!flush.outcome.ok) {
    ESP_LOGW(kTag, "outbox flush failed: %s", flush.outcome.error.c_str());
    return;
  }
  // Match the Go agent: clear the local queue when Identity accepted at least
  // one event (skipped rows are already recorded server-side).
  if (flush.flushed > 0) {
    const esp_err_t cleared =
        contingency_store::MarkOutboxFlushed(static_cast<int64_t>(std::time(nullptr)));
    if (cleared != ESP_OK) {
      ESP_LOGW(kTag, "outbox clear failed after flush: %s", esp_err_to_name(cleared));
    } else {
      ESP_LOGI(kTag, "outbox flushed: %d skipped=%d", flush.flushed, flush.skipped);
    }
  }
}

void SyncDeviceConfig(const viaaccess::RuntimeConfig& cfg) {
  app::State& state = app::State::Instance();
  identity::DeviceConfigFetch fetch =
      identity::FetchDeviceConfig(cfg, state.device_config_etag());
  if (HandleUnauthorized(fetch.outcome, "device-config")) {
    return;
  }
  if (!fetch.outcome.ok) {
    ESP_LOGW(kTag, "device-config failed: %s", fetch.outcome.error.c_str());
    return;
  }
  if (!fetch.etag.empty()) {
    state.set_device_config_etag(fetch.etag);
  }
  if (fetch.not_modified) {
    return;
  }

  // A remotely disabled bridge is not a revoked key: keep the credentials and
  // let /health report the reason, so re-enabling in the dashboard is enough.
  if (!fetch.config.enabled) {
    ESP_LOGW(kTag, "bridge disabled in Identity, refusing scans until re-enabled");
  }

  bool changed = false;
  viaaccess::RuntimeConfig updated =
      viaaccess::ApplyRemoteDeviceConfig(state.config(), fetch.config, &changed);
  if (!changed) {
    return;
  }
  const esp_err_t saved = state.SaveConfig(std::move(updated));
  if (saved != ESP_OK) {
    ESP_LOGE(kTag, "cannot persist remote config: %s", esp_err_to_name(saved));
    return;
  }
  ESP_LOGI(kTag, "device-config applied from Identity");
}

void PolicyLoop(void* /*argument*/) {
  // A fresh boot has nothing to answer with, so the first cycle runs
  // immediately instead of after the interval.
  for (;;) {
    const viaaccess::RuntimeConfig cfg = app::State::Instance().config();
    if (cfg.configured && wifi::connected()) {
      SyncPolicy(cfg);
      SyncDeviceConfig(app::State::Instance().config());
      FlushContingencyOutbox(app::State::Instance().config());
    }
    // Same cadence, off the HTTP thread: an I2C stall must not hold a /health
    // response, and enclosure temperature only needs minute resolution.
    clock_service::RefreshRtcTemperature();
    vTaskDelay(pdMS_TO_TICKS(viaaccess::kPolicySyncIntervalMs));
  }
}

// ExecuteCommand handles the remote maintenance verbs. OTA is acknowledged as
// unsupported for now: the partition table already reserves ota_0/ota_1, but the
// download and rollback path is a later step and silently ignoring the command
// would leave the dashboard waiting forever.
identity::Outcome ExecuteCommand(const viaaccess::RuntimeConfig& cfg,
                                 const identity::PendingCommand& command,
                                 bool* reboot_after_ack) {
  switch (viaaccess::ParseCommandAction(command.type)) {
    case viaaccess::CommandAction::kReboot:
      *reboot_after_ack = true;
      return identity::AckCommand(cfg, command.id, true, "");
    case viaaccess::CommandAction::kUnlock: {
      const esp_err_t pulsed = relay::Pulse();
      return identity::AckCommand(cfg, command.id, pulsed == ESP_OK,
                                  pulsed == ESP_OK ? "" : esp_err_to_name(pulsed));
    }
    case viaaccess::CommandAction::kSync:
      SyncPolicy(cfg);
      return identity::AckCommand(cfg, command.id, true, "");
    case viaaccess::CommandAction::kReset: {
      const identity::Outcome acked = identity::AckCommand(cfg, command.id, true, "");
      app::State::Instance().EnterSetupMode("reset command from Identity");
      return acked;
    }
    case viaaccess::CommandAction::kUpdate:
      ESP_LOGW(kTag, "command %s asks for OTA, which this firmware cannot apply yet",
               command.id.c_str());
      return identity::AckCommand(cfg, command.id, false,
                                  "OTA not supported by esp32 firmware yet");
    case viaaccess::CommandAction::kUnknown:
      break;
  }
  ESP_LOGW(kTag, "command %s of type %s is not supported by this firmware",
           command.id.c_str(), command.type.c_str());
  return identity::AckCommand(cfg, command.id, false,
                              "command type not supported by esp32 firmware");
}

void CommandLoop(void* /*argument*/) {
  int delay_ms = viaaccess::kCommandPollIdleMs;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    const viaaccess::RuntimeConfig cfg = app::State::Instance().config();
    if (!cfg.configured || !wifi::connected()) {
      delay_ms = viaaccess::kCommandPollIdleMs;
      continue;
    }

    identity::CommandsFetch fetch = identity::FetchCommands(cfg);
    if (HandleUnauthorized(fetch.outcome, "command poll")) {
      delay_ms = viaaccess::kCommandPollIdleMs;
      continue;
    }
    if (!fetch.outcome.ok) {
      // Back off to the ceiling while Identity is unreachable so a long outage
      // does not keep the radio busy.
      delay_ms = viaaccess::kCommandPollMaxMs;
      continue;
    }

    bool reboot_after_ack = false;
    for (const identity::PendingCommand& command : fetch.commands) {
      ESP_LOGI(kTag, "executing command %s (%s)", command.id.c_str(),
               command.type.c_str());
      const identity::Outcome acked = ExecuteCommand(cfg, command, &reboot_after_ack);
      if (!acked.ok) {
        ESP_LOGW(kTag, "ack for %s failed: %s", command.id.c_str(), acked.error.c_str());
      }
    }

    delay_ms = viaaccess::NextCommandPollDelayMs(
        fetch.poll_after_ms, static_cast<int>(fetch.commands.size()));

    if (reboot_after_ack) {
      ESP_LOGW(kTag, "rebooting on Identity command");
      vTaskDelay(pdMS_TO_TICKS(500));
      esp_restart();
    }
  }
}

}  // namespace

void Start() {
  if (g_started.exchange(true)) {
    return;
  }
  // Both stacks must hold esp_http_client plus a TLS session; the policy loop
  // gets the larger one because it also parses the whole snapshot.
  xTaskCreate(PolicyLoop, "va_policy", 8192, nullptr, 4, nullptr);
  xTaskCreate(CommandLoop, "va_commands", 6144, nullptr, 4, nullptr);
  ESP_LOGI(kTag, "sync workers started");
}

}  // namespace sync_task
