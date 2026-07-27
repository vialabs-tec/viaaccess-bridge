// Debounced Request-to-Exit state machine, shared by the GPIO and simulated
// drivers. Mirrors internal/exitbutton in the Go agent: a press edge fires once
// when armed and outside the cooldown; release re-arms; a button stuck at boot
// is seeded without unlocking.
#pragma once

#include <cstdint>

namespace viaaccess {

enum class ExitButtonState {
  kUnknown,
  kIdle,
  kPressed,
};

const char* ExitButtonStateString(ExitButtonState state);

struct ExitButtonStep {
  bool emit_pressed = false;
};

class ExitButtonEngine {
 public:
  void Reset(int debounce_ms, int cooldown_ms);

  // Seed records the first reading without emitting. A stuck button must not
  // unlock on boot; armed stays false until the first release.
  void Seed(bool pressed, int64_t now_ms);

  ExitButtonStep ApplyRaw(bool pressed, int64_t now_ms);
  ExitButtonStep Tick(int64_t now_ms);

  ExitButtonState stable() const { return stable_; }

 private:
  ExitButtonStep Commit(bool pressed, int64_t now_ms);

  int debounce_ms_ = 50;
  int cooldown_ms_ = 3000;

  ExitButtonState stable_ = ExitButtonState::kUnknown;
  bool has_pending_ = false;
  bool pending_pressed_ = false;
  int64_t debounce_ends_ms_ = 0;

  bool armed_ = true;
  int64_t cooldown_until_ms_ = 0;
  bool cooldown_active_ = false;
};

}  // namespace viaaccess
