// LittleFS-backed nonce and outbox stores for offline contingency.
#pragma once

#include <vector>

#include "esp_err.h"
#include "viaaccess/contingency.hpp"
#include "viaaccess/outbox.hpp"

namespace contingency_store {

// Load reads consumed intents and pending outbox events from LittleFS. Missing
// files are treated as empty stores.
esp_err_t Load();

viaaccess::NonceStore& Nonce();

// Outbox helpers take the store lock so scan and the sync loop can race safely.
void EnqueueOutbox(viaaccess::OutboxEvent event);
std::vector<viaaccess::OutboxEvent> PendingOutboxEvents();
esp_err_t MarkOutboxFlushed(int64_t now_unix);
int OutboxPendingCount();
int64_t OutboxLastFlushAt();

}  // namespace contingency_store
