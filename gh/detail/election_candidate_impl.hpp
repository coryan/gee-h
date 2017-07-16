#ifndef gh_detail_election_candidate_impl_hpp
#define gh_detail_election_candidate_impl_hpp

#include <gh/assert_throw.hpp>
#include <gh/completion_queue.hpp>
#include <gh/detail/async_op_counter.hpp>
#include <gh/detail/candidate_state_machine.hpp>
#include <gh/detail/grpc_errors.hpp>
#include <gh/detail/stream_async_ops.hpp>
#include <gh/election_candidate.hpp>
#include <gh/log.hpp>
#include <gh/prefix_end.hpp>

#include <etcd/etcdserver/etcdserverpb/rpc.grpc.pb.h>

namespace gh {
namespace detail {

/**
 * Implement an election candidate given the type of completion queue.
 */
template <typename completion_queue_type>
class election_candidate_impl : public election_candidate {
public:
  //@{
  using watcher_stream_type = detail::async_rdwr_stream<etcdserverpb::WatchRequest, etcdserverpb::WatchResponse>;
  using watch_write_op = watcher_stream_type::write_op;
  using watch_read_op = watcher_stream_type::read_op;

  using state = candidate_state;
  //@}

  /// Constructor, non-blocking, calls the callback when elected.
  template <typename Functor>
  election_candidate_impl(
      completion_queue_type& queue, std::uint64_t lease_id, std::unique_ptr<etcdserverpb::KV::Stub> kv_stub,
      std::unique_ptr<etcdserverpb::Watch::Stub> watch_stub, std::string const& election_name,
      std::string const& candidate_value, Functor&& elected_callback)
      : mu_()
      , queue_(queue)
      , lease_id_(lease_id)
      , kv_stub_(std::move(kv_stub))
      , watch_stub_(std::move(watch_stub))
      , watcher_stream_()
      , election_name_(election_name)
      , election_prefix_()
      , candidate_value_(candidate_value)
      , candidate_key_()
      , creation_revision_(0)
      , current_watches_()
      , watched_keys_()
      , campaign_callback_()
      , state_machine_()
      , ops_() {
    election_prefix_ = election_name_ + "/";
    std::ostringstream os;
    os << election_prefix_ << std::hex << lease_id_;
    candidate_key_ = os.str();
  }

  /**
   * Release local resources.
   *
   * The destructor makes sure the *local* resources are released, including connections to the etcd server and
   * pending operations.  It makes no attempt to resign from the election, or delete the keys in etcd, or to
   * gracefully revoke the etcd leases.
   *
   * The application should call resign() to release the resources held in the etcd server *before* the destructor is
   * called.
   */
  ~election_candidate_impl() noexcept(false) {
    cleanup();
  }

  virtual std::string const& key() const override {
    return candidate_key_;
  }

  std::string const& value() const override {
    return candidate_value_;
  }

  std::uint64_t creation_revision() const override {
    return creation_revision_;
  }

  std::uint64_t lease_id() const override {
    return lease_id_;
  }

  void proclaim(std::string const& new_value) override {
    GH_LOG(trace) << log_header("") << " proclaim(" << new_value << ")";
    std::string copy(new_value);
    etcdserverpb::RequestOp failure_op;
    auto result = publish_value(copy, failure_op);
    if (result.succeeded()) {
      copy.swap(candidate_value_);
      GH_LOG(trace) << log_header("") << " proclaim(" << new_value << ")";
      return;
    }
    std::ostringstream os;
    os << key() << " unexpected failure writing new value:\n" << print_to_stream(result) << "\n";
    throw std::runtime_error(os.str());
  }

  void startup() override {
    preamble();
  }

