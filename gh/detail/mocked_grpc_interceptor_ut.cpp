#include "gh/detail/mocked_grpc_interceptor.hpp"
#include <gh/completion_queue.hpp>

#include <etcd/etcdserver/etcdserverpb/rpc.grpc.pb.h>

/**
 * @test Verify that we can mock timers using gh::detail::mocked_grpc_interceptor.
 */
TEST(mocked_grpc_interceptor, deadline_timer) {
  using namespace std::chrono_literals;
  using namespace gh::detail;

  using completion_queue_type = gh::completion_queue<gh::detail::mocked_grpc_interceptor>;
  completion_queue_type queue;

  std::vector<std::shared_ptr<deadline_timer>> pending_timer;
  using namespace ::testing;
  auto action = [&pending_timer](auto bop) {
    using op_type = gh::detail::deadline_timer;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    // ... the standard trick to downcast shared_ptr<> ...
    pending_timer.push_back(std::shared_ptr<deadline_timer>(bop, op));
    // ... most of the time we can invoke the callback immediately in the mock object, like so:
    //      bop->callback(*bop, true);
    // ... we do not do this here because we want the application to be able to test it ...
  };
  EXPECT_CALL(*queue.interceptor().shared_mock, make_deadline_timer(Truly([](auto op) {
    return op->name == "testing/deadline_timer";
  }))).WillOnce(Invoke(action));

  EXPECT_CALL(*queue.interceptor().shared_mock, make_deadline_timer(Truly([](auto op) {
    return op->name == "testing/relative_timer";
  }))).WillOnce(Invoke(action));

  int cnt_ok = 0;
  int cnt_canceled = 0;
  auto handle_timer = [&cnt_ok, &cnt_canceled](auto& op, bool ok) {
    if (ok) {
      ++cnt_ok;
    } else {
      ++cnt_canceled;
    }
  };
  queue.make_relative_timer(100ms, "testing/relative_timer", handle_timer);
  ASSERT_EQ(pending_timer.size(), 1UL);
  ASSERT_EQ(cnt_ok, 0);
  ASSERT_EQ(cnt_canceled, 0);
  pending_timer[0]->callback(*pending_timer[0], true);
  ASSERT_EQ(cnt_ok, 1);
  ASSERT_EQ(cnt_canceled, 0);
  ASSERT_NO_THROW(pending_timer.pop_back());

  queue.make_deadline_timer(std::chrono::system_clock::now() + 100ms, "testing/deadline_timer", handle_timer);
  ASSERT_EQ(pending_timer.size(), 1UL);
  ASSERT_EQ(cnt_ok, 1);
  ASSERT_EQ(cnt_canceled, 0);
  pending_timer[0]->callback(*pending_timer[0], false);
  ASSERT_EQ(cnt_ok, 1);
  ASSERT_EQ(cnt_canceled, 1);
  ASSERT_NO_THROW(pending_timer.pop_back());
}

/**
 * @test Make sure we can mock async_rpc() calls a completion_queue.
 */
TEST(mocked_grpc_interceptor, async_rpc) {
  using namespace std::chrono_literals;

  // Create a null lease object, we do not need (or want) a real
  // connection for mocked operations ...
  std::shared_ptr<etcdserverpb::Lease::Stub> lease;

  using namespace gh;
  completion_queue<detail::mocked_grpc_interceptor> queue;

  // Prepare the Mock to save the asynchronous operation state,
  // normally you would simply invoke the callback in the mock action,
  // but this test wants to verify what happens if there is a delay
  // ...
  using ::testing::_;
  using ::testing::Invoke;
  std::shared_ptr<gh::detail::base_async_op> last_op;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(_)).WillRepeatedly(Invoke([&last_op](auto const& op) mutable {
    last_op = op;
  }));

  // ... make the request, that will post operations to the mock
  // completion queue ...
  etcdserverpb::LeaseGrantRequest req;
  req.set_ttl(5); // in seconds
  req.set_id(0);  // let the server pick the lease_id
  auto fut = queue.async_rpc(
      lease.get(), &etcdserverpb::Lease::Stub::AsyncLeaseGrant, std::move(req), "test/Lease/future", gh::use_future());

  // ... verify the results are not there, the interceptor should have
  // stopped the call from going out ...
  auto wait_response = fut.wait_for(10ms);
  ASSERT_EQ(wait_response, std::future_status::timeout);

  // ... we need to fill the response parameters, which again could be
  // done in the mock action, but we are delaying the operations to
  // verify the std::promise is not immediately satisfied ...
  ASSERT_TRUE((bool)last_op);
  {
    auto op =
        dynamic_cast<gh::detail::async_rpc_op<etcdserverpb::LeaseGrantRequest, etcdserverpb::LeaseGrantResponse>*>(
            last_op.get());
    ASSERT_TRUE(op != nullptr);
    op->response.set_ttl(7);
    op->response.set_id(123456UL);
  }
  // ... now we can execute the callback ...
  last_op->callback(*last_op, true);

  // ... that must make the result read or we will get a deadlock ...
  wait_response = fut.wait_for(10ms);
  ASSERT_EQ(wait_response, std::future_status::ready);

  // ... get the response ...
  auto response = fut.get();
  ASSERT_EQ(response.ttl(), 7);
  ASSERT_EQ(response.id(), 123456);
}

