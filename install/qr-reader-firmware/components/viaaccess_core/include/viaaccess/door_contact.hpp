// Debounced reed-switch state machine shared by the GPIO and simulated drivers.
//
// Mirrors internal/doorcontact in the Go agent: raw open/closed edges are held
// for debounce_ms before becoming stable, and a door that stays open past
// held_open_after_ms emits held_open once. Seed sets the initial position
// without posting events, so a boot with the door already open does not look
// like a forced entry.
#pragma once

#include <cstdint>

namespace viaaccess {

enum class DoorState {
  kUnknown,
  kOpen,
  kClosed,
};

enum class DoorKind {
  kOpened,
  kClosed,
  kHeldOpen,
};

const char* DoorStateString(DoorState state);
const char* DoorKindString(DoorKind kind);

// DoorContactStep carries the zero, one or two events a single ApplyRaw/Tick
// can produce (a debounce commit plus a held_open that fires in the same tick).
struct DoorContactStep {
  int count = 0;
  DoorKind kinds[2] = {};

  void Push(DoorKind kind) {
    if (count < 2) {
      kinds[count++] = kind;
    }
  }
};

class DoorContactEngine {
 public:
  void Reset(int debounce_ms, int held_open_after_ms);

  // Seed records the first reading without emitting. Call once after claiming
  // the pin (or starting the sim) so /health shows a real state immediately.
  void Seed(bool open, int64_t now_ms);

  DoorContactStep ApplyRaw(bool open, int64_t now_ms);
  DoorContactStep Tick(int64_t now_ms);

  DoorState stable() const { return stable_; }

 private:
  void Commit(bool open, int64_t now_ms, DoorContactStep* out);
  void ScheduleHeld(int64_t now_ms);
  void StopHeld();
  void FireHeldIfDue(int64_t now_ms, DoorContactStep* out);

  int debounce_ms_ = 50;
  int held_open_after_ms_ = 60000;

  DoorState stable_ = DoorState::kUnknown;
  bool has_pending_ = false;
  bool pending_open_ = false;
  int64_t debounce_ends_ms_ = 0;

  bool held_armed_ = false;
  bool held_fired_ = false;
  int64_t held_deadline_ms_ = 0;
};

}  // namespace viaaccess
