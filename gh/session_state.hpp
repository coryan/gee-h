#ifndef gh_session_state_hpp
#define gh_session_state_hpp

#include <iostream>

namespace gh {
enum class session_state {
  /**
   * Initial state.
   *
   * The initial state for a session.  After some lightweight initialization it transitions to the @c connecting state.
   */
  constructing,

  /**
   * The state while trying to obtain a new lease identifier.
   *
   * The state while the session is asking the etcd server for a new identifier.  Transitions to the @c connected state.
   */
  connecting,

  connected,
};

/**
 * The streaming operator for @c session_state.
 *
 * Mostly used for unit testing and debugging / logging messages.
 */
std::ostream& operator<<(std::ostream& os, session_state x);
} // namespace gh

#endif // gh_session_state_hpp
