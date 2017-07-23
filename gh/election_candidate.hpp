#ifndef gh_election_candidate_hpp
#define gh_election_candidate_hpp

#include <cstdint>
#include <string>
#include <functional>
#include <future>

namespace gh {
/**
 * Define the interface to joins a leader election and campaign to become the leader.
 */
class election_candidate {
public:
  virtual ~election_candidate() noexcept(false);

  /// Return true if this is the current leader
  virtual bool elected() const = 0;

  /// Return the etcd key associated with this participant
  virtual std::string const& key() const = 0;

  /// Return the etcd eky associated with this participant
  virtual std::string const& value() const = 0;

  /// Return the fetched participant revision, mostly for debugging
  virtual std::int64_t creation_revision() const = 0;

  /// Return the lease corresponding to this participant's session.
  virtual std::uint64_t lease_id() const = 0;

  /// Publish a new value
  virtual void proclaim(std::string const& value) = 0;

  /// Start the campaign and block until elected
  virtual std::shared_future<bool> campaign() = 0;

  /// Shutdown the campaign, release all global resources associated with this candidate
  virtual void resign() = 0;
};
} // namespace gh

#endif // gh_election_candidate_hpp
