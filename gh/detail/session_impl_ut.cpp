#include "gh/detail/session_impl.hpp"
#include <gh/detail/mocked_grpc_interceptor.hpp>

/// Define helper types and functions used in these tests
namespace {
using completion_queue_type = gh::completion_queue<gh::detail::mocked_grpc_interceptor>;
using session_type = gh::detail::session_impl<completion_queue_type>;

/// Common initialization for all tests
void prepare_mocks_common(completion_queue_type& queue);
} // anonymous namespace

/**
 * @test Verify that gh::detail::session_impl works in the simple case.
 */
TEST(session_impl, basic) {
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;
  prepare_mocks_common(queue);
  using namespace ::testing;
  // ... we expect a call to request a lease ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "session/preamble/lease_grant";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = gh::detail::async_rpc_op<etcdserverpb::LeaseGrantRequest, etcdserverpb::LeaseGrantResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    // ... verify the request is what we expect ...
    EXPECT_EQ(op->request.ttl(), 5);
    EXPECT_EQ(op->request.id(), 0);

    // ... in this test we just assume everything worked, so
    // provide a response ...
    op->response.set_error("");
    op->response.set_id(1000);
    op->response.set_ttl(42);
    // ... and to not forget the callback ...
    bop->callback(*bop, true);
  }));

  // ... and a call to setup a timer ...
  std::shared_ptr<deadline_timer> pending_timer;
  EXPECT_CALL(*queue.interceptor().shared_mock, make_deadline_timer(Truly([](auto op) {
    return op->name == "session/set_timer/ttl_refresh";
  }))).WillOnce(Invoke([r = std::ref(pending_timer)](auto bop) {
    using op_type = gh::detail::deadline_timer;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    // ... the standard trick to downcast shared_ptr<> ...
    r.get() = std::shared_ptr<deadline_timer>(bop, op);
    // ... delay sending the callback until we can test it:
    //  DO NOT CALL YET bop->callback(*bop, true);
  }));

  auto session = std::make_unique<session_type>(queue, std::unique_ptr<etcdserverpb::Lease::Stub>(), 5000ms);
  EXPECT_EQ(session->lease_id(), 1000UL);
  EXPECT_EQ(session->actual_TTL().count(), 42000);
  EXPECT_NO_THROW(session.reset(nullptr));
}

/**
 * @test Verify that gh::detail::session_impl works when the lease fails to be acquired.
 */
TEST(session_impl, lease_error) {
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;
  prepare_mocks_common(queue);
  using namespace ::testing;
  // ... we expect a call to request a lease ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "session/preamble/lease_grant";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = gh::detail::async_rpc_op<etcdserverpb::LeaseGrantRequest, etcdserverpb::LeaseGrantResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    // ... verify the request is what we expect ...
    EXPECT_EQ(op->request.ttl(), 5);
    EXPECT_EQ(op->request.id(), 0);

    // ... in this test we just assume everything worked, so
    // provide a response ...
    op->response.set_error("something broke");
    // ... and to not forget the callback ...
    bop->callback(*bop, true);
  }));

  // ... there should be no calls to setup the timer.
  std::shared_ptr<deadline_timer> pending_timer;
  EXPECT_CALL(*queue.interceptor().shared_mock, make_deadline_timer(Truly([](auto op) {
    return op->name == "session/set_timer/ttl_refresh";
  }))).Times(0);

  std::unique_ptr<session_type> session;

  EXPECT_THROW(
      session = std::make_unique<session_type>(queue, std::unique_ptr<etcdserverpb::Lease::Stub>(), 5000ms),
      std::exception);
}

/**
 * @test Verify that gh::detail::session_impl works when the
 * lease fails to be acquired.
 */
TEST(session_impl, lease_unusual_exception) {
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;
  prepare_mocks_common(queue);
  using namespace ::testing;
  // ... we expect a call to request a lease ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "session/preamble/lease_grant";
  }))).WillOnce(Invoke([](auto bop) { throw std::string("raising std::string is just bad manners"); }));

  // ... there should be no calls to setup the timer.
  std::shared_ptr<deadline_timer> pending_timer;
  EXPECT_CALL(*queue.interceptor().shared_mock, make_deadline_timer(Truly([](auto op) {
    return op->name == "session/set_timer/ttl_refresh";
  }))).Times(0);

  std::unique_ptr<session_type> session;

  EXPECT_THROW(
      session = std::make_unique<session_type>(queue, std::unique_ptr<etcdserverpb::Lease::Stub>(), 5000ms),
      std::string);
}

/**
 * @test Verify that gh::detail::session_impl works for a full lifecycle (create, get lease, some keep alive, revoke).
 */
