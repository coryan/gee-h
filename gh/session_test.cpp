#include "gh/session.hpp"
#include <gh/active_completion_queue.hpp>
#include <gh/detail/session_impl.hpp>

#include <gtest/gtest.h>

/// Define helper types and functions used in these tests
namespace {
using session_type = gh::detail::session_impl<gh::completion_queue<>>;
} // anonymous namespace

/**
 * @test Verify that one can create and destroy a session.
 */
TEST(session_test, session_basic) {
  using namespace std::chrono_literals;

  std::string const address = "localhost:22379";
  auto etcd_channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
  auto queue = std::make_shared<gh::active_completion_queue>();

  // We want to test that the destructor does not throw, so use a
  // smart pointer ...
  auto session = std::make_unique<session_type>(queue->cq(), etcdserverpb::Lease::NewStub(etcd_channel), 5s);
  ASSERT_NE(session->lease_id(), 0UL);
  ASSERT_NO_THROW(session.reset());
}

/**
 * @test Verify that one can create, run, and stop a completion queue.
 */
TEST(session_test, session_normal) {
  using namespace std::chrono_literals;

  std::string const address = "localhost:22379";
  auto etcd_channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
  auto queue = std::make_shared<gh::active_completion_queue>();

  session_type session(queue->cq(), etcdserverpb::Lease::NewStub(etcd_channel), 5s);
  EXPECT_NE(session.lease_id(), 0UL);
  ASSERT_TRUE(session.is_active());

  session.revoke();
  ASSERT_FALSE(session.is_active());
}

/**
 * @test Verify that one can create, run, and stop a completion queue.
 */
TEST(session_test, session_long) {
  using namespace std::chrono_literals;

  std::string const address = "localhost:22379";
  auto etcd_channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
  auto queue = std::make_shared<gh::active_completion_queue>();

  session_type session(queue->cq(), etcdserverpb::Lease::NewStub(etcd_channel), 1s);
  ASSERT_NE(session.lease_id(), 0UL);
  ASSERT_TRUE(session.is_active());

  // ... keep the session open for a while ...
  std::this_thread::sleep_for(5000ms);
  ASSERT_TRUE(session.is_active());

  session.revoke();
  ASSERT_FALSE(session.is_active());
}