/**
 * @test Verify canceled RPCs result in exception for the std::promise.
 */
TEST(mocked_grpc_interceptor, async_rpc_cancelled) {
  using namespace std::chrono_literals;

  // Create a null lease object, we do not need (or want) a real
  // connection for mocked operations ...
  std::shared_ptr<etcdserverpb::Lease::Stub> lease;

  using namespace gh;
  completion_queue<detail::mocked_grpc_interceptor> queue;

  // Prepare the Mock to save the asynchronous operation state,
  // normally you would simply invoke the callback in the mock action,
  // but this test wants to verify what happens if there is a delay
  // ...
  using ::testing::_;
  using ::testing::Invoke;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(_)).WillRepeatedly(Invoke([](auto bop) mutable {
    bop->callback(*bop, false);
  }));

  // ... make the request, that will post operations to the mock
  // completion queue ...
  etcdserverpb::LeaseGrantRequest req;
  req.set_ttl(5); // in seconds
  req.set_id(0);  // let the server pick the lease_id
  auto fut = queue.async_rpc(
      lease.get(), &etcdserverpb::Lease::Stub::AsyncLeaseGrant, std::move(req), "test/Lease/future/cancelled",
      gh::use_future());

  // ... check that the operation was immediately cancelled ...
  ASSERT_EQ(fut.wait_for(0ms), std::future_status::ready);

  // ... and the promise was satisfied with an exception ...
  ASSERT_THROW(fut.get(), std::exception);
}

/**
 * @test Verify creation of rdwr RPC streams is intercepted.
 */
TEST(mocked_grpc_interceptor, create_rdwr_stream_future) {
  using namespace std::chrono_literals;

  // Create a null lease object, we do not need (or want) a real
  // connection for mocked operations ...
  std::shared_ptr<etcdserverpb::Lease::Stub> lease;

  using namespace gh;
  completion_queue<detail::mocked_grpc_interceptor> queue;

  // Prepare the Mock to save the asynchronous operation state,
  // normally you would simply invoke the callback in the mock action,
  // but this test wants to verify what happens if there is a delay
  // ...
  using ::testing::_;
  using ::testing::Invoke;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_create_rdwr_stream(_)).WillRepeatedly(Invoke([](auto op) mutable {
    op->callback(*op, true);
  }));

  // ... make the request, that will post operations to the mock
  // completion queue ...
  auto fut = queue.async_create_rdwr_stream(
      lease.get(), &etcdserverpb::Lease::Stub::AsyncLeaseKeepAlive, "test/CreateLeaseKeepAlive/future",
      gh::use_future());

  // ... check that the promise was immediately satisfied ...
  ASSERT_EQ(fut.wait_for(0ms), std::future_status::ready);

  // ... and that it did not raise an exception ..
  ASSERT_NO_THROW(fut.get());
  // ... we do not check the value because that is too complicated to
  // setup ...

  // ... change the mock to start canceling operations ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_create_rdwr_stream(_)).WillRepeatedly(Invoke([](auto op) mutable {
    op->callback(*op, false);
  }));
  // ... make another request, it should fail ...
  auto fut2 = queue.async_create_rdwr_stream(
      lease.get(), &etcdserverpb::Lease::Stub::AsyncLeaseKeepAlive, "test/CreateLeaseKeepAlive/future/cancelled",
      gh::use_future());
  ASSERT_EQ(fut2.wait_for(0ms), std::future_status::ready);
  ASSERT_THROW(fut2.get(), std::exception);
}

/**
 * @test Verify creation of rdwr RPC streams is intercepted.
 */
TEST(mocked_grpc_interceptor, create_rdwr_stream_functor) {
  using namespace std::chrono_literals;

  // Create a null lease object, we do not need (or want) a real
  // connection for mocked operations ...
  std::shared_ptr<etcdserverpb::Lease::Stub> lease;

  using namespace gh;
  completion_queue<detail::mocked_grpc_interceptor> queue;

  // Prepare the Mock to save the asynchronous operation state,
  // normally you would simply invoke the callback in the mock action,
  // but this test wants to verify what happens if there is a delay
  // ...
  using ::testing::_;
  using ::testing::Invoke;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_create_rdwr_stream(_)).WillRepeatedly(Invoke([](auto op) mutable {
    op->callback(*op, true);
  }));

  // ... make the request, that will post operations to the mock
  // completion queue ...
  int counter = 0;
  int* cnt = &counter;
  queue.async_create_rdwr_stream(
      lease.get(), &etcdserverpb::Lease::Stub::AsyncLeaseKeepAlive, "test/CreateLeaseKeepAlive/functor",
      [cnt](auto op, bool ok) { *cnt += int(ok); });

  ASSERT_EQ(counter, 1);
}

