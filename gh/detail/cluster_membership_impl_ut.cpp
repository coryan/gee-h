#define GH_MIN_SEVERITY trace
#include "gh/detail/cluster_membership_impl.hpp"
#include <gh/completion_queue.hpp>
#include <gh/detail/exponential_backoff.hpp>
#include <gh/detail/mocked_grpc_interceptor.hpp>

#include <gmock/gmock.h>

/// Define helper types and functions used in these tests
namespace {
using completion_queue_type = gh::completion_queue<gh::detail::mocked_grpc_interceptor>;
using cluster_membership_type = gh::detail::cluster_membership_impl<completion_queue_type>;
}

/// @test Verify that we can construct a gh::detail::cluster_membership<> object.
TEST(cluster_membership_impl, basic) {
  using namespace std::chrono_literals;
  completion_queue_type queue;
  std::shared_ptr<grpc::ChannelCredentials> credentials;
  auto backoff = std::make_shared<gh::detail::exponential_backoff>(10ms, 24h, 1000);
  cluster_membership_type cluster(queue, credentials, backoff, "localhost:22379");

  EXPECT_NO_THROW(cluster.startup());
}