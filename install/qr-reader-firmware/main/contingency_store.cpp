#include "contingency_store.hpp"

#include <mutex>
#include <unordered_map>
#include <vector>

#include "cJSON.h"
#include "esp_log.h"
#include "storage.hpp"
#include "viaaccess/time.hpp"

namespace contingency_store {
namespace {

constexpr const char* kTag = "contingency";

class PersistentNonceStore : public viaaccess::NonceStore {
 public:
  bool IsConsumed(const std::string& intent_id) const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return memory_.IsConsumed(intent_id);
  }

  bool MarkConsumed(const std::string& intent_id, int64_t now_unix) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!memory_.MarkConsumed(intent_id, now_unix)) {
      return false;
    }
    return PersistLocked() == ESP_OK;
  }

  esp_err_t LoadFromDisk() {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string json = storage::LoadNonceStore();
    std::unordered_map<std::string, int64_t> consumed;
    if (!json.empty()) {
      cJSON* root = cJSON_Parse(json.c_str());
      if (root == nullptr) {
        ESP_LOGW(kTag, "nonce store is not valid JSON, starting empty");
      } else {
        const cJSON* map = cJSON_GetObjectItemCaseSensitive(root, "consumed");
        if (cJSON_IsObject(map)) {
          const cJSON* item = nullptr;
          cJSON_ArrayForEach(item, map) {
            if (item->string == nullptr) {
              continue;
            }
            if (cJSON_IsString(item) && item->valuestring != nullptr) {
              const int64_t at = viaaccess::ParseRfc3339(item->valuestring);
              if (at > 0) {
                consumed[item->string] = at;
              }
            } else if (cJSON_IsNumber(item)) {
              consumed[item->string] = static_cast<int64_t>(item->valuedouble);
            }
          }
        }
        cJSON_Delete(root);
      }
    }
    memory_.Load(std::move(consumed));
    return ESP_OK;
  }

 private:
  esp_err_t PersistLocked() {
    cJSON* root = cJSON_CreateObject();
    cJSON* consumed = cJSON_AddObjectToObject(root, "consumed");
    for (const auto& entry : memory_.consumed()) {
      const std::string at = viaaccess::FormatRfc3339(entry.second);
      cJSON_AddStringToObject(consumed, entry.first.c_str(), at.c_str());
    }
    char* printed = cJSON_PrintUnformatted(root);
    std::string body = printed != nullptr ? printed : "{}";
    cJSON_free(printed);
    cJSON_Delete(root);
    return storage::SaveNonceStore(body);
  }

  mutable std::mutex mutex_;
  viaaccess::MemoryNonceStore memory_;
};

std::mutex g_outbox_mutex;
viaaccess::OutboxStore g_outbox;
PersistentNonceStore g_nonce;

esp_err_t PersistOutboxLocked() {
  cJSON* root = cJSON_CreateObject();
  cJSON* events = cJSON_AddArrayToObject(root, "events");
  for (const viaaccess::OutboxEvent& event : g_outbox.events()) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "intentId", event.intent_id.c_str());
    cJSON_AddStringToObject(item, "memberId", event.member_id.c_str());
    cJSON_AddStringToObject(item, "accessPointSlug", event.access_point_slug.c_str());
    if (!event.qr_url.empty()) {
      cJSON_AddStringToObject(item, "qrUrl", event.qr_url.c_str());
    }
    cJSON_AddStringToObject(item, "scannedAt",
                            viaaccess::FormatRfc3339(event.scanned_at).c_str());
    cJSON_AddItemToArray(events, item);
  }
  if (g_outbox.last_flush_at() > 0) {
    cJSON_AddStringToObject(root, "lastFlushAt",
                            viaaccess::FormatRfc3339(g_outbox.last_flush_at()).c_str());
  }
  char* printed = cJSON_PrintUnformatted(root);
  std::string body = printed != nullptr ? printed : "{}";
  cJSON_free(printed);
  cJSON_Delete(root);
  return storage::SaveOutbox(body);
}

}  // namespace

