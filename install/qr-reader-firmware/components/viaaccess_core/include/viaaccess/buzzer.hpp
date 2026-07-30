// Active-buzzer feedback tones. Timing lives here so host tests can lock the
// UX without GPIO; the firmware driver plays the plan.
//
// Primary use: held_open alarm when the reed stays open past the grace window.
// Secondary: short success/fail cues on scan and REX.
#pragma once

namespace viaaccess {

enum class BeepKind {
  kSuccess,
  kFail,
  // Repeating cadence for "door held open too long". Driver loops until Stop.
  kHeldOpen,
  kStop,
};

struct BeepPlan {
  int on_ms = 0;
  int off_ms = 0;
  // One cycle of the pattern. Held-open uses pulses=1 and the driver repeats.
  int pulses = 0;
};

BeepPlan PlanForBeep(BeepKind kind);

}  // namespace viaaccess
