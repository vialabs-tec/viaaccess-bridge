// Command types queued by Identity for the appliance to poll.
//
// Identity spells them in upper case (`UNLOCK`, `UPDATE`) and the Go agent matches
// with ToUpper, so the wire format is case insensitive in practice. Comparing it
// case sensitively is what once made the admin's "Abrir porta" ack as unsupported
// without ever pulsing the relay, which is why the mapping lives here with tests
// instead of inline in the polling task.
#pragma once

#include <string>

namespace viaaccess {

enum class CommandAction {
  kUnknown,
  kUnlock,
  kReboot,
  kSync,
  kReset,
  // Accepted and answered explicitly: the A/B partitions exist but this firmware
  // does not download images yet, so the admin gets a failed ack instead of a
  // command that looks queued forever.
  kUpdate,
};

CommandAction ParseCommandAction(const std::string& type);

}  // namespace viaaccess
