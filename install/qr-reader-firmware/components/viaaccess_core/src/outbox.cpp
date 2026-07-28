#include "viaaccess/outbox.hpp"

namespace viaaccess {

void OutboxStore::Enqueue(OutboxEvent event) { events_.push_back(std::move(event)); }

std::vector<OutboxEvent> OutboxStore::PendingEvents() const { return events_; }

void OutboxStore::MarkFlushed(int64_t now_unix) {
  events_.clear();
  last_flush_at_ = now_unix;
}

int OutboxStore::PendingCount() const { return static_cast<int>(events_.size()); }

void OutboxStore::Load(std::vector<OutboxEvent> events, int64_t last_flush_at) {
  events_ = std::move(events);
  last_flush_at_ = last_flush_at;
}

}  // namespace viaaccess
