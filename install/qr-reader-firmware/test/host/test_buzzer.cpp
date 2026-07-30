#include "check.hpp"
#include "viaaccess/buzzer.hpp"

namespace {

using viaaccess::BeepKind;
using viaaccess::PlanForBeep;

VA_TEST(BuzzerSuccessIsOneShortPulse) {
  const auto plan = PlanForBeep(BeepKind::kSuccess);
  CHECK_EQ(plan.pulses, 1);
  CHECK(plan.on_ms > 0);
  CHECK_EQ(plan.off_ms, 0);
}

VA_TEST(BuzzerFailIsTwoPulses) {
  const auto plan = PlanForBeep(BeepKind::kFail);
  CHECK_EQ(plan.pulses, 2);
  CHECK(plan.on_ms > 0);
  CHECK(plan.off_ms > 0);
}

VA_TEST(BuzzerHeldOpenIsLongerCadence) {
  const auto plan = PlanForBeep(BeepKind::kHeldOpen);
  CHECK_EQ(plan.pulses, 1);
  CHECK(plan.on_ms >= 300);
  CHECK(plan.off_ms >= 300);
  // Held-open cycle should be more insistent than a single success cue.
  CHECK(plan.on_ms + plan.off_ms > PlanForBeep(BeepKind::kSuccess).on_ms);
}

VA_TEST(BuzzerStopHasNoPulses) {
  const auto plan = PlanForBeep(BeepKind::kStop);
  CHECK_EQ(plan.pulses, 0);
}

}  // namespace