/**
 * @test Verify Write() operations on rdwr RPC streams are intercepted.
 */
TEST(mocked_grpc_interceptor, rdwr_stream_write_functor) {
  using namespace std::chrono_literals;

  // Create a null lease object, we do not need (or want) a real
  // connection for mocked operations ...
  std::shared_ptr<etcdserverpb::Lease::Stub> lease;

  using namespace gh;
  completion_queue<detail::mocked_grpc_interceptor> queue;

  // Prepare the Mock to save the asynchronous operation state,
  // normally you would simply invoke the callback in the mock action,
  // but this test wants to verify what happens if there is a delay
  // ...
  using ::testing::_;
  using ::testing::Invoke;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(_)).WillRepeatedly(Invoke([](auto op) mutable {
    op->callback(*op, true);
  }));

  // ... make the request, that will post operations to the mock
  // completion queue ...
  int counter = 0;
  int* cnt = &counter;
  using stream_type =
  detail::async_rdwr_stream<etcdserverpb::LeaseKeepAliveRequest, etcdserverpb::LeaseKeepAliveResponse>;
  stream_type stream;
  etcdserverpb::LeaseKeepAliveRequest req;
  req.set_id(123456UL);
  queue.async_write(
      stream, std::move(req), "test/AsyncLeaseKeepAlive::Write/functor", [cnt](auto op, bool ok) { *cnt += int(ok); });

  ASSERT_EQ(counter, 1);
}

/**
 * @test Verify Read() operations on rdwr RPC streams are intercepted.
 */
TEST(mocked_grpc_interceptor, rdwr_stream_read_functor) {
  using namespace std::chrono_literals;

  // Create a null lease object, we do not need (or want) a real
  // connection for mocked operations ...
  std::shared_ptr<etcdserverpb::Lease::Stub> lease;

  using namespace gh;
  completion_queue<detail::mocked_grpc_interceptor> queue;

  // Prepare the Mock to save the asynchronous operation state,
  // normally you would simply invoke the callback in the mock action,
  // but this test wants to verify what happens if there is a delay
  // ...
  using ::testing::_;
  using ::testing::Invoke;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_read(_)).WillRepeatedly(Invoke([](auto op) mutable {
    op->callback(*op, true);
  }));

  // ... make the request, that will post operations to the mock
  // completion queue ...
  int counter = 0;
  int* cnt = &counter;
  using stream_type =
  detail::async_rdwr_stream<etcdserverpb::LeaseKeepAliveRequest, etcdserverpb::LeaseKeepAliveResponse>;
  stream_type stream;
  queue.async_read(stream, "test/AsyncLeaseKeepAlive::Read/functor", [cnt](auto op, bool ok) { *cnt += int(ok); });

  ASSERT_EQ(counter, 1);
}

#if 0

/**
 * @test Verify WritesDone() operations on rdwr RPC streams are intercepted.
 */
TEST(mocked_grpc_interceptor, rdwr_stream_writes_done_functor) {
  using namespace std::chrono_literals;

  // Create a null lease object, we do not need (or want) a real
  // connection for mocked operations ...
  std::shared_ptr<etcdserverpb::Lease::Stub> lease;

  using namespace gh;
  completion_queue<detail::mocked_grpc_interceptor> queue;

  // Prepare the Mock to save the asynchronous operation state,
  // normally you would simply invoke the callback in the mock action,
  // but this test wants to verify what happens if there is a delay
  // ...
  using ::testing::_;
  using ::testing::Invoke;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_writes_done(_)).WillRepeatedly(Invoke([](auto op) mutable {
    op->callback(*op, true);
  }));

  // ... make the request, that will post operations to the mock
  // completion queue ...
  int counter = 0;
  int* cnt = &counter;
  using stream_type =
      detail::async_rdwr_stream<etcdserverpb::LeaseKeepAliveRequest, etcdserverpb::LeaseKeepAliveResponse>;
  stream_type stream;
  queue.async_writes_done(
      stream, "test/AsyncLeaseKeepAlive::WriteDone/functor", [cnt](auto op, bool ok) { *cnt += int(ok); });

 ASSERT_EQ(counter, 1);
}

/**
 * @test Verify WritesDone() operations with use_future() work as expected.
 */
