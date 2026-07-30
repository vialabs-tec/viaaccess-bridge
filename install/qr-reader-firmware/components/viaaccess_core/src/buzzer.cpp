#include "viaaccess/buzzer.hpp"

namespace viaaccess {

BeepPlan PlanForBeep(BeepKind kind) {
  switch (kind) {
    case BeepKind::kSuccess:
      return BeepPlan{120, 0, 1};
    case BeepKind::kFail:
      return BeepPlan{90, 80, 2};
    case BeepKind::kHeldOpen:
      // One cycle: long beep + pause. The driver repeats until Stop / closed.
      return BeepPlan{400, 600, 1};
    case BeepKind::kStop:
      return BeepPlan{};
  }
  return BeepPlan{};
}

}  // namespace viaaccess