TEST(session_impl, full_lifecycle) {
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;
  prepare_mocks_common(queue);
  using namespace ::testing;
  // ... we expect a call to request a lease ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "session/preamble/lease_grant";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = gh::detail::async_rpc_op<etcdserverpb::LeaseGrantRequest, etcdserverpb::LeaseGrantResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    // ... verify the request is what we expect ...
    EXPECT_EQ(op->request.ttl(), 5);
    EXPECT_EQ(op->request.id(), 0);

    // ... in this test we just assume everything worked, so provide a response ...
    op->response.set_error("");
    op->response.set_id(1000);
    op->response.set_ttl(42);
    // ... and to not forget the callback ...
    bop->callback(*bop, true);
  }));

  // ... and a call to setup a timer ...
  std::shared_ptr<deadline_timer> pending_timer;
  auto handle_timer = [r = std::ref(pending_timer)](auto bop) {
    using op_type = gh::detail::deadline_timer;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    // ... the standard trick to downcast shared_ptr<> ...
    r.get() = std::shared_ptr<deadline_timer>(bop, op);
  };
  EXPECT_CALL(*queue.interceptor().shared_mock, make_deadline_timer(Truly([](auto op) {
    return op->name == "session/set_timer/ttl_refresh";
  }))).WillRepeatedly(Invoke(handle_timer));

  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(Truly([](auto op) {
    return op->name == "session/on_timeout/write";
  }))).WillRepeatedly(Invoke([](auto bop) {
    using op_type = gh::detail::write_op<etcdserverpb::LeaseKeepAliveRequest>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    // ... verify the request is what we expect ...
    EXPECT_EQ(op->request.id(), 1000);

    // ... and to not forget the callback ...
    bop->callback(*bop, true);
  }));

  EXPECT_CALL(*queue.interceptor().shared_mock, async_read(Truly([](auto op) {
    return op->name == "session/on_write/read";
  }))).WillRepeatedly(Invoke([](auto bop) {
    using op_type = gh::detail::read_op<etcdserverpb::LeaseKeepAliveResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);

    // ... provide a response value
    op->response.set_ttl(24);
    // ... and to not forget the callback ...
    bop->callback(*bop, true);
  }));

  auto session = std::make_unique<session_type>(queue, std::unique_ptr<etcdserverpb::Lease::Stub>(), 5000ms);
  EXPECT_EQ(session->lease_id(), 1000UL);
  EXPECT_EQ(session->actual_TTL().count(), 42000);
  ASSERT_TRUE(session->is_active());

  // ... reset the timer to verify it is setup again after the timer -> write -> read cycle ...
  auto p = std::move(pending_timer);
  ASSERT_FALSE((bool)pending_timer);
  p->callback(*p, true);
  // ... once a complete timer -> write -> read cycle goes through we expect the TTL to change (see the mock
  // expectation setup above) ...
  EXPECT_EQ(session->actual_TTL().count(), 24000);
  ASSERT_TRUE((bool)pending_timer);
  p = std::move(pending_timer);
  p->callback(*p, true);
  EXPECT_EQ(session->actual_TTL().count(), 24000);
  ASSERT_TRUE((bool)pending_timer);

  // ... we should still be connected ...
  ASSERT_TRUE(session->is_active());

  // ... okay, after two cycles let's revoke the lease ...
  session->revoke();
  ASSERT_FALSE(session->is_active());

  EXPECT_NO_THROW(session.reset(nullptr));
}

/**
 * @test Verify that gh::detail::session_impl handles races between revoke() and the timer.
 */
TEST(session_impl, race_timer) {
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;
  prepare_mocks_common(queue);
  using namespace ::testing;
  // ... we expect a call to request a lease ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "session/preamble/lease_grant";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = gh::detail::async_rpc_op<etcdserverpb::LeaseGrantRequest, etcdserverpb::LeaseGrantResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    // ... verify the request is what we expect ...
    EXPECT_EQ(op->request.ttl(), 5);
    EXPECT_EQ(op->request.id(), 0);

    // ... in this test we just assume everything worked, so provide a response ...
    op->response.set_error("");
    op->response.set_id(1000);
    op->response.set_ttl(42);
    // ... and to not forget the callback ...
    bop->callback(*bop, true);
  }));

  // ... and a call to setup a timer ...
  std::shared_ptr<deadline_timer> pending_timer;
  auto handle_timer = [r = std::ref(pending_timer)](auto bop) {
    using op_type = gh::detail::deadline_timer;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    // ... the standard trick to downcast shared_ptr<> ...
    r.get() = std::shared_ptr<deadline_timer>(bop, op);
  };
  EXPECT_CALL(*queue.interceptor().shared_mock, make_deadline_timer(Truly([](auto op) {
    return op->name == "session/set_timer/ttl_refresh";
  }))).WillRepeatedly(Invoke(handle_timer));

  auto session = std::make_unique<session_type>(queue, std::unique_ptr<etcdserverpb::Lease::Stub>(), 5000ms);
  EXPECT_EQ(session->lease_id(), 1000UL);
  EXPECT_EQ(session->actual_TTL().count(), 42000);
  ASSERT_TRUE(session->is_active());

  ASSERT_TRUE((bool)pending_timer);

  // ... prepare to hold the LeaseRevoke() call ...
  using revoke_op_type = async_rpc_op<etcdserverpb::LeaseRevokeRequest, etcdserverpb::LeaseRevokeResponse>;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "session/revoke/lease_revoke";
  }))).WillOnce(Invoke([t = std::ref(pending_timer), &session](auto bop) {
    ASSERT_FALSE(session->is_active());
    auto* op = dynamic_cast<revoke_op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    // ... fire the timer, which should not create a new timer as it usual does ...
    auto p = std::move(t.get());
    p->callback(*p, true);
    // ... the timer should not reconnect ...
    ASSERT_FALSE((bool)t.get());
    bop->callback(*bop, true);
  }));

  EXPECT_NO_THROW(session->revoke());
  ASSERT_FALSE(session->is_active());
  EXPECT_NO_THROW(session.reset(nullptr));
}

namespace {
void prepare_mocks_common(completion_queue_type& queue) {
  using namespace ::testing;
  // ... on most calls we just invoke the application's callback
  // immediately ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_read(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_create_rdwr_stream(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_writes_done(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_finish(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, make_deadline_timer(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
}
} // anonymous namespace