  void resign() override {
    if (not state_machine_.change_state("resign() begin", state::resigning)) {
      return;
    }
    std::set<std::uint64_t> watches;
    {
      std::lock_guard<std::mutex> lock(mu_);
      watches = std::move(current_watches_);
    }
    // ... cancel all the watchers too ...
    for (auto w : watches) {
      GH_LOG(trace) << log_header(" cancel watch") << " = " << w;
      if (not ops_.async_op_start("cancel watch")) {
        return;
      }
      etcdserverpb::WatchRequest req;
      auto& cancel = *req.mutable_cancel_request();
      cancel.set_watch_id(w);

      queue_.async_write(
          *watcher_stream_, std::move(req), "leader_election_candidate/cancel_watcher",
          [this, w](auto op, bool ok) { this->on_watch_cancel(op, ok, w); });
    }
    // ... block until all pending operations complete ...
    ops_.block_until_all_done();
    // ... if there is a pending callback we need to let them know the
    // election failed ...
    make_callback(false);
    // ... now we are really done with remote resources ...
    state_machine_.change_state("resign() end", state::resigned);
  }

private:
  /**
   * Runs the operations before starting the election campaign.
   *
   * This function can throw exceptions which means the campaign was never even started.
   */
  void preamble() try {
    // ... no real need to grab a mutex here.  The object is not fully
    // constructed, it should not be used by more than one thread ...
    state_machine_.change_state("preamble()", state::connecting);

    auto fut = queue_.async_create_rdwr_stream(
        watch_stub_.get(), &etcdserverpb::Watch::Stub::AsyncWatch, "leader_election_candidate/watch", gh::use_future());
    watcher_stream_ = fut.get();
    state_machine_.change_state("preamble()", state::querying_predecessor);

    // ... we need to create a node to represent this candidate in
    // the leader election.  We do this with a test-and-set
    // operation.  The test is "does this key have creation_version ==
    // 0", which is really equivalent to "does this key exist",
    // because any key actually created would have a higher creation
    // version ...
    etcdserverpb::TxnRequest req;
    auto& cmp = *req.add_compare();
    cmp.set_key(key());
    cmp.set_result(etcdserverpb::Compare::EQUAL);
    cmp.set_target(etcdserverpb::Compare::CREATE);
    cmp.set_create_revision(0);
    // ... if the key is not there we are going to create it, and
    // store the "candidate value" there ...
    auto& on_success = *req.add_success()->mutable_request_put();
    on_success.set_key(key());
    on_success.set_value(value());
    on_success.set_lease(lease_id());
    // ... if the key is there, we are going to fetch its current
    // value, there will be some fun action with that ...
    auto& on_failure = *req.add_failure()->mutable_request_range();
    on_failure.set_key(key());

    // ... execute the transaction in etcd ...
    etcdserverpb::TxnResponse resp = commit(req, "leader_election/commit/create_node");

    // ... regardless of which branch of the test-and-set operation
    // pass, we now have fetched the candidate revision value ..
    creation_revision_ = resp.header().revision();

    if (not resp.succeeded()) {
      // ... the key already existed, possibly because a previous
      // instance of the program participated in the election and etcd
      // did not had time to expire the key.  We need to use the
      // previous creation_revision and save our new candidate_value
      // ...
      GH_ASSERT_THROW(resp.responses().size() == 1);
      GH_ASSERT_THROW(resp.responses()[0].response_range().kvs().size() == 1U);
      auto const& kv = resp.responses()[0].response_range().kvs()[0];
      creation_revision_ = kv.create_revision();
      // ... if the value is the same, we can avoid a round-trip
      // request to the server ...
      if (kv.value() != value()) {
        state_machine_.change_state("preamble()", state::updating_node);
        // ... too bad, need to publish again *and* we need to delete
        // the key if the publication fails ...
        etcdserverpb::RequestOp failure_op;
        auto& delete_op = *failure_op.mutable_request_delete_range();
        delete_op.set_key(key());
        auto published = publish_value(value(), failure_op);
        if (not published.succeeded()) {
          // ... ugh, the publication failed.  We now have an inconsistent state with the server.  We think we own the
          // code (and at least we own the lease!), but we were unable to publish the value.  We are going to raise
          // an exception and abort the creation of the candidate ...
          std::ostringstream os;
          os << "Unexpected failure writing new value on existing key=" << key()
             << "\ntxn result=" << print_to_stream(published) << "\n";
          throw std::runtime_error(os.str());
        }
      }
    }
    state_machine_.change_state("preamble()", state::node_created);
  } catch (std::exception const& ex) {
    GH_LOG(trace) << log_header("") << " std::exception raised in preamble: " << ex.what();
    shutdown();
    throw;
  }

