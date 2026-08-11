// DevKit BOOT (GPIO 0) multi-gesture recognition — host-testable, no ESP-IDF.
//
// Gestures are mutually exclusive in time: short presses accumulate into
// single / double / triple clicks; holding past the long-press thresholds
// cancels the click path.
#pragma once

#include <cstdint>

namespace viaaccess {

enum class ServiceGesture {
  kNone,
  kSingleClick,
  kDoubleClick,
  kTripleClick,
  // Crossed the arm threshold while still held (feedback cue, once per hold).
  kLongPressArmed,
  kLongPress,
};

struct ServiceButtonConfig {
  int debounce_ms = 40;
  // After a short release, wait this long for another click before firing.
  int click_window_ms = 400;
  // Hold this long to commit factory reset.
  int long_press_ms = 5000;
  // Hold this long (still pressed) to warn that a long press is in progress.
  int long_press_arm_ms = 2000;
};

class ServiceButtonEngine {
 public:
  void Reset(const ServiceButtonConfig& cfg = {});

  // Feed raw active-low or already-normalized "pressed" samples.
  ServiceGesture ApplyRaw(bool pressed, int64_t now_ms);

 private:
  ServiceGesture CommitClicks();

  ServiceButtonConfig cfg_{};

  bool has_stable_ = false;
  bool stable_pressed_ = false;

  bool has_pending_ = false;
  bool pending_pressed_ = false;
  int64_t debounce_ends_ms_ = 0;

  bool holding_ = false;
  int64_t hold_started_ms_ = 0;
  bool armed_emitted_ = false;
  bool long_press_emitted_ = false;

  int click_count_ = 0;
  bool waiting_clicks_ = false;
  int64_t click_deadline_ms_ = 0;
};

}  // namespace viaaccess
