// Background workers that keep the appliance in sync with Identity.
//
// Ports the two goroutines of internal/server/app.go: a 60 s policy and
// device-config loop, and a command loop whose cadence follows the pollAfterMs
// hint Identity returns.
#pragma once

namespace sync_task {

// Start is idempotent and only has an effect once the appliance is provisioned.
void Start();

// KickNow wakes the policy worker so a just-saved claim or a fresh GOT_IP does
// not wait for the 60 s cadence. Safe to call before Start().
void KickNow();

}  // namespace sync_task