  /**
   * Gracefully cleanup the local resources of a partially or fully constructed instance
   *
   * That requires terminating any pending operations, or the completion queue calls abort().
   */
  void cleanup() {
    queue_.try_cancel_on(*watcher_stream_);
    ops_.shutdown();
    ops_.block_until_all_done();
  }

  void shutdown() {
    if (not state_machine_.change_state("shutdown()", state::shutting_down)) {
      return;
    }
    GH_LOG(trace) << log_header("") << "  shutdown";
    // ... if there is a pending range request we need to block on it ...
    ops_.block_until_all_done();
    if (watcher_stream_) {
      // The watcher stream was already created, we need to close it before shutting down the completion queue ...
      (void)ops_.async_op_start("writes done");
      auto writes_done_complete =
          queue_.async_writes_done(*watcher_stream_, "election_candidate/shutdown/writes_done", gh::use_future());

      // ... block until it closes ...
      writes_done_complete.get();
      GH_LOG(trace) << log_header("") << "  writes done completed";

      (void)ops_.async_op_start("finish");
      auto finished_complete =
          queue_.async_finish(*watcher_stream_, "election_candidate/shutdown/finish", gh::use_future());
      GH_LOG(trace) << log_header("") << "  finish scheduled";
      // TODO() - this is a workaround, the finish() call does not seem
      // to terminate for me ...
      if (finished_complete.wait_for(std::chrono::milliseconds(200)) == std::future_status::timeout) {
        GH_LOG(info) << log_header("") << "  timeout on Finish() call";
        ops_.async_op_done("on_finish() - forced");
      }
    }
    (void)state_machine_.change_state("shutdown()", state::shutdown);
  }

  /// Kick-off a campaign and call a functor when elected
  template <typename Functor>
  void campaign(Functor&& callback) {
    campaign_impl(std::function<void(bool)>(std::move(callback)));
  }

  /**
   * Refactor template code via std::function
   *
   * The main "interface" is the campaing() template member functions, but we loath duplicating that much code here,
   * so refactor with a std::function<>.  The cost of such functions is higher, but leader election is not a fast
   * operation.
   */
  void campaign_impl(std::function<void(bool)>&& callback) {
    GH_LOG(trace) << log_header("") << "  kicking off campaign";
    // First save the callback ...
    {
      std::lock_guard<std::mutex> lock(mu_);
      GH_ASSERT_THROW(bool(campaign_callback_) == false);
      campaign_callback_ = std::move(callback);
    }
    // ... we want to wait on a single key, waiting on more would
    // create thundering herd problems.  To win the election this
    // candidate needs to have the smallest creation_revision
    // amongst all the candidates within the election.

    // So we wait on the immediate predecessor of the current
    // candidate sorted by creation_revision.  That is found by:
    etcdserverpb::RangeRequest req;
    //   - Search all the keys that have the same prefix (that is the
    //     election_prefix_
    req.set_key(election_prefix_);
    //   - Prefix searches are range searches where the end value is 1
    //     bit higher than the initial value.
    req.set_range_end(prefix_end(election_prefix_));
    //   - Limit those results to the keys that have creation_revision
    //     lower than this candidate's creation_revision key
    req.set_max_create_revision(creation_revision_ - 1);
    //   - Sort those results in descending order by
    //     creation_revision.
    req.set_sort_order(etcdserverpb::RangeRequest::DESCEND);
    req.set_sort_target(etcdserverpb::RangeRequest::CREATE);
    //   - Only fetch the first of those results.
    req.set_limit(1);

    // ... after all that filtering you are left with 0 or 1 keys.
    // If there is 1 key, we need to setup a watcher and wait until
    // the key is deleted.
    // If there are 0 keys, we won the campaign, and we are done.
    // That won't happen in this function, the code is asynchronous,
    // and broken over many functions, but the context is useful to
    // understand what is happening ...

    (void)state_machine_.change_state("campaign_impl()", state::querying_predecessor);
    ops_.async_op_start("range request");
    queue_.async_rpc(
        kv_stub_.get(), &etcdserverpb::KV::Stub::AsyncRange, std::move(req), "election_candidate/campaign/range",
        [this](auto const& op, bool ok) { this->on_range_request(op, ok); });
  }

