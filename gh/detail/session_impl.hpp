#ifndef gh_detail_session_impl_hpp
#define gh_detail_session_impl_hpp

#include <gh/completion_queue.hpp>
#include <gh/detail/session_impl_common.hpp>
#include <gh/detail/grpc_errors.hpp>
#include <gh/log.hpp>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace gh {
namespace detail {

template <typename completion_queue_type>
class session_impl : public session_impl_common {
public:
  /// Constructor
  template <typename duration_type>
  session_impl(
      completion_queue_type& queue, std::unique_ptr<etcdserverpb::Lease::Stub> lease_stub, duration_type desired_TTL)
      : session_impl(queue, std::move(lease_stub), convert_duration(desired_TTL), 0) {
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
      : session_impl(true, queue, std::move(lease_stub), convert_duration(desired_TTL), lease_id) {
  }

  virtual ~session_impl() {
    shutdown();
  }

  /// Revoke the lease
  virtual void revoke() override {
    if (not state_machine_.change_state("revoke()", session_state::revoking)) {
      return;
    }
    // ... here we just block, we could make this asynchronous, but
    // really there is no reason to.  This is running in the session's
    // thread, and there is no more use for the completion queue, no
    // pending operations or anything ...
    grpc::ClientContext context;
    etcdserverpb::LeaseRevokeRequest req;
    req.set_id(lease_id_);
    etcdserverpb::LeaseRevokeResponse resp;
    auto status = lease_client_->LeaseRevoke(&context, req, &resp);
    check_grpc_status(
        status, "session::LeaseRevoke()", " lease_id=", lease_id(), ", response=", print_to_stream(resp));
    GH_LOG(trace) << std::hex << lease_id() << " lease Revoked";
    state_machine_.change_state("revoke()", session_state::revoked);
  }

private:
  /// Common constructor
  session_impl(
      bool, completion_queue_type& queue, std::unique_ptr<etcdserverpb::Lease::Stub> lease_stub,
      std::chrono::milliseconds desired_TTL, std::uint64_t lease_id)
      : session_impl_common(std::move(lease_stub), desired_TTL, lease_id)
      , queue_(queue) {
    preamble();
  }

  /// Requests (or renews) the lease and setup the watcher stream.
  void preamble() try {
    if (not state_machine_.change_state("preamble", session_state::connecting)) {
      throw std::runtime_error("Failed to change state machine to 'connecting");
    }

    // ... we want to block until the keep alive streaming RPC is setup,
    // this is (unfortunately) an asynchronous operation, so we have to
    // do some magic ...
    std::promise<bool> stream_ready;
    auto fut = queue_.async_create_rdwr_stream(
        lease_client_.get(), &etcdserverpb::Lease::Stub::AsyncLeaseKeepAlive, "session/ka_stream", gh::use_future());
    this->ka_stream_ = fut.get();

    if (not state_machine_.change_state("preamble", session_state::connected)) {
      throw std::runtime_error("Failed to change state machine to 'connected");
    }

    // ... the double state transition seems wasteful ...
    if (not state_machine_.change_state("preamble()", session_state::obtaining_lease)) {
      throw std::runtime_error("Failed to change state machine to obtaining_lease");
    }
    // ...request a new lease from the etcd server ...
    etcdserverpb::LeaseGrantRequest req;
    // ... the TTL is is seconds, convert to the right units ...
    auto ttl_seconds = std::chrono::duration_cast<std::chrono::seconds>(desired_TTL_);
    req.set_ttl(ttl_seconds.count());
    req.set_id(lease_id_);

    auto lfut = queue_.async_rpc(
        lease_client_.get(), &etcdserverpb::Lease::Stub::AsyncLeaseGrant, std::move(req),
        "session/preamble/lease_grant", gh::use_future());
    auto resp = lfut.get();

    // TODO() - probably need to retry until it succeeds, with some kind of backoff, and a long timeout ...
    if (resp.error() != "") {
      std::ostringstream os;
      os << "Lease grant request rejected"
         << "\n request=" << print_to_stream(req) << "\n response=" << print_to_stream(resp);
      throw std::runtime_error(os.str());
    }

    if (not state_machine_.change_state("preamble()", session_state::lease_obtained)) {
      throw std::runtime_error("Failed to change state machine to lease_obtained");
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
    if (not state_machine_.change_state("shutdown()", session_state::shutting_down)) {
      return;
    }
    // ... cancel the outstanding timer, if any ...
    if (current_timer_) {
      current_timer_->cancel();
      current_timer_.reset();
    }
    if (ka_stream_) {
      // The KeepAlive stream was already created, we need to close it
      // before shutting down ...
      auto writes_done_complete =
          queue_.async_writes_done(*ka_stream_, "session/shutdown/writes_done", gh::use_future());
      // ... block until it closes ...
      writes_done_complete.get();

      auto finish_complete = queue_.async_finish(*ka_stream_, "session/ka_stream/finish", gh::use_future());
      auto status = finish_complete.get();
      check_grpc_status(status, "session::finish()");
    }
    state_machine_.change_state("shutdown()", session_state::shutdown);
  }

  /// Set a timer to start the next Write/Read cycle.
  void set_timer() {
    if (not state_machine_.change_state("set_timer()", session_state::waiting_for_timer)) {
      return;
    }
    // ... we are going to schedule a new timer, in general, we should
    // only schedule a timer when there are no pending KeepAlive
    // request/responses in the stream.  The AsyncReaderWriter docs says
    // you can only have one outstanding Write() request at a time.  If
    // we started the timer there is no guarantee that the timer won't
    // expire before the next response.

    auto deadline = std::chrono::system_clock::now() + (actual_TTL_ / keep_alives_per_ttl);
    current_timer_ = queue_.make_deadline_timer(
        deadline, "session/set_timer/ttl_refresh", [this](auto const& op, bool ok) { this->on_timeout(op, ok); });
  }

  /// Handle the timer expiration, Write() a new LeaseKeepAlive request.
  void on_timeout(detail::deadline_timer const& op, bool ok) {
    if (not ok) {
      // ... this is a canceled timer ...
      return;
    }
    if (not state_machine_.change_state("on_timeout()", session_state::waiting_for_keep_alive_write)) {
      return;
    }
    etcdserverpb::LeaseKeepAliveRequest req;
    req.set_id(lease_id());

    queue_.async_write(
        *ka_stream_, std::move(req), "session/on_timeout/write", [this](auto op, bool ok) { this->on_write(op, ok); });
  }

  /// Handle the Write() completion, schedule a new LeaseKeepAlive Read().
  void on_write(ka_stream_type::write_op& op, bool ok) {
    if (not ok) {
      // TODO() - consider logging or exceptions in this case (canceled operation) ...
      return;
    }
    if (not state_machine_.change_state("on_write()", session_state::waiting_for_keep_alive_read)) {
      return;
    }
    queue_.async_read(*ka_stream_, "session/on_write/read", [this](auto op, bool ok) { this->on_read(op, ok); });
  }

  /// Handle the Read() completion, schedule a new Timer().
  void on_read(ka_stream_type::read_op& op, bool ok) {
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
};

} // namespace detail
} // namespace gh

#endif // gh_detail_session_impl_hpp