esp_err_t Load() {
  ESP_ERROR_CHECK_WITHOUT_ABORT(g_nonce.LoadFromDisk());

  std::lock_guard<std::mutex> lock(g_outbox_mutex);
  const std::string json = storage::LoadOutbox();
  std::vector<viaaccess::OutboxEvent> events;
  int64_t last_flush_at = 0;
  if (!json.empty()) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (root == nullptr) {
      ESP_LOGW(kTag, "outbox is not valid JSON, starting empty");
    } else {
      const cJSON* flush = cJSON_GetObjectItemCaseSensitive(root, "lastFlushAt");
      if (cJSON_IsString(flush) && flush->valuestring != nullptr) {
        last_flush_at = viaaccess::ParseRfc3339(flush->valuestring);
      }
      const cJSON* arr = cJSON_GetObjectItemCaseSensitive(root, "events");
      if (cJSON_IsArray(arr)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, arr) {
          if (!cJSON_IsObject(item)) {
            continue;
          }
          viaaccess::OutboxEvent event;
          const cJSON* intent = cJSON_GetObjectItemCaseSensitive(item, "intentId");
          const cJSON* member = cJSON_GetObjectItemCaseSensitive(item, "memberId");
          const cJSON* ap = cJSON_GetObjectItemCaseSensitive(item, "accessPointSlug");
          const cJSON* qr = cJSON_GetObjectItemCaseSensitive(item, "qrUrl");
          const cJSON* at = cJSON_GetObjectItemCaseSensitive(item, "scannedAt");
          if (cJSON_IsString(intent) && intent->valuestring != nullptr) {
            event.intent_id = intent->valuestring;
          }
          if (cJSON_IsString(member) && member->valuestring != nullptr) {
            event.member_id = member->valuestring;
          }
          if (cJSON_IsString(ap) && ap->valuestring != nullptr) {
            event.access_point_slug = ap->valuestring;
          }
          if (cJSON_IsString(qr) && qr->valuestring != nullptr) {
            event.qr_url = qr->valuestring;
          }
          if (cJSON_IsString(at) && at->valuestring != nullptr) {
            event.scanned_at = viaaccess::ParseRfc3339(at->valuestring);
          }
          if (!event.intent_id.empty() && !event.member_id.empty()) {
            events.push_back(std::move(event));
          }
        }
      }
      cJSON_Delete(root);
    }
  }
  g_outbox.Load(std::move(events), last_flush_at);
  ESP_LOGI(kTag, "loaded contingency stores (outbox pending=%d)",
           g_outbox.PendingCount());
  return ESP_OK;
}

viaaccess::NonceStore& Nonce() { return g_nonce; }

void EnqueueOutbox(viaaccess::OutboxEvent event) {
  std::lock_guard<std::mutex> lock(g_outbox_mutex);
  g_outbox.Enqueue(std::move(event));
  const esp_err_t saved = PersistOutboxLocked();
  if (saved != ESP_OK) {
    ESP_LOGW(kTag, "outbox persist failed: %s", esp_err_to_name(saved));
  }
}

std::vector<viaaccess::OutboxEvent> PendingOutboxEvents() {
  std::lock_guard<std::mutex> lock(g_outbox_mutex);
  return g_outbox.PendingEvents();
}

esp_err_t MarkOutboxFlushed(int64_t now_unix) {
  std::lock_guard<std::mutex> lock(g_outbox_mutex);
  g_outbox.MarkFlushed(now_unix);
  return PersistOutboxLocked();
}

int OutboxPendingCount() {
  std::lock_guard<std::mutex> lock(g_outbox_mutex);
  return g_outbox.PendingCount();
}

int64_t OutboxLastFlushAt() {
  std::lock_guard<std::mutex> lock(g_outbox_mutex);
  return g_outbox.last_flush_at();
}

}  // namespace contingency_store
