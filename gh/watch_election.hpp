#ifndef gh_watch_election_hpp
#define gh_watch_election_hpp

#include <gh/active_completion_queue.hpp>
#include <gh/election_observer.hpp>
#include <gh/session.hpp>

namespace gh {
class watch_election : public election_observer {
public:
  //@{
  /// @name type traits
  /**
   * Define the type used by subscribers.
   */
  using subscriber_type = election_observer::subscriber_type;
  //@}

  watch_election(
    std::shared_ptr<active_completion_queue> queue, std::shared_ptr<grpc::Channel> etcd_channel,
    std::string const& election_name);

  ~watch_election() noexcept(false) = default;

  //@{
  /// @name Implement the election_observer interface using the pimpl idiom.
  void startup() override {
    return observer_->startup();
  }
  void shutdown() override {
    return observer_->shutdown();
  }
  bool has_leader() const override {
    return observer_->has_leader();
  }
  std::string election_name() const override {
    return observer_->election_name();
  }
  std::string current_key() const override {
    return observer_->current_key();
  }
  std::string current_value() const override {
    return observer_->current_value();
  }
  long subscribe(subscriber_type&& subscriber) override {
    return observer_->subscribe(std::move(subscriber));
  }
  void unsubscribe(long token) override {
    observer_->unsubscribe(token);
  }
  //@}

private:
  std::shared_ptr<active_completion_queue> queue_;
  std::shared_ptr<grpc::Channel> channel_;
  std::shared_ptr<election_observer> observer_;
};
} // namespace gh

#endif // gh_watch_election_hpp
