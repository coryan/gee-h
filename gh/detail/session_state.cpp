#include "gh/detail/session_state.hpp"

namespace gh {
namespace detail {

std::ostream& operator<<(std::ostream& os, session_state x) {
  char const* values[] = {
      "constructing",
      "connecting",
      "connected",
      "obtaining_lease",
      "lease_obtained",
      "waiting_for_timer",
      "waiting_for_keep_alive_read",
      "revoking",
      "revoked",
      "shutting_down",
      "shutdown",
  };
  return os << values[int(x)];
}

session_state_machine::session_state_machine()
    : mu_()
    , state_(session_state::constructing) {
}

session_state session_state_machine::current() const {
  std::lock_guard<std::mutex> lock(mu_);
  return state_;
}

bool session_state_machine::change_state(char const* where, session_state nstate) {
  std::lock_guard<std::mutex> lock(mu_);
  if (not check_change_state(where, nstate)) {
    return false;
  }
  state_ = nstate;
  return true;
}

bool session_state_machine::check_change_state(char const* where, session_state nstate) const {
  using s = session_state;
  switch (state_) {
  case s::shutdown:
    return false;
  case s::shutting_down:
    return nstate == s::shutdown;
  case s::revoked:
    return nstate == s::shutting_down;
  case s::revoking:
    return nstate == s::revoked or nstate == s::shutting_down;
  case s::waiting_for_keep_alive_read:
    return nstate == s::waiting_for_timer or nstate == s::revoking or nstate == s::shutting_down;
  case s::waiting_for_timer:
    return nstate == s::waiting_for_keep_alive_read or nstate == s::revoking or nstate == s::shutting_down;
  case s::lease_obtained:
    return nstate == s::waiting_for_timer or nstate == s::shutting_down or nstate == s::revoking;
  case s::obtaining_lease:
    return nstate == s::lease_obtained or nstate == s::shutting_down or nstate == s::revoking;
  case s::connected:
    return nstate == s::obtaining_lease or nstate == s::shutting_down or nstate == s::revoking;
  case s::connecting:
    return nstate == s::connected or nstate == s::shutting_down or nstate == s::revoking;
  case s::constructing:
    return nstate == s::connecting;
  }
  return true;
}

} // namespace detail
} // namespace gh
