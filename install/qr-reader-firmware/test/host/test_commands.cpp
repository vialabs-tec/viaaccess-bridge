// Command type mapping. The cases that matter are the ones Identity actually
// sends, in upper case, since matching them case sensitively kept the admin's
// "Abrir porta" from reaching the relay.
#include "check.hpp"
#include "viaaccess/commands.hpp"

using viaaccess::CommandAction;
using viaaccess::ParseCommandAction;

VA_TEST(UnlockFromIdentityUpperCase) {
  CHECK(ParseCommandAction("UNLOCK") == CommandAction::kUnlock);
}

VA_TEST(UnlockToleratesCaseAndSurroundingSpace) {
  CHECK(ParseCommandAction("unlock") == CommandAction::kUnlock);
  CHECK(ParseCommandAction(" Unlock ") == CommandAction::kUnlock);
  CHECK(ParseCommandAction("open_door") == CommandAction::kUnlock);
}

VA_TEST(MaintenanceVerbsMapToTheirActions) {
  CHECK(ParseCommandAction("REBOOT") == CommandAction::kReboot);
  CHECK(ParseCommandAction("restart") == CommandAction::kReboot);
  CHECK(ParseCommandAction("SYNC") == CommandAction::kSync);
  CHECK(ParseCommandAction("resync") == CommandAction::kSync);
  CHECK(ParseCommandAction("RESET") == CommandAction::kReset);
  CHECK(ParseCommandAction("unprovision") == CommandAction::kReset);
}

// OTA is a distinct action so the ack says what is missing instead of the generic
// unsupported message: the partitions exist, the download path does not.
VA_TEST(UpdateIsRecognizedButSeparate) {
  CHECK(ParseCommandAction("UPDATE") == CommandAction::kUpdate);
}

VA_TEST(UnknownTypeStaysUnknown) {
  CHECK(ParseCommandAction("") == CommandAction::kUnknown);
  CHECK(ParseCommandAction("LOCK") == CommandAction::kUnknown);
  CHECK(ParseCommandAction("unlock_now") == CommandAction::kUnknown);
}
