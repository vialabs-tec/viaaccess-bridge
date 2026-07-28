// Offline passage outbox waiting for Identity flush, ported from
// internal/outbox in the Go agent.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace viaaccess {

struct OutboxEvent {
  std::string intent_id;
  std::string member_id;
  std::string access_point_slug;
  std::string qr_url;
  // Unix seconds (UTC).
  int64_t scanned_at = 0;
};

class OutboxStore {
 public:
  void Enqueue(OutboxEvent event);
  std::vector<OutboxEvent> PendingEvents() const;
  void MarkFlushed(int64_t now_unix);
  int PendingCount() const;
  int64_t last_flush_at() const { return last_flush_at_; }

  void Load(std::vector<OutboxEvent> events, int64_t last_flush_at);
  const std::vector<OutboxEvent>& events() const { return events_; }

 private:
  std::vector<OutboxEvent> events_;
  int64_t last_flush_at_ = 0;
};

}  // namespace viaaccess
