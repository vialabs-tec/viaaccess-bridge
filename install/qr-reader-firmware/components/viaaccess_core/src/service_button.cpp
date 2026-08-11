#include "viaaccess/service_button.hpp"

namespace viaaccess {

void ServiceButtonEngine::Reset(const ServiceButtonConfig& cfg) {
  cfg_ = cfg;
  if (cfg_.debounce_ms < 0) {
    cfg_.debounce_ms = 0;
  }
  if (cfg_.click_window_ms < 50) {
    cfg_.click_window_ms = 50;
  }
  if (cfg_.long_press_ms < cfg_.long_press_arm_ms) {
    cfg_.long_press_ms = cfg_.long_press_arm_ms;
  }
  if (cfg_.long_press_arm_ms < 0) {
    cfg_.long_press_arm_ms = 0;
  }

  has_stable_ = false;
  stable_pressed_ = false;
  has_pending_ = false;
  pending_pressed_ = false;
  debounce_ends_ms_ = 0;
  holding_ = false;
  hold_started_ms_ = 0;
  armed_emitted_ = false;
  long_press_emitted_ = false;
  click_count_ = 0;
  waiting_clicks_ = false;
  click_deadline_ms_ = 0;
}

ServiceGesture ServiceButtonEngine::CommitClicks() {
  waiting_clicks_ = false;
  const int n = click_count_;
  click_count_ = 0;
  if (n >= 3) {
    return ServiceGesture::kTripleClick;
  }
  if (n == 2) {
    return ServiceGesture::kDoubleClick;
  }
  if (n == 1) {
    return ServiceGesture::kSingleClick;
  }
  return ServiceGesture::kNone;
}

ServiceGesture ServiceButtonEngine::ApplyRaw(bool pressed, int64_t now_ms) {
  if (!has_stable_) {
    has_stable_ = true;
    stable_pressed_ = pressed;
    return ServiceGesture::kNone;
  }

  if (pressed != stable_pressed_) {
    if (!has_pending_ || pending_pressed_ != pressed) {
      has_pending_ = true;
      pending_pressed_ = pressed;
      debounce_ends_ms_ = now_ms + cfg_.debounce_ms;
    }
  } else {
    has_pending_ = false;
  }

  ServiceGesture from_hold = ServiceGesture::kNone;

  if (has_pending_ && now_ms >= debounce_ends_ms_) {
    has_pending_ = false;
    const bool next = pending_pressed_;
    if (next != stable_pressed_) {
      stable_pressed_ = next;

      if (stable_pressed_) {
        // New press: cancel an unfinished click window and start a hold.
        waiting_clicks_ = false;
        holding_ = true;
        hold_started_ms_ = now_ms;
        armed_emitted_ = false;
        long_press_emitted_ = false;
      } else if (holding_) {
        holding_ = false;
        if (!long_press_emitted_) {
          click_count_++;
          if (click_count_ >= 3) {
            return CommitClicks();
          }
          waiting_clicks_ = true;
          click_deadline_ms_ = now_ms + cfg_.click_window_ms;
        }
        // Long-press release: gesture already fired while held.
      }
    }
  }

  if (holding_ && !long_press_emitted_) {
    const int64_t held = now_ms - hold_started_ms_;
    if (!armed_emitted_ && held >= cfg_.long_press_arm_ms) {
      armed_emitted_ = true;
      from_hold = ServiceGesture::kLongPressArmed;
    }
    if (held >= cfg_.long_press_ms) {
      long_press_emitted_ = true;
      holding_ = false;
      waiting_clicks_ = false;
      click_count_ = 0;
      return ServiceGesture::kLongPress;
    }
  }

  if (from_hold != ServiceGesture::kNone) {
    return from_hold;
  }

  if (waiting_clicks_ && now_ms >= click_deadline_ms_) {
    return CommitClicks();
  }

  return ServiceGesture::kNone;
}

}  // namespace viaaccess
