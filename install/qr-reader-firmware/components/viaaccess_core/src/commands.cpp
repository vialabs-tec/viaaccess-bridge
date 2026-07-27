#include "viaaccess/commands.hpp"

#include "viaaccess/strings.hpp"

namespace viaaccess {

CommandAction ParseCommandAction(const std::string& type) {
  const std::string normalized = ToLower(Trim(type));
  if (normalized == "unlock" || normalized == "open_door") {
    return CommandAction::kUnlock;
  }
  if (normalized == "reboot" || normalized == "restart") {
    return CommandAction::kReboot;
  }
  if (normalized == "sync" || normalized == "resync") {
    return CommandAction::kSync;
  }
  if (normalized == "reset" || normalized == "unprovision") {
    return CommandAction::kReset;
  }
  if (normalized == "update") {
    return CommandAction::kUpdate;
  }
  return CommandAction::kUnknown;
}

}  // namespace viaaccess
