#ifndef gh_detail_session_impl_hpp
#define gh_detail_session_impl_hpp

#include <gh/completion_queue.hpp>
#include <gh/detail/async_op_counter.hpp>
#include <gh/detail/deadline_timer.hpp>
#include <gh/detail/grpc_errors.hpp>
#include <gh/detail/stream_async_ops.hpp>
#include <gh/log.hpp>
#include <gh/session.hpp>

#include <etcd/etcdserver/etcdserverpb/rpc.grpc.pb.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace gh {
namespace detail {

template <typename completion_queue_type>
class session_impl : public ::gh::session {
public:
  //@{
  /// @name type traits

  /// The type of the bi-directional RPC stream for keep alive messages
  using ka_stream_type = async_rdwr_stream<etcdserverpb::LeaseKeepAliveRequest, etcdserverpb::LeaseKeepAliveResponse>;

  /// The preferred units for measuring time in this class
  using duration_type = std::chrono::milliseconds;
  //@}

  /// How many KeepAlive requests we send per TTL cyle.
  // TODO() - the magic number 5  should be a configurable parameter.
  static int constexpr keep_alives_per_ttl = 5;

  /// Constructor
  template <typename duration_type>
  session_impl(
      completion_queue_type& queue, std::unique_ptr<etcdserverpb::Lease::Stub> lease_stub, duration_type desired_TTL)
      : queue_(queue)
      , lease_client_(std::move(lease_stub))
      , ka_stream_()
      , lease_id_(0)
      , desired_TTL_(convert_duration(desired_TTL))
      , actual_TTL_(convert_duration(desired_TTL))
      , current_timer_()
      , ops_()
      , reads_() {
    preamble();
  }

  /**
   * Contructor with a previous lease.
   *
   * This could be useful for an application that saves its lease, shuts
   * down, and quickly restarts.  I think it is a stretch, but so easy
   * to implement that why not?
   */
  template <typename duration_type>
  session_impl(
      completion_queue_type& queue, std::unique_ptr<etcdserverpb::Lease::Stub> lease_stub, duration_type desired_TTL,
      std::uint64_t lease_id)
      : queue_(queue)
      , lease_client_(std::move(lease_stub))
      , ka_stream_()
      , lease_id_(lease_id)
      , desired_TTL_(convert_duration(desired_TTL))
      , actual_TTL_(convert_duration(desired_TTL))
      , current_timer_()
      , ops_()
      , reads_() {
    preamble();
  }

  session_impl(session_impl const&) = delete;
  session_impl& operator=(session_impl const&) = delete;
  session_impl(session_impl&&) = delete;
  session_impl& operator=(session_impl&&) = delete;

  ~session_impl() noexcept(false) override {
    shutdown();
  }

  std::uint64_t lease_id() const override {
    return lease_id_;
  }

  std::chrono::milliseconds actual_TTL() const override {
    return actual_TTL_;
  }

  bool is_active() const override {
    return ((bool)current_timer_) and not ops_.in_shutdown();
  }

  /// Convert a duration to the preferred units in this class
  template <typename other_duration_type>
  static duration_type convert_duration(other_duration_type d) {
    return std::chrono::duration_cast<duration_type>(d);
  }

  /// Revoke the lease
  void revoke() override {
    if (ops_.in_shutdown()) {
      return;
    }
    // ... cancel the outstanding timer, if any ...
    if (current_timer_) {
      current_timer_->cancel();
      current_timer_.reset();
    }

    // ... here we just block, we could make this asynchronous, but really there is no reason to ...
    {
      etcdserverpb::LeaseRevokeRequest req;
      req.set_id(lease_id_);
      async_op_tracer lease_revoke_trace(ops_, "session/revoke/lease_revoke");
      auto lfut = queue_.async_rpc(
          lease_client_.get(), &etcdserverpb::Lease::Stub::AsyncLeaseRevoke, std::move(req),
          "session/revoke/lease_revoke", gh::use_future());
      auto resp = lfut.get();
    }
    if (ka_stream_) {
      reads_.block_until_all_done();
      // The KeepAlive stream was already created, we need to close it before shutting down ...
      async_op_tracer writes_done_trace(ops_, "session/shutdown/writes_done");
      auto writes_done_complete =
          queue_.async_writes_done(*ka_stream_, "session/shutdown/writes_done", gh::use_future());
      // ... block until it closes ...
      writes_done_complete.get();

      async_op_tracer finish_trace(ops_, "session/shutdown/finish");
      auto finish_complete = queue_.async_finish(*ka_stream_, "session/shutdown/finish", gh::use_future());
      auto status = finish_complete.get();
      check_grpc_status(status, "session::finish()");
    }
    ops_.block_until_all_done();
  }

private:
  /// Requests (or renews) the lease and setup the watcher stream.
  void preamble() try {
    // ... we want to block until the keep alive streaming RPC is setup,
    // this is (unfortunately) an asynchronous operation, so we have to
    // do some magic ...
    std::promise<bool> stream_ready;
    async_op_tracer create_rdwr_stream_trace(ops_, "session/ka_stream");
    auto fut = queue_.async_create_rdwr_stream(
        lease_client_.get(), &etcdserverpb::Lease::Stub::AsyncLeaseKeepAlive, "session/ka_stream", gh::use_future());
    this->ka_stream_ = fut.get();

    // ...request a new lease from the etcd server ...
    etcdserverpb::LeaseGrantRequest req;
    // ... the TTL is is seconds, convert to the right units ...
    auto ttl_seconds = std::chrono::duration_cast<std::chrono::seconds>(desired_TTL_);
    req.set_ttl(ttl_seconds.count());
    req.set_id(lease_id_);

    async_op_tracer lease_grant_trace(ops_, "session/preamble/lease_grant");
    auto lfut = queue_.async_rpc(
        lease_client_.get(), &etcdserverpb::Lease::Stub::AsyncLeaseGrant, std::move(req),
        "session/preamble/lease_grant", gh::use_future());
    auto resp = lfut.get();

    // TODO() - probably need to retry until it succeeds, with some kind of backoff, and a long timeout ...
    if (resp.error() != "") {
      std::ostringstream os;
      os << "Lease grant request rejected lease_id=" << lease_id_ << " response=" << print_to_stream(resp);
      throw std::runtime_error(os.str());
    }

    lease_id_ = resp.id();
    actual_TTL_ = convert_duration(std::chrono::seconds(resp.ttl()));

    set_timer();
  } catch (std::exception const& ex) {
    shutdown();
    throw;
  } catch (...) {
    shutdown();
    throw;
  }

