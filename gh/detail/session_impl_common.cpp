#include "gh/detail/session_impl_common.hpp"

namespace gh {
namespace detail {

int constexpr session_impl_common::keep_alives_per_ttl;

session_impl_common::session_impl_common(
    std::unique_ptr<etcdserverpb::Lease::Stub> lease_stub, std::chrono::milliseconds desired_TTL,
    std::uint64_t lease_id)
    : session()
    , state_machine_()
    , lease_client_(std::move(lease_stub))
    , ka_stream_()
    , lease_id_(lease_id)
    , desired_TTL_(desired_TTL)
    , actual_TTL_(desired_TTL)
    , current_timer_()
    , ops_() {
}

} // namespace detail
} // namespace gh
