#include "viaaccess/poll.hpp"

namespace viaaccess {

int NextCommandPollDelayMs(int poll_after_ms,
                           int command_count,
                           int idle_ms,
                           int fast_ms,
                           int max_ms) {
  int wait = poll_after_ms;
  if (wait < fast_ms) {
    wait = command_count > 0 ? fast_ms : idle_ms;
  }
  if (wait > max_ms) {
    wait = max_ms;
  }
  return wait;
}

}  // namespace viaaccess