  /// Shutdown the local resources.
  void shutdown() {
    if (ops_.in_shutdown()) {
      // ... already shutdown once, nothing to do ...
      return;
    }
    if (current_timer_) {
      current_timer_->cancel();
      current_timer_.reset();
    }
    if (ka_stream_) {
      queue_.try_cancel_on(*ka_stream_);
    }
    reads_.block_until_all_done();
    ops_.block_until_all_done();
  }

  /// Set a timer to start the next Write/Read cycle.
  void set_timer() {
    // ... we are going to schedule a new timer, in general, we should only schedule a timer when there are no pending
    // KeepAlive request/responses in the stream.  The AsyncReaderWriter docs says you can only have one outstanding
    // Write() request at a time.  If we started the timer there is no guarantee that the timer won't expire before
    // the next response ...

    auto deadline = std::chrono::system_clock::now() + (actual_TTL_ / keep_alives_per_ttl);
    if (not ops_.async_op_start("session/set_timer/ttl_refresh")) {
      return;
    }
    current_timer_ = queue_.make_deadline_timer(
        deadline, "session/set_timer/ttl_refresh", [this](auto const& op, bool ok) { this->on_timeout(op, ok); });
  }

  /// Handle the timer expiration, Write() a new LeaseKeepAlive request.
  void on_timeout(detail::deadline_timer const& op, bool ok) {
    ops_.async_op_done("session/set_timer/ttl_refresh");
    if (not ok or not ops_.async_op_start("session/on_timeout/write")) {
      // ... this is a canceled timer ...
      return;
    }
    etcdserverpb::LeaseKeepAliveRequest req;
    req.set_id(lease_id());
    queue_.async_write(*ka_stream_, std::move(req), "session/on_timeout/write", [this](auto fop, bool fok) {
      this->on_write(fop, fok);
    });
  }

  /// Handle the Write() completion, schedule a new LeaseKeepAlive Read().
  void on_write(ka_stream_type::write_op& op, bool ok) {
    ops_.async_op_done("session/on_timeout/write");
    if (not ok or not reads_.async_op_start("session/on_write/read")) {
      // TODO() - consider logging or exceptions in this case (canceled operation) ...
      return;
    }
    queue_.async_read(*ka_stream_, "session/on_write/read", [this](auto fop, bool fok) { this->on_read(fop, fok); });
  }

  /// Handle the Read() completion, schedule a new Timer().
  void on_read(ka_stream_type::read_op& op, bool ok) {
    reads_.async_op_done("session/on_write/read");
    if (not ok) {
      // TODO() - consider logging or exceptions in this case (canceled operation) ...
      return;
    }
    // ... the KeepAliveResponse may have a new TTL value, that is the
    // etcd server may be telling us to backoff a little ...
    actual_TTL_ = std::chrono::seconds(op.response.ttl());
    set_timer();
  }

private:
  completion_queue_type& queue_;

  std::unique_ptr<etcdserverpb::Lease::Stub> lease_client_;
  std::shared_ptr<ka_stream_type> ka_stream_;

  /// The lease is assigned by etcd during the constructor
  std::uint64_t lease_id_;

  /// The requested TTL value.
  // TODO() - decide if storing the TTL in milliseconds makes any sense, after all etcd uses seconds ...
  std::chrono::milliseconds desired_TTL_;

  /// etcd may tell us to use a longer (or shorter?) TTL.
  std::chrono::milliseconds actual_TTL_;

  /// The current timer, can be null when waiting for a KeepAlive response.
  std::shared_ptr<detail::deadline_timer> current_timer_;

  /// Track pending asynchronous operations.
  async_op_counter ops_;
  async_op_counter reads_;
};

/// Define the object.
template <typename completion_queue_type>
int constexpr session_impl<completion_queue_type>::keep_alives_per_ttl;

} // namespace detail
} // namespace gh

#endif // gh_detail_session_impl_hpp