  /// Refactor code common to proclaim() and preamble()
  etcdserverpb::TxnResponse publish_value(std::string const& value, etcdserverpb::RequestOp const& failure_op) {
    GH_LOG(trace) << log_header("") << " publish_value()";
    etcdserverpb::TxnRequest req;
    auto& cmp = *req.add_compare();
    cmp.set_key(key());
    cmp.set_result(etcdserverpb::Compare::EQUAL);
    cmp.set_target(etcdserverpb::Compare::CREATE);
    cmp.set_create_revision(creation_revision_);
    auto& on_success = *req.add_success()->mutable_request_put();
    on_success.set_key(key());
    on_success.set_value(value);
    on_success.set_lease(lease_id());
    if (failure_op.request_case() != etcdserverpb::RequestOp::REQUEST_NOT_SET) {
      *req.add_failure() = failure_op;
    }

    return commit(req, "leader_election/publish_value");
  }

  /// Refactor code to perform a Txn() request.
  etcdserverpb::TxnResponse commit(etcdserverpb::TxnRequest const& r, std::string name) {
    etcdserverpb::TxnRequest req = r;
    auto fut = queue_.async_rpc(
        kv_stub_.get(), &etcdserverpb::KV::Stub::AsyncTxn, std::move(req), std::move(name), gh::use_future());
    return fut.get();
  }

  /// Called when the Range() operation in the kv_stub completes.
  void on_range_request(async_rpc_op<etcdserverpb::RangeRequest, etcdserverpb::RangeResponse> const& op, bool ok) {
    if (not ok) {
      make_callback(false);
    }
    ops_.async_op_done("on_range_request()");
    check_grpc_status(op.status, log_header("on_range_request()"), ", response=", print_to_stream(op.response));

    for (auto const& kv : op.response.kvs()) {
      // ... we need to capture the key and revision of the result, so
      // we can then start a Watch starting from that revision ...

      if (not ops_.async_op_start("create watch")) {
        return;
      }
      (void)state_machine_.change_state("on_range_request()", state::waiting_for_watcher_read);
      GH_LOG(trace) << log_header("") << "  create watcher ... k=" << kv.key();
      watched_keys_.insert(kv.key());

      etcdserverpb::WatchRequest req;
      auto& create = *req.mutable_create_request();
      create.set_key(kv.key());
      create.set_start_revision(op.response.header().revision() - 1);

      queue_.async_write(*watcher_stream_, std::move(req), "election_candidate/on_range_request/watch", [
        this, key = kv.key(), revision = op.response.header().revision()
      ](auto op, bool ok) { this->on_watch_create(op, ok, key, revision); });
    }
    check_election_over_maybe();
  }

  /// Called when a Write() operation that creates a watcher completes.
  void on_watch_create(watch_write_op const& op, bool ok, std::string const& wkey, std::uint64_t wrevision) {
    ops_.async_op_done("on_watch_create()");
    if (not ok) {
      GH_LOG(trace) << log_header("on_watch_create(.., false) wkey=") << wkey;
      return;
    }
    if (not ops_.async_op_start("read watch")) {
      return;
    }

    queue_.async_read(
        *watcher_stream_, "election_candidate/on_watch_create/read",
        [this, wkey, wrevision](auto op, bool ok) { this->on_watch_read(op, ok, wkey, wrevision); });
  }

  /// Called when a Write() operation that cancels a watcher completes.
  void on_watch_cancel(watch_write_op const& op, bool ok, std::uint64_t watched_id) {
    // ... there should be a Read() pending already ...
    ops_.async_op_done("on_watch_cancel()");
  }

