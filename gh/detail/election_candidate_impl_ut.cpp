#define GH_MIN_SEVERITY trace
#include "gh/detail/election_candidate_impl.hpp"

#include <gh/completion_queue.hpp>
#include <gh/detail/mocked_grpc_interceptor.hpp>

#include <gmock/gmock.h>

/// Define helper types and functions used in these tests
namespace {
using completion_queue_type = gh::completion_queue<gh::detail::mocked_grpc_interceptor>;
using candidate_type = gh::detail::election_candidate_impl<completion_queue_type>;
} // anonymous namespace

TEST(election_candidate_impl, basic) {
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;
  using namespace ::testing;
  auto candidate = std::make_unique<candidate_type>(
      queue, 0x123456, std::unique_ptr<etcdserverpb::KV::Stub>(), std::unique_ptr<etcdserverpb::Watch::Stub>(),
      "basic-election", "abc1000", []() {});

  EXPECT_NO_THROW(candidate.reset(nullptr));
}