TEST(mocked_grpc_interceptor, rdwr_stream_writes_done_future) {
  using namespace std::chrono_literals;

  // Create a null lease object, we do not need (or want) a real
  // connection for mocked operations ...
  std::shared_ptr<etcdserverpb::Lease::Stub> lease;

  using namespace gh;
  completion_queue<detail::mocked_grpc_interceptor> queue;

  // Prepare the Mock to save the asynchronous operation state,
  // normally you would simply invoke the callback in the mock action,
  // but this test wants to verify what happens if there is a delay
  // ...
  using ::testing::_;
  using ::testing::Invoke;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_writes_done(_)).WillRepeatedly(Invoke([](auto op) mutable {
    op->callback(*op, true);
  }));

  // ... make the request, that will post operations to the mock
  // completion queue ...
  using stream_type =
      detail::async_rdwr_stream<etcdserverpb::LeaseKeepAliveRequest, etcdserverpb::LeaseKeepAliveResponse>;
  stream_type stream;
  auto fut = queue.async_writes_done(stream, "test/AsyncLeaseKeepAlive::WritesDone/future", gh::use_future());
  auto wait_response = fut.wait_for(0ms);
  ASSERT_EQ(wait_response, std::future_status::ready);
  ASSERT_NO_THROW(fut.get());

  // ... also test cancelations ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_writes_done(_)).WillRepeatedly(Invoke([](auto op) mutable {
    op->callback(*op, false);
  }));
  auto fut2 =
      queue.async_writes_done(stream, "test/AsyncLeaseKeepAlive::WritesDone/future/canceled", gh::use_future());
  wait_response = fut2.wait_for(0ms);
  ASSERT_EQ(wait_response, std::future_status::ready);
  ASSERT_THROW(fut2.get(), std::exception);
}
/**
 * @test Verify Finish() operations on rdwr RPC streams are intercepted.
 */
TEST(mocked_grpc_interceptor, rdwr_stream_finish_functor) {
  using namespace std::chrono_literals;

  // Create a null lease object, we do not need (or want) a real
  // connection for mocked operations ...
  std::shared_ptr<etcdserverpb::Lease::Stub> lease;

  using namespace gh;
  completion_queue<detail::mocked_grpc_interceptor> queue;

  // Prepare the Mock to save the asynchronous operation state,
  // normally you would simply invoke the callback in the mock action,
  // but this test wants to verify what happens if there is a delay
  // ...
  using ::testing::_;
  using ::testing::Invoke;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_finish(_)).WillRepeatedly(Invoke([](auto op) mutable {
    op->callback(*op, true);
  }));

  // ... make the request, that will post operations to the mock
  // completion queue ...
  int counter = 0;
  int* cnt = &counter;
  using stream_type =
      detail::async_rdwr_stream<etcdserverpb::LeaseKeepAliveRequest, etcdserverpb::LeaseKeepAliveResponse>;
  stream_type stream;
  queue.async_finish(stream, "test/AsyncLeaseKeepAlive::Finish/functor", [cnt](auto op, bool ok) { *cnt += int(ok); });

 ASSERT_EQ(counter, 1);
}

/**
 * @test Verify Finish() operations with use_future() work as expected.
 */
TEST(mocked_grpc_interceptor, rdwr_stream_finish_future) {
  using namespace std::chrono_literals;

  // Create a null lease object, we do not need (or want) a real
  // connection for mocked operations ...
  std::shared_ptr<etcdserverpb::Lease::Stub> lease;

  using namespace gh;
  completion_queue<detail::mocked_grpc_interceptor> queue;

  // Prepare the Mock to save the asynchronous operation state,
  // normally you would simply invoke the callback in the mock action,
  // but this test wants to verify what happens if there is a delay
  // ...
  using ::testing::_;
  using ::testing::Invoke;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_finish(_)).WillRepeatedly(Invoke([](auto op) mutable {
    op->callback(*op, true);
  }));

  // ... make the request, that will post operations to the mock
  // completion queue ...
  using stream_type =
      detail::async_rdwr_stream<etcdserverpb::LeaseKeepAliveRequest, etcdserverpb::LeaseKeepAliveResponse>;
  stream_type stream;
  auto fut = queue.async_finish(stream, "test/AsyncLeaseKeepAlive::Finish/future", gh::use_future());
  auto wait_response = fut.wait_for(0ms);
  ASSERT_EQ(wait_response, std::future_status::ready);
  ASSERT_NO_THROW(fut.get());

  // ... also test cancelations ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_finish(_)).WillRepeatedly(Invoke([](auto op) mutable {
    op->callback(*op, false);
  }));
  auto fut2 = queue.async_finish(stream, "test/AsyncLeaseKeepAlive::Finish/future/canceled", gh::use_future());
  wait_response = fut2.wait_for(0ms);
  ASSERT_EQ(wait_response, std::future_status::ready);
  ASSERT_THROW(fut2.get(), std::exception);
}
#endif // 0