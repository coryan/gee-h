#ifndef gh_detail_election_observer_impl_hpp
#define gh_detail_election_observer_impl_hpp

#include <gh/assert_throw.hpp>
#include <gh/completion_queue.hpp>
#include <gh/detail/async_op_counter.hpp>
#include <gh/detail/async_rpc_op.hpp>
#include <gh/detail/stream_async_ops.hpp>
#include <gh/election_observer.hpp>
#include <gh/prefix_end.hpp>

#include <etcd/etcdserver/etcdserverpb/rpc.grpc.pb.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace gh {
namespace detail {

/**
 * Monitor an election, reporting the current leader and the value associated with said leader.
 *
 * @tparam completion_queue_type
 */
template <typename completion_queue_type>
class election_observer_impl : public election_observer {
public:
  //@{
  /// @name Type traits.
  using watcher_stream_type = async_rdwr_stream<etcdserverpb::WatchRequest, etcdserverpb::WatchResponse>;
  using watch_write_op = watcher_stream_type::write_op;
  using watch_read_op = watcher_stream_type::read_op;
  //@}

  /**
   * Create a etcd leader election observer and start watching.
   *
   * @param election_name the name of the election.  Do not include the '/' suffix.
   * @param queue the completion queue to mediate all gRPC operations.
   * @param kv_stub a stub to access the etcd server.
   * @param watch_stub a stub to access the etcd server.
   */
  election_observer_impl(
      std::string election_name, completion_queue_type& queue, std::unique_ptr<etcdserverpb::KV::Stub> kv_stub,
      std::unique_ptr<etcdserverpb::Watch::Stub> watch_stub)
      : queue_(queue)
      , mu_()
      , election_name_(std::move(election_name))
      , election_prefix_(election_name_) // see body for full initialization ...
      , subscriptions_()
      , token_gen_(0)
      , kv_stub_(std::move(kv_stub))
      , watch_stub_(std::move(watch_stub))
      , ops_() {
    election_prefix_ += '/'; // ... complete initialization of the field
  }

  virtual ~election_observer_impl() noexcept(false) {
    cleanup();
  }

  //@{
  /// @name implement @c gh::election_observer interface
  virtual bool has_leader() const override {
    std::lock_guard<std::mutex> lock(mu_);
    return not participants_.empty();
  }
  virtual std::string election_name() const override {
    return election_name_;
  }
  virtual std::string current_key() const override {
    std::lock_guard<std::mutex> lock(mu_);
    if (participants_.empty()) {
      std::ostringstream os;
      os << "election_observer_impl::current_key called on empty election";
      throw std::runtime_error(os.str());
    }
    return participants_.begin()->second.key();
  }
  virtual std::string current_value() const override {
    std::lock_guard<std::mutex> lock(mu_);
    if (participants_.empty()) {
      std::ostringstream os;
      os << "election_observer_impl::current_value called on empty election";
      throw std::runtime_error(os.str());
    }
    return participants_.begin()->second.value();
  }
  virtual long subscribe(subscriber_type&& subscriber) override {
    std::unique_lock<std::mutex> lock(mu_);
    // ... make a consistent copy of the current state ...
    if (not participants_.empty()) {
      // ... release the lock while calling application code, holding locks in such cases is prone to deadlocking ...
      auto kv = participants_.begin()->second;
      lock.unlock();
      subscriber(kv.key(), kv.value());
      lock.lock();
    }
    auto token = ++token_gen_;
    subscriptions_.emplace(token, std::move(subscriber));
    return token;
  }
  virtual void unsubscribe(long token) override {
    std::lock_guard<std::mutex> lock(mu_);
    subscriptions_.erase(token);
  }
  virtual void startup() override {
    create_watcher_stream();
    discover_node_with_lowest_creation_revision();
  }
  virtual void shutdown() override {
    // cancel_watcher();
    // cleanup();
  }
  //}

private:
  /// Cleanup local resources, e.g. cancel pending operations and wait for them.
  void cleanup() {
    // ... try to cancel any pending operations ...
    watcher_stream_->context.TryCancel();
    // ... stop any new operations from being created ...
    ops_.shutdown();
    ops_.block_until_all_done();
  }

  void create_watcher_stream() {
    if (not ops_.async_op_start("election_observer/create_watcher_stream")) {
      return;
    }
    // ... we use a blocking operation here because the extra complexity to make these asynchronous is not worth it ...
    auto fut = queue_.async_create_rdwr_stream(
        watch_stub_.get(), &etcdserverpb::Watch::Stub::AsyncWatch, "election_observer/create_watcher_stream",
        gh::use_future());
    watcher_stream_ = fut.get();
    ops_.async_op_done("create_watcher_stream()");
  }

  void create_watcher(long start_revision) {
    if (not ops_.async_op_start("on_range_request/create_watch")) {
      return;
    }
    etcdserverpb::WatchRequest req;
    auto& create = *req.mutable_create_request();
    create.set_key(election_prefix_);
    create.set_range_end(prefix_end(election_prefix_));
    create.set_progress_notify(true);
    create.set_start_revision(start_revision);
    create.set_prev_kv(true);
    queue_.async_write(
        *watcher_stream_, std::move(req), "on_range_request/create_watch",
        [this](auto const& fop, bool fok) { this->on_watch_create(fop, fok); });
  }

  void discover_node_with_lowest_creation_revision() {
    // So we wait on the immediate predecessor of the current
    // participant sorted by creation_revision.  That is found by:
    etcdserverpb::RangeRequest req;
    //   - Search all the keys in the range for the election ...
    req.set_key(election_prefix_);
    req.set_range_end(prefix_end(election_prefix_));
    //   - Sort those results in ascending order by creation_revision.
    req.set_sort_order(etcdserverpb::RangeRequest::ASCEND);
    req.set_sort_target(etcdserverpb::RangeRequest::CREATE);
    //   - Only fetch the first of those results.
    req.set_limit(1);

    if (not ops_.async_op_start("election_observer/discover_node_with_lowest_creation_revision")) {
      return;
    }
    queue_.async_rpc(
        kv_stub_.get(), &etcdserverpb::KV::Stub::AsyncRange, std::move(req),
        "election_observer/discover_node_with_lowest_creation_revision",
        [this](auto const& op, bool ok) { this->on_range_request(op, ok); });
  }

  void on_range_request(async_rpc_op<etcdserverpb::RangeRequest, etcdserverpb::RangeResponse> const& op, bool ok) {
    ops_.async_op_done("election_observer/discover_node_with_lowest_creation_revision");
    if (not ok) {
      // ... operation canceled, consider restarting the whole cycle ...
      return;
    }
    // ... the range query returns either 0 or 1 keys. If there are no keys we setup the watcher, the next element to
    // be reported will be the leader of the election.  If there is one key that is the current leader of the
    // election.  All of this is proceed asynchronously, but the comments help put the code in context ...
    if (not op.response.kvs().empty()) {
      // ... watch all relevant nodes starting from the first one ...
      create_watcher(op.response.kvs(0).create_revision());
      GH_ASSERT_THROW(op.response.kvs().size() == 1UL);
      report_election_leader(op.response.kvs(0));
    } else {
      // ... watch all the relevant nodes starting from the current revision ...
      create_watcher(op.response.header().revision());
    }
  }

  void on_watch_create(watch_write_op const& op, bool ok) {
    ops_.async_op_done("election_observer/create_watcher_stream");
    if (not ok) {
      // ... operation canceled, consider restarting the whole cycle ...
      return;
    }
    if (not ops_.async_op_start("election_observer/on_watch_create/watch_read")) {
      return;
    }
    queue_.async_read(
        *watcher_stream_, "election_observer/on_watch_create/watch_read",
        [this](auto const& fop, bool fok) { this->on_watch_read(fop, fok); });
  }

  void on_watch_read(watch_read_op const& op, bool ok) {
    ops_.async_op_done("election_observer/on_watch_create/watch_read");
    if (not ok) {
      return;
    }
    if (op.response.canceled()) {
      return;
    }
    if (op.response.compact_revision()) {
      return;
    }
    std::unique_lock<std::mutex> lock(mu_);
    bool leader_changed = false;
    for (auto const& ev : op.response.events()) {
      if (ev.type() == mvccpb::Event::PUT) {
        auto const& kv = ev.kv();
        leader_changed = handle_node_put(kv) || leader_changed;
      } else if (ev.type() == mvccpb::Event::DELETE) {
        auto const& kv = ev.prev_kv();
        leader_changed = handle_node_delete(kv) || leader_changed;
      }
    }
    if (leader_changed) {
      call_subscribers(lock);
    }
    if (not ops_.async_op_start("election_observer/on_watch_read/watch_read")) {
      return;
    }
    queue_.async_read(
        *watcher_stream_, "election_observer/on_watch_read/watch_read",
        [this](auto const& fop, bool fok) { this->on_watch_read(fop, fok); });
  }

  /// Modify or add a node to the known participants, return true if the leader changed.
  bool handle_node_put(mvccpb::KeyValue const& kv) {
    GH_LOG(info) << election_name() << " PUT on " << kv.key() << " = " << kv.value() << " / " << kv.create_revision();
    auto f = participants_.find(kv.create_revision());
    if (f != participants_.end()) {
      f->second = kv;
    } else {
      f = participants_.emplace_hint(f, kv.create_revision(), kv);
    }
    return f == participants_.begin();
  }

  /// Delete a node from the known participants, return true if the leader changed.
  bool handle_node_delete(mvccpb::KeyValue const& kv) {
    GH_LOG(info) << election_name() << " DEL on " << kv.key() << " = " << kv.value() << " / " << kv.create_revision();
    auto f = participants_.find(kv.create_revision());
    if (f == participants_.end()) {
      return false;
    }
    auto changed = f == participants_.begin();
    participants_.erase(f);
    return changed;
  }

  void report_election_leader(mvccpb::KeyValue const& kv) {
    std::unique_lock<std::mutex> lock(mu_);
    if (handle_node_put(kv)) {
      call_subscribers(lock);
    }
  }

  void call_subscribers(std::unique_lock<std::mutex>& lock) {
    mvccpb::KeyValue kv;
    if (not participants_.empty()) {
      kv = participants_.begin()->second;
    }
    // ... make a copy of the subscriptions so we can iterate over it without holding the lock ...
    auto copy = subscriptions_;
    // ... and we release the lock because calling application code while holding locks is a recipe for deadlocks ...
    lock.unlock();
    for (auto const& p : subscriptions_) {
      try {
        p.second(kv.key(), kv.value());
      } catch (...) {
      }
    }
  }

private:
  using subscriptions_type = std::unordered_map<long, subscriber_type>;

  completion_queue_type& queue_;
  mutable std::mutex mu_;
  std::string election_name_;
  std::string election_prefix_;
  subscriptions_type subscriptions_;
  long token_gen_;
  std::unique_ptr<etcdserverpb::KV::Stub> kv_stub_;
  std::unique_ptr<etcdserverpb::Watch::Stub> watch_stub_;
  std::shared_ptr<watcher_stream_type> watcher_stream_;

  using participants_type = std::map<std::uint64_t, mvccpb::KeyValue>;
  participants_type participants_;

  async_op_counter ops_;
};

} // namespace detail
} // namespace gh

#endif // gh_detal_election_observer_hpp
