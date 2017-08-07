#include "gh/watch_election.hpp"
#include <gh/detail/election_observer_impl.hpp>

namespace gh {
watch_election::watch_election(
    std::shared_ptr<active_completion_queue> queue, std::shared_ptr<grpc::Channel> etcd_channel,
    std::string const& election_name)
    : queue_(queue)
    , channel_(etcd_channel)
    , observer_(new ::gh::detail::election_observer_impl<completion_queue<>>(
          election_name, queue_->cq(), etcdserverpb::KV::NewStub(channel_), etcdserverpb::Watch::NewStub(channel_))) {
}
} // namespace gh
