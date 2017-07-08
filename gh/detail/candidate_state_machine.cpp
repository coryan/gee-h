#include "gh/detail/candidate_state_machine.hpp"
#include <gh/log.hpp>

namespace gh {
namespace detail {

std::ostream& operator<<(std::ostream& os, candidate_state x) {
  char const* values[] = {
      "constructing",
      "connecting",
      "connected",
      "creating_node",
      "updating_node",
      "node_created",
      "querying_predecessor",
      "predecessor_fetched",
      "creating_watcher",
      "watcher_created",
      "waiting_for_watcher_read",
      "resigning",
      "resigned",
      "shutting_down",
      "shutdown",
  };
  return os << values[int(x)];
}

candidate_state_machine::candidate_state_machine()
    : mu_()
    , state_(candidate_state::constructing) {
}

candidate_state candidate_state_machine::current() const {
  std::lock_guard<std::mutex> lock(mu_);
  return state_;
}

bool candidate_state_machine::change_state(char const* where, candidate_state nstate) {
  std::lock_guard<std::mutex> lock(mu_);
  if (not check_change_state(where, nstate)) {
    return false;
  }
  state_ = nstate;
  return true;
}

bool candidate_state_machine::check_change_state(char const* where, candidate_state nstate) const {
  using s = candidate_state;
  switch (state_) {
  case s::shutdown:
    break;
  case s::shutting_down:
    return nstate == s::shutdown;
  case s::resigned:
    return nstate == s::shutting_down;
  case s::resigning:
    return nstate == s::resigned or nstate == s::shutting_down;
  case s::waiting_for_watcher_read:
    return nstate == s::waiting_for_watcher_read or nstate == s::resigning or nstate == s::shutting_down;
  case s::watcher_created:
    return nstate == s::waiting_for_watcher_read or nstate == s::resigning or nstate == s::shutting_down;
  case s::creating_watcher:
    return nstate == s::watcher_created or nstate == s::resigning or nstate == s::shutting_down;
  case s::predecessor_fetched:
    return nstate == s::creating_watcher or nstate == s::shutting_down or nstate == s::resigning;
  case s::querying_predecessor:
    return nstate == s::predecessor_fetched or nstate == s::shutting_down or nstate == s::resigning;
  case s::node_created:
    return nstate == s::querying_predecessor or nstate == s::shutting_down or nstate == s::resigning;
  case s::updating_node:
    return nstate == s::node_created or nstate == s::shutting_down or nstate == s::resigning;
  case s::creating_node:
    return nstate == s::updating_node or nstate == s::node_created or nstate == s::shutting_down or
           nstate == s::resigning;
  case s::connected:
    return nstate == s::creating_node or nstate == s::shutting_down or nstate == s::resigning;
  case s::connecting:
    return nstate == s::connected or nstate == s::shutting_down or nstate == s::resigning;
  case s::constructing:
    return nstate == s::connecting;
  }
  return false;
}

} // namespace detail
} // namespace gh
