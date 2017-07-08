#ifndef gh_detail_candidate_state_machine_hpp
#define gh_detail_candidate_state_machine_hpp

#include <iostream>
#include <mutex>

namespace gh {
namespace detail {
/**
 * Represent the state machine for a etcd session.
 *
 * A @c session is an abstraction to create and maintain etcd Leases.  The class uses a state machine to represent
 * the possible transitions based on both asynchronous events from the etcd server, as well as local member function
 * calls from the application.  This enum makes the state machine explicit.  Its main purpose is to help us debug the
 * state machine transitions through logging, and to ignore requests that are invalid once certain states are reached.
 */
enum class candidate_state {
  /// Initial state
  constructing,
  /// Getting the bi-dir streaming RPC connection.
  connecting,
  /// Obtained the bi-dir streaming RPC connection.
  connected,
  /// Creating a node.
  creating_node,
  /// Updating an existing node.
  updating_node,
  /// Node obtained.
  node_created,
  /// Query existing nodes.
  querying_predecessor,
  /// Fetched other candidates.
  predecessor_fetched,
  /// Create watcher.
  creating_watcher,
  /// Watcher created.
  watcher_created,
  /// Waiting for watcher read.
  waiting_for_watcher_read,
  /// Resigning from the election (delete node).
  resigning,
  /// Resigned from the election (deleted node).
  resigned,
  /// Starting shutdown of local resources.
  shutting_down,
  /// Final state, shutdown complete.
  shutdown,
};

/**
 * The streaming operator for @c candidate_state.
 *
 * Mostly used for unit testing and debugging / logging messages.
 */
std::ostream& operator<<(std::ostream& os, candidate_state x);

/**
 * Implement the state machine for an etcd leader election candidate.
 *
 * This class is used to model the state machine for an etcd leader election candidate.  The idea is to have a small
 * place to look at valid vs. invalid transitions and to centralize debug logging.
 */
class candidate_state_machine {
public:
  candidate_state_machine();

  /// Return the current state.
  candidate_state current() const;

  /// Propose a state change, returns true if accepted.
  bool change_state(char const* where, candidate_state nstate);

  /// Propose a state change, returns true and calls @a functor if accepted.
  template <typename Functor>
  bool change_state_action(char const* where, candidate_state nstate, Functor& functor) {
    std::lock_guard<std::mutex> lock(mu_);
    if (not check_change_state(where, nstate)) {
      return false;
    }
    functor();
    state_ = nstate;
    return true;
  }

private:
  /// Checks if a state transition is acceptable.
  bool check_change_state(char const* where, candidate_state nstate) const;

private:
  mutable std::mutex mu_;
  candidate_state state_;
};

} // namespace detail
} // namespace gh

#endif // gh_detail_candidate_state_machine_hpp
