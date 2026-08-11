// Status RGB pattern selection from OperationMode (WS2812 or KY-016).
//
// Mirrors internal/statusled/pattern.go: ONLINE solid green, SYNC_STALE solid
// red, SETUP blink blue, CONTINGENCY blink red. The GPIO driver lives in main;
// this module is host-testable and free of ESP-IDF.
#pragma once

#include "viaaccess/mode.hpp"

namespace viaaccess {

struct LedPattern {
  bool red = false;
  bool green = false;
  bool blue = false;
  bool blink = false;
  const char* name = "UNKNOWN";
};

LedPattern PatternForMode(OperationMode mode);

}  // namespace viaaccess
