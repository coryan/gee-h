#ifndef gh_session_state_hpp
#define gh_session_state_hpp

#include <iostream>

namespace gh {
/**
 * Represent the state machine for a etcd session.
 *
 * A @c session is an abstraction to create and maintain etcd Leases.  The class uses a state machine to represent
 * the possible transitions based on both asynchronous events from the etcd server, as well as local member function
 * calls from the application.  This enum makes the state machine explicit.  Its main purpose is to help us debug the
 * state machine transitions through logging, and to ignore requests that are invalid once certain states are reached.
 */
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
