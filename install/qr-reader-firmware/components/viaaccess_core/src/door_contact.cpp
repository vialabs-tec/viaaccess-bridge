#include "viaaccess/door_contact.hpp"

namespace viaaccess {

const char* DoorStateString(DoorState state) {
  switch (state) {
    case DoorState::kOpen:
      return "open";
    case DoorState::kClosed:
      return "closed";
    case DoorState::kUnknown:
      break;
  }
  return "unknown";
}

const char* DoorKindString(DoorKind kind) {
  switch (kind) {
    case DoorKind::kOpened:
      return "opened";
    case DoorKind::kClosed:
      return "closed";
    case DoorKind::kHeldOpen:
      return "held_open";
  }
  return "unknown";
}

void DoorContactEngine::Reset(int debounce_ms, int held_open_after_ms) {
  debounce_ms_ = debounce_ms > 0 ? debounce_ms : 50;
  held_open_after_ms_ = held_open_after_ms > 0 ? held_open_after_ms : 60000;
  stable_ = DoorState::kUnknown;
  has_pending_ = false;
  pending_open_ = false;
  debounce_ends_ms_ = 0;
  StopHeld();
}

void DoorContactEngine::Seed(bool open, int64_t now_ms) {
  has_pending_ = false;
  if (open) {
    stable_ = DoorState::kOpen;
    ScheduleHeld(now_ms);
  } else {
    stable_ = DoorState::kClosed;
    StopHeld();
  }
}

DoorContactStep DoorContactEngine::ApplyRaw(bool open, int64_t now_ms) {
  DoorContactStep out;

  if (has_pending_ && pending_open_ == open) {
    FireHeldIfDue(now_ms, &out);
    return out;
  }
  if (stable_ != DoorState::kUnknown) {
    const bool stable_open = stable_ == DoorState::kOpen;
    if (stable_open == open && !has_pending_) {
      FireHeldIfDue(now_ms, &out);
      return out;
    }
  }

  has_pending_ = true;
  pending_open_ = open;
  debounce_ends_ms_ = now_ms + debounce_ms_;
  if (debounce_ms_ == 0) {
    Commit(open, now_ms, &out);
  }
  FireHeldIfDue(now_ms, &out);
  return out;
}

DoorContactStep DoorContactEngine::Tick(int64_t now_ms) {
  DoorContactStep out;
  if (has_pending_ && now_ms >= debounce_ends_ms_) {
    Commit(pending_open_, now_ms, &out);
  }
  FireHeldIfDue(now_ms, &out);
  return out;
}

void DoorContactEngine::Commit(bool open, int64_t now_ms, DoorContactStep* out) {
  has_pending_ = false;
  const DoorState next = open ? DoorState::kOpen : DoorState::kClosed;
  if (stable_ == next) {
    return;
  }
  stable_ = next;
  StopHeld();
  out->Push(open ? DoorKind::kOpened : DoorKind::kClosed);
  if (open) {
    ScheduleHeld(now_ms);
  }
}

void DoorContactEngine::ScheduleHeld(int64_t now_ms) {
  held_armed_ = true;
  held_fired_ = false;
  held_deadline_ms_ = now_ms + held_open_after_ms_;
}

void DoorContactEngine::StopHeld() {
  held_armed_ = false;
  held_fired_ = false;
  held_deadline_ms_ = 0;
}

void DoorContactEngine::FireHeldIfDue(int64_t now_ms, DoorContactStep* out) {
  if (!held_armed_ || held_fired_ || stable_ != DoorState::kOpen) {
    return;
  }
  if (now_ms < held_deadline_ms_) {
    return;
  }
  held_fired_ = true;
  out->Push(DoorKind::kHeldOpen);
}

}  // namespace viaaccess
