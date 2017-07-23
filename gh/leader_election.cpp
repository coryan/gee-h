#include "gh/leader_election.hpp"
#include <gh/detail/election_candidate_impl.hpp>
#include <gh/detail/session_impl.hpp>

namespace gh {
leader_election::leader_election(
    bool shared, std::shared_ptr<active_completion_queue> queue, std::shared_ptr<grpc::Channel> etcd_channel,
    std::string const& election_name, std::string const& participant_value, std::chrono::milliseconds desired_TTL,
    std::uint64_t lease_id)
    : queue_(queue)
    , channel_(etcd_channel)
    , session_(new detail::session_impl<completion_queue<>>(
          queue->cq(), etcdserverpb::Lease::NewStub(channel_), desired_TTL, lease_id))
    , candidate_(new detail::election_candidate_impl<completion_queue<>>(
          queue_->cq(), session_->lease_id(), etcdserverpb::KV::NewStub(channel_),
          etcdserverpb::Watch::NewStub(channel_), election_name, participant_value)) {
}

leader_election::~leader_election() noexcept(false) {
}
} // namespace gh
