#define GH_MIN_SEVERITY trace
#include "gh/detail/cluster_membership_impl.hpp"
#include <gh/detail/exponential_backoff.hpp>
#include <gh/detail/mocked_grpc_interceptor.hpp>

#include <gmock/gmock.h>

/// Define helper types and functions used in these tests
namespace {
using interceptor_type = gh::detail::mocked_grpc_interceptor;
using completion_queue_type = gh::completion_queue<interceptor_type>;
using cluster_membership_type = gh::detail::cluster_membership_impl<interceptor_type>;
} // anonymous namespace


/// @test Verify that cluster membership works correctly in the normal lifecycle.
TEST(cluster_membership_impl, normal_lifecycle) {
  using namespace std::chrono_literals;
  using namespace gh::detail;
  using namespace testing;

  using namespace std::chrono_literals;
  completion_queue_type queue;
  std::shared_ptr<grpc::ChannelCredentials> credentials;
  auto backoff = std::make_shared<gh::detail::exponential_backoff>(10ms, 24h, 1000);
  cluster_membership_type cluster(queue, credentials, backoff, "localhost:22379", 30s);

  std::shared_ptr<deadline_timer> pending_timer;
  auto handle_timer = [r = std::ref(pending_timer)](auto bop) {
    auto* op = dynamic_cast<deadline_timer*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    // ... the standard trick to downcast shared_ptr<> ...
    r.get() = std::shared_ptr<deadline_timer>(bop, op);
  };
  EXPECT_CALL(*queue.interceptor().shared_mock, make_deadline_timer(Truly([](auto op) {
    return op->name == "cluster_membership/refresh_timer";
  }))).WillRepeatedly(Invoke(handle_timer));

  // ... we expect a call to request a lease ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "cluster_membership/member_list";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = cluster_membership_type::member_list_op;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);

    // ... in this test we just assume everything worked, so provide a response ...
    auto& m0 = *op->response.add_members();
    *m0.add_clienturls() = std::string("url0.0");
    *m0.add_clienturls() = std::string("url0.1");
    auto& m1 = *op->response.add_members();
    *m1.add_clienturls() = std::string("url1.0");
    *m1.add_clienturls() = std::string("url1.1");
    // ... and to not forget the callback ...
    bop->callback(*bop, true);
  }));

  EXPECT_NO_THROW(cluster.startup());
  auto pt = std::move(pending_timer);
  ASSERT_TRUE(pt);
  pt->callback(*pt, true);

  auto urls = cluster.current_urls();
  EXPECT_THAT(urls, ElementsAre("url0.0", "url0.1", "url1.0", "url1.1"));

  EXPECT_NO_THROW(cluster.shutdown());
}

/// @test Verify that cluster membership works correctly even with some failures.
TEST(cluster_membership_impl, with_failure) {
  using namespace std::chrono_literals;
  using namespace gh::detail;
  using namespace testing;

  using namespace std::chrono_literals;
  completion_queue_type queue;
  std::shared_ptr<grpc::ChannelCredentials> credentials;
  auto backoff = std::make_shared<gh::detail::exponential_backoff>(10ms, 24h, 1000);
  cluster_membership_type cluster(queue, credentials, backoff, "localhost:22379", 30s);

  std::shared_ptr<deadline_timer> pending_timer;
  auto handle_timer = [r = std::ref(pending_timer)](auto bop) {
    auto* op = dynamic_cast<deadline_timer*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    // ... the standard trick to downcast shared_ptr<> ...
    r.get() = std::shared_ptr<deadline_timer>(bop, op);
  };
  EXPECT_CALL(*queue.interceptor().shared_mock, make_deadline_timer(Truly([](auto op) {
    return op->name == "cluster_membership/refresh_timer";
  }))).WillRepeatedly(Invoke(handle_timer));

  // ... we expect a call to request a lease ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "cluster_membership/member_list";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = cluster_membership_type::member_list_op;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);

    // ... in this test we just assume everything worked, so provide a response ...
    auto& m0 = *op->response.add_members();
    *m0.add_clienturls() = std::string("url0.0");
    *m0.add_clienturls() = std::string("url0.1");
    *m0.add_clienturls() = std::string("url0.2");
    // ... and to not forget the callback ...
    bop->callback(*bop, true);
  }));

  EXPECT_NO_THROW(cluster.startup());
  auto pt = std::move(pending_timer);
  ASSERT_TRUE(pt);
  pt->callback(*pt, true);

  auto urls = cluster.current_urls();
  EXPECT_THAT(urls, ElementsAre("url0.0", "url0.1", "url0.2"));

  // ... simulate some failures in the second iteration, first a canceled operation ...
  auto& call0 = EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "cluster_membership/member_list";
  })));
  call0.WillOnce(Invoke([](auto bop) {
    using op_type = cluster_membership_type::member_list_op;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    bop->callback(*bop, false);
  }));

  // ... then some kind of failure ...
  auto& call1 = EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "cluster_membership/member_list";
  })));
  call1.After(call0).WillOnce(Invoke([](auto bop) {
    using op_type = cluster_membership_type::member_list_op;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    op->status = grpc::Status(grpc::DEADLINE_EXCEEDED, "too long", "really, I mean it.");
    bop->callback(*bop, true);
  }));

  // ... then some kind of failure ...
  auto& call2 = EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "cluster_membership/member_list";
  })));
  call2.After(call1).WillOnce(Invoke([](auto bop) {
    using op_type = cluster_membership_type::member_list_op;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    op->status = grpc::Status(grpc::UNKNOWN, "something bad", "without more details.");
    bop->callback(*bop, true);
  }));

  pt = std::move(pending_timer);
  ASSERT_TRUE(pt);
  pt->callback(*pt, true);

  EXPECT_NO_THROW(cluster.shutdown());
}
