#include "viaaccess/status_led.hpp"

namespace viaaccess {

LedPattern PatternForMode(OperationMode mode) {
  switch (mode) {
    case OperationMode::kOnline:
      return LedPattern{false, true, false, false, "ONLINE"};
    case OperationMode::kSyncStale:
      return LedPattern{true, false, false, false, "SYNC_STALE"};
    case OperationMode::kContingency:
      return LedPattern{true, false, false, true, "CONTINGENCY"};
    case OperationMode::kSetup:
      return LedPattern{false, false, true, true, "SETUP"};
  }
  return LedPattern{true, false, false, true, "UNKNOWN"};
}

}  // namespace viaaccess
