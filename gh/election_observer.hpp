#ifndef gh_election_observer_hpp
#define gh_election_observer_hpp

#include <functional>
#include <string>

namespace gh {

/**
 * Define interface for election monitor classes.
 */
class election_observer {
public:
  //@{
  /// @name type traits
  /**
   * Define the type used by subscribers.
   *
   * @param new_key the key of the new leader, an empty string if there is no longer a leader.
   * @param new_value the new value set by the leader.
   */
  using subscriber_type = std::function<void(std::string const& new_key,std::string const& new_value)>;
  //@}

  /// Destructor, can raise if deleting local gRPC resources fail.
  virtual ~election_observer() noexcept(false) = 0;

  /// Connects to the etcd server and starts watching the election.
  virtual void startup() = 0;

  /// Disconnects from the etcd server and stops watching the election.
  virtual void shutdown() = 0;

  /// Returns false if no elected has been elected.
  virtual bool has_leader() const = 0;

  /// The name of the election being observed.
  virtual std::string election_name() const = 0;

  /// Returns the etcd node associated with the current leader, an empty string if there is no leader.
  virtual std::string current_key() const = 0;

  /// Returns the value associated with the current leader, can be empty even if there is a leader.
  virtual std::string current_value() const = 0;

  /**
   * Notify @a subscriber when the leader key and/or value changes.
   *
   * Notice that the subscriber is always called with the current status upon subscription.
   * @param subscriber the function to call with subscription updates.
   * @returns a token to later remove the subscription.
   */
  virtual long subscribe(subscriber_type&& subscriber) = 0;

  /**
   * Remove a subscriber.
   *
   * @param token the value returns by subscribe().
   * @throws std::exception if token is invalid.
   */
  virtual void unsubscribe(long token) = 0;
};

} // namespace gh

#endif // gh_election_observer_hpp
