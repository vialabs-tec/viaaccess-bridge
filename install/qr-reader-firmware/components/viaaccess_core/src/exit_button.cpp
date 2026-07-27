#include "viaaccess/exit_button.hpp"

namespace viaaccess {

const char* ExitButtonStateString(ExitButtonState state) {
  switch (state) {
    case ExitButtonState::kIdle:
      return "idle";
    case ExitButtonState::kPressed:
      return "pressed";
    case ExitButtonState::kUnknown:
      break;
  }
  return "unknown";
}

void ExitButtonEngine::Reset(int debounce_ms, int cooldown_ms) {
  // Debounce 0 means commit on the edge (tests); negative falls back to 50 ms.
  debounce_ms_ = debounce_ms < 0 ? 50 : debounce_ms;
  cooldown_ms_ = cooldown_ms > 0 ? cooldown_ms : 3000;
  stable_ = ExitButtonState::kUnknown;
  has_pending_ = false;
  pending_pressed_ = false;
  debounce_ends_ms_ = 0;
  armed_ = true;
  cooldown_until_ms_ = 0;
  cooldown_active_ = false;
}

void ExitButtonEngine::Seed(bool pressed, int64_t /*now_ms*/) {
  has_pending_ = false;
  if (pressed) {
    stable_ = ExitButtonState::kPressed;
    armed_ = false;
  } else {
    stable_ = ExitButtonState::kIdle;
    armed_ = true;
  }
}

ExitButtonStep ExitButtonEngine::ApplyRaw(bool pressed, int64_t now_ms) {
  if (has_pending_ && pending_pressed_ == pressed) {
    return {};
  }
  if (stable_ != ExitButtonState::kUnknown) {
    const bool stable_pressed = stable_ == ExitButtonState::kPressed;
    if (stable_pressed == pressed && !has_pending_) {
      return {};
    }
  }

  has_pending_ = true;
  pending_pressed_ = pressed;
  debounce_ends_ms_ = now_ms + debounce_ms_;
  if (debounce_ms_ == 0) {
    return Commit(pressed, now_ms);
  }
  return {};
}

ExitButtonStep ExitButtonEngine::Tick(int64_t now_ms) {
  if (!has_pending_ || now_ms < debounce_ends_ms_) {
    return {};
  }
  return Commit(pending_pressed_, now_ms);
}

ExitButtonStep ExitButtonEngine::Commit(bool pressed, int64_t now_ms) {
  has_pending_ = false;
  const ExitButtonState next =
      pressed ? ExitButtonState::kPressed : ExitButtonState::kIdle;
  if (stable_ == next) {
    return {};
  }
  stable_ = next;

  if (!pressed) {
    armed_ = true;
    return {};
  }

  // Press edge: fire only when armed and outside cooldown. Consume the edge
  // either way so a held button cannot fire later without a release.
  if (!armed_ || (cooldown_active_ && now_ms < cooldown_until_ms_)) {
    armed_ = false;
    return {};
  }
  armed_ = false;
  cooldown_active_ = true;
  cooldown_until_ms_ = now_ms + cooldown_ms_;
  return ExitButtonStep{.emit_pressed = true};
}

}  // namespace viaaccess
