#include "check.hpp"
#include "viaaccess/status_led.hpp"

using viaaccess::LedPattern;
using viaaccess::OperationMode;
using viaaccess::PatternForMode;

VA_TEST(StatusLedPatternOnlineIsSolidGreen) {
  const LedPattern p = PatternForMode(OperationMode::kOnline);
  CHECK(!p.red);
  CHECK(p.green);
  CHECK(!p.blue);
  CHECK(!p.blink);
  CHECK_EQ(std::string(p.name), "ONLINE");
}

VA_TEST(StatusLedPatternSyncStaleIsSolidRed) {
  const LedPattern p = PatternForMode(OperationMode::kSyncStale);
  CHECK(p.red);
  CHECK(!p.green);
  CHECK(!p.blue);
  CHECK(!p.blink);
  CHECK_EQ(std::string(p.name), "SYNC_STALE");
}

VA_TEST(StatusLedPatternSetupBlinksBlue) {
  const LedPattern p = PatternForMode(OperationMode::kSetup);
  CHECK(!p.red);
  CHECK(!p.green);
  CHECK(p.blue);
  CHECK(p.blink);
  CHECK_EQ(std::string(p.name), "SETUP");
}

VA_TEST(StatusLedPatternContingencyBlinksRed) {
  const LedPattern p = PatternForMode(OperationMode::kContingency);
  CHECK(p.red);
  CHECK(!p.green);
  CHECK(!p.blue);
  CHECK(p.blink);
  CHECK_EQ(std::string(p.name), "CONTINGENCY");
}