  /// Called when a Read() operation in the watcher stream completes.
  void on_watch_read(watch_read_op const& op, bool ok, std::string const& wkey, std::uint64_t wrevision) {
    ops_.async_op_done("on_watch_read()");
    if (not ok) {
      GH_LOG(trace) << log_header("on_watch_read(.., false) wkey=") << wkey;
      return;
    }
    if (op.response.created()) {
      GH_LOG(trace) << "  received new watcher=" << op.response.watch_id();
      std::lock_guard<std::mutex> lock(mu_);
      current_watches_.insert(op.response.watch_id());
    } else {
      GH_LOG(trace) << log_header("") << "  update for existing watcher=" << op.response.watch_id();
    }
    for (auto const& ev : op.response.events()) {
      // ... DELETE events indicate that the other candidate's lease
      // expired, or they actively resigned, other events are not of
      // interest ...
      if (ev.type() != mvccpb::Event::DELETE) {
        continue;
      }
      // ... remove that key from the set of keys we are waiting to be
      // deleted ...
      watched_keys_.erase(ev.kv().key());
    }
    check_election_over_maybe();
    // ... unless the watcher was canceled we should continue to read
    // from it ...
    if (op.response.canceled()) {
      current_watches_.erase(op.response.watch_id());
      return;
    }
    if (op.response.compact_revision()) {
      // TODO() - if I am reading the documentation correctly, this
      // means the watcher was cancelled, but the data may (or may
      // not) still be there.  We need to worry about the case where
      // the candidate figures out the key to watch.  Then it goes
      // to sleep, or gets rescheduled, then the key is deleted
      // and etcd compacted.  And then the client starts watching.
      //
      // I am not sure this is a problem, but it might be.
      //
      GH_LOG(info) << log_header("") << " watcher cancelled with compact_revision=" << op.response.compact_revision()
                   << ", wkey=" << wkey << ", revision=" << wrevision << ", reason=" << op.response.cancel_reason()
                   << ", watch_id=" << op.response.watch_id();
      std::lock_guard<std::mutex> lock(mu_);
      current_watches_.erase(op.response.watch_id());
      return;
    }
    if (not state_machine_.change_state("shutdown()", state::shutdown)) {
      return;
    }
    // ... the watcher was not canceled, so try reading again ...
    if (not ops_.async_op_start("read watch / followup")) {
      return;
    }

    queue_.async_read(
        *watcher_stream_, "election_candidate/on_watch_read/read",
        [this, wkey, wrevision](auto op, bool ok) { this->on_watch_read(op, ok, wkey, wrevision); });
  }

  /// Check if the election has finished, if so invoke the callbacks.
  void check_election_over_maybe() {
    // ... do not worry about changes without a lock if it is positive then a future Read() will decrement it and we
    // will check again ...

    std::unique_lock<std::mutex> lock(mu_);
    if (not watched_keys_.empty()) {
      return;
    }
    lock.unlock();
    GH_LOG(trace) << log_header("") << " election completed";
    state_machine_.change_state("check_election_over()", state::waiting_for_watcher_read);
    make_callback(true);
  }

  // Invoke the callback, notice that the callback is invoked only once.
  void make_callback(bool result) {
    std::function<void(bool)> callback;
    {
      std::lock_guard<std::mutex> lock(mu_);
      callback = std::move(campaign_callback_);
    }
    if (not callback) {
      GH_LOG(trace) << log_header("") << " no callback present";
      return;
    }
    callback(result);
    GH_LOG(trace) << log_header("") << "  made callback";
  }

  std::string log_header(char const* log) const {
    std::ostringstream os;
    os << key() << " " << state_machine_.current() << " " << log;
    return os.str();
  }

private:
  mutable std::mutex mu_;

  completion_queue_type& queue_;
  std::uint64_t lease_id_;
  std::unique_ptr<etcdserverpb::KV::Stub> kv_stub_;
  std::unique_ptr<etcdserverpb::Watch::Stub> watch_stub_;
  std::shared_ptr<watcher_stream_type> watcher_stream_;
  std::string election_name_;
  std::string election_prefix_;
  std::string candidate_value_;
  std::string candidate_key_;
  std::uint64_t creation_revision_;

  std::set<std::uint64_t> current_watches_;
  std::set<std::string> watched_keys_;

  std::function<void(bool)> campaign_callback_;

  candidate_state_machine state_machine_;
  async_op_counter ops_;
};

} // namespace detail
} // namespace gh

#endif // gh_detail_election_candidate_impl_hpp
