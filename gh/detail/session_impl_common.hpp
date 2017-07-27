#ifndef gh_detail_session_impl_common_hpp
#define gh_detail_session_impl_common_hpp

#include <gh/detail/async_op_counter.hpp>
#include <gh/detail/deadline_timer.hpp>
#include <gh/detail/session_state_machine.hpp>
#include <gh/detail/stream_async_ops.hpp>
#include <gh/session.hpp>

#include <etcd/etcdserver/etcdserverpb/rpc.grpc.pb.h>

namespace gh {
namespace detail {

/**
 * Refactor common implementation for @c gh::detail::session_impl
 */
class session_impl_common : public ::gh::session {
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

  std::uint64_t lease_id() const override {
    return lease_id_;
  }

  std::chrono::milliseconds actual_TTL() const override {
    return actual_TTL_;
  }

  bool is_active() const override {
    auto c = state_machine_.current();
    using s = session_state;
    return c == s::connected or c == s::waiting_for_timer or c == s::waiting_for_keep_alive_write or
           c == s::waiting_for_keep_alive_read;
  }

  /// Convert a duration to the preferred units in this class
  template <typename other_duration_type>
  static duration_type convert_duration(other_duration_type d) {
    return std::chrono::duration_cast<duration_type>(d);
  }

protected:
  /// Only derived classes can construct this object.
  session_impl_common(
      std::unique_ptr<etcdserverpb::Lease::Stub> lease_stub, std::chrono::milliseconds desired_TTL,
      std::uint64_t lease_id);

protected:
  detail::session_state_machine state_machine_;

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

  async_op_counter ops_;
};

} // namespace detail
} // namespace gh

#endif // gh_detail_session_impl_common_hpp
