#ifndef gh_leader_election_hpp
#define gh_leader_election_hpp

#include <gh/active_completion_queue.hpp>
#include <gh/election_candidate.hpp>
#include <gh/session.hpp>

namespace gh {

/**
 * Participate in a leader election protocol.
 */
class leader_election {
public:
  /// Constructor, blocks until this participant becomes the leader.
  template <typename duration_type>
  leader_election(
      std::shared_ptr<active_completion_queue> queue, std::shared_ptr<grpc::Channel> etcd_channel,
      std::string const& election_name, std::string const& participant_value, duration_type d,
      std::uint64_t lease_id = 0)
      : leader_election(
            true, queue, etcd_channel, election_name, participant_value,
            std::chrono::duration_cast<std::chrono::milliseconds>(d), lease_id) {
  }

  /**
   * Release local resources.
   *
   * The destructor makes sure the *local* resources are released,
   * including connections to the etcd server, and pending
   * operations.  It makes no attempt to resign from the election, or
   * delete the keys in etcd, or to gracefully revoke the etcd leases.
   *
   * The application should call resign() to release the resources
   * held in the etcd server *before* the destructor is called.
   */
  ~leader_election() noexcept(false);

  /// Return true if the candidate is currently the leader
  bool elected() const {
    return candidate_->elected();
  }

  /// Return the etcd key associated with this participant
  std::string const& key() const {
    return candidate_->key();
  }

  /// Return the etcd eky associated with this participant
  std::string const& value() const {
    return candidate_->value();
  }

  /// Return the fetched participant revision, mostly for debugging
  std::int64_t creation_revision() const {
    return candidate_->creation_revision();
  }

  /// Return the lease corresponding to this participant's session.
  std::uint64_t lease_id() const {
    return candidate_->lease_id();
  }

  /// Change the published value
  void proclaim(std::string const& value) {
    candidate_->proclaim(value);
  }

  /// Start the campaign
  virtual std::shared_future<bool> campaign() {
    return candidate_->campaign();
  }

  /// Resign from the election, terminate the internal loops
  void resign() {
    candidate_->resign();
    session_->revoke();
  }

private:
  /// Refactor common code to public constructors ...
  leader_election(
      bool, std::shared_ptr<active_completion_queue> queue, std::shared_ptr<grpc::Channel>,
      std::string const& election_name, std::string const& participant_value, std::chrono::milliseconds desired_TTL,
      std::uint64_t lease_id);

private:
  std::shared_ptr<active_completion_queue> queue_;
  std::shared_ptr<grpc::Channel> channel_;
  std::shared_ptr<session> session_;
  std::shared_ptr<election_candidate> candidate_;
};

} // namespace gh

#endif // gh_leader_election_hpp
