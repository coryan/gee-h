#define GH_MIN_SEVERITY trace
#include "gh/detail/election_candidate_impl.hpp"

#include <gh/completion_queue.hpp>
#include <gh/detail/mocked_grpc_interceptor.hpp>
#include <gh/detail/stream_future_status.hpp>

#include <gmock/gmock.h>

/// Define helper types and functions used in these tests
namespace {
using completion_queue_type = gh::completion_queue<gh::detail::mocked_grpc_interceptor>;
using candidate_type = gh::detail::election_candidate_impl<completion_queue_type>;
} // anonymous namespace

/// @test verify gh::detail::election_candidate_impl<> instances can be created and destructed.
TEST(election_candidate_impl, basic) {
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;
  using namespace ::testing;
  auto candidate = std::make_unique<candidate_type>(
      queue, 0x123456, std::unique_ptr<etcdserverpb::KV::Stub>(), std::unique_ptr<etcdserverpb::Watch::Stub>(),
      "basic-election", "abc1000");

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).WillRepeatedly(Invoke([]() {}));

  EXPECT_NO_THROW(candidate.reset(nullptr));
}

/// @test verify gh::detail::election_candidate_impl<> works in the happy day scenario.
TEST(election_candidate_impl, normal_lifecycle) {
  // gh::log::instance().add_sink(
  //    gh::make_log_sink([](gh::severity sev, std::string&& msg) { std::cout << msg << std::endl; }));
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;

  using namespace ::testing;
  // We are going to simulate an election where the candidate has to wait for another leader.  Several calls are
  // trivial for this purpose ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_create_rdwr_stream(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_writes_done(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_finish(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));

  // ... verify that the candidate makes a valid query to create the node, and give it a reasonable response ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/create_node";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::TxnRequest, etcdserverpb::TxnResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    ASSERT_EQ(op->request.compare_size(), 1);
    EXPECT_EQ(op->request.compare(0).key(), "mock-election/123456");
    ASSERT_EQ(op->request.success_size(), 1);
    ASSERT_TRUE(op->request.success(0).has_request_put());
    ASSERT_EQ(op->request.success(0).request_put().key(), "mock-election/123456");
    ASSERT_EQ(op->request.success(0).request_put().value(), "bcd2000");
    ASSERT_EQ(op->request.success(0).request_put().lease(), 0x123456);
    ASSERT_EQ(op->request.failure_size(), 1);
    ASSERT_TRUE(op->request.failure(0).has_request_range());
    EXPECT_EQ(op->request.failure(0).request_range().key(), "mock-election/123456");

    op->response.set_succeeded(true);
    op->response.mutable_header()->set_revision(2000);
    bop->callback(*bop, true);
  }));

  // ... verify that the candidate uses the previous information to create a valid query for its predecessor, and
  // return no precessor, we have another test for the more complex case where predecessors exists ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/query_predecessor";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::RangeRequest, etcdserverpb::RangeResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    EXPECT_EQ(op->request.key(), "mock-election/");
    EXPECT_EQ(op->request.max_create_revision(), 1999);

    op->response.mutable_header()->set_revision(3000);
    bop->callback(*bop, true);
  }));

  // ... no watcher should be set ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(Truly([](auto op) {
    return op->name == "election_candidate/on_range_request/watch";
  }))).Times(0);

  auto candidate = std::make_unique<candidate_type>(
      queue, 0x123456, std::unique_ptr<etcdserverpb::KV::Stub>(), std::unique_ptr<etcdserverpb::Watch::Stub>(),
      "mock-election", "bcd2000");

  auto fut = candidate->campaign();
  EXPECT_TRUE(candidate->elected());

  auto fc = fut.wait_for(0ms);
  EXPECT_EQ(fc, std::future_status::ready);

  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/publish_value";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::TxnRequest, etcdserverpb::TxnResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    op->response.set_succeeded(true);
    bop->callback(*bop, true);
  }));
  EXPECT_NO_THROW(candidate->proclaim("bcd1000"));

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).Times(1);

  EXPECT_NO_THROW(candidate->resign());

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).Times(1);
  EXPECT_NO_THROW(candidate.reset(nullptr));
}

/// @test verify gh::detail::election_candidate_impl<> works if the node already exists.
TEST(election_candidate_impl, existing_node_same_value) {
  // gh::log::instance().add_sink(
  //    gh::make_log_sink([](gh::severity sev, std::string&& msg) { std::cout << msg << std::endl; }));
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;

  using namespace ::testing;
  // We are going to simulate an election where the candidate has to wait for another leader.  Several calls are
  // trivial for this purpose ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_create_rdwr_stream(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_writes_done(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_finish(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));

  // ... verify that the candidate makes a valid query to create the node, and give it a reasonable response ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/create_node";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::TxnRequest, etcdserverpb::TxnResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);

    // ... the node already exists ...
    op->response.set_succeeded(false);
    op->response.mutable_header()->set_revision(2000);
    // ... but we are lucky, the value in the node is exactly the same we want to insert ...
    auto& range = *op->response.add_responses()->mutable_response_range();
    auto& kv = *range.add_kvs();
    kv.set_value("bcd2000");
    kv.set_key("mock-election/123456");
    kv.set_create_revision(1500);
    bop->callback(*bop, true);
  }));

  // ... verify that the candidate uses the previous information to create a valid query for its predecessor, and
  // return one predecessor to the query ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/query_predecessor";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::RangeRequest, etcdserverpb::RangeResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    EXPECT_EQ(op->request.key(), "mock-election/");
    EXPECT_EQ(op->request.max_create_revision(), 1499);

    op->response.mutable_header()->set_revision(3000);
    bop->callback(*bop, true);
  }));

  // ... since there is no precessor, there should be no watch set ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(Truly([](auto op) {
    return op->name == "election_candidate/on_range_request/watch";
  }))).Times(0);

  auto candidate = std::make_unique<candidate_type>(
      queue, 0x123456, std::unique_ptr<etcdserverpb::KV::Stub>(), std::unique_ptr<etcdserverpb::Watch::Stub>(),
      "mock-election", "bcd2000");

  auto fut = candidate->campaign();
  EXPECT_TRUE(candidate->elected());

  auto fc = fut.wait_for(0ms);
  EXPECT_EQ(fc, std::future_status::ready);

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).Times(1);
  EXPECT_NO_THROW(candidate->resign());

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).Times(1);
  EXPECT_NO_THROW(candidate.reset(nullptr));
}

/// @test verify gh::detail::election_candidate_impl<> works if the node already exists and has a mismatch value.
TEST(election_candidate_impl, existing_node_value_mismatch) {
  // gh::log::instance().add_sink(
  //    gh::make_log_sink([](gh::severity sev, std::string&& msg) { std::cout << msg << std::endl; }));
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;

  using namespace ::testing;
  // We are going to simulate an election where the candidate has to wait for another leader.  Several calls are
  // trivial for this purpose ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_create_rdwr_stream(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_writes_done(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_finish(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));

  // ... verify that the candidate makes a valid query to create the node, in this case we respond with a failed
  // creation and with a different existing value that we desire ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/create_node";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::TxnRequest, etcdserverpb::TxnResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);

    // ... the node already exists ...
    op->response.set_succeeded(false);
    op->response.mutable_header()->set_revision(2000);
    // ... but we are lucky, the value in the node is exactly the same we want to insert ...
    auto& range = *op->response.add_responses()->mutable_response_range();
    auto& kv = *range.add_kvs();
    kv.set_value("bcd1200");
    kv.set_key("mock-election/123456");
    kv.set_create_revision(1500);
    bop->callback(*bop, true);
  }));

  // ... because the value was mismatched we expect a call to publish_value() ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/publish_value";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::TxnRequest, etcdserverpb::TxnResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);

    ASSERT_EQ(op->request.success_size(), 1);
    ASSERT_TRUE(op->request.success(0).has_request_put());
    EXPECT_EQ(op->request.success(0).request_put().key(), "mock-election/123456");
    EXPECT_EQ(op->request.success(0).request_put().value(), "bcd2000");
    op->response.set_succeeded(true);
    bop->callback(*bop, true);
  }));

  // ... verify that the candidate uses the previous information to create a valid query for its predecessor, and
  // return one predecessor to the query ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/query_predecessor";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::RangeRequest, etcdserverpb::RangeResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    EXPECT_EQ(op->request.key(), "mock-election/");
    EXPECT_EQ(op->request.max_create_revision(), 1499);

    op->response.mutable_header()->set_revision(3000);
    bop->callback(*bop, true);
  }));

  // ... since there is no predecessor, no watch should be set ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(Truly([](auto op) {
    return op->name == "election_candidate/on_range_request/watch";
  }))).Times(0);

  auto candidate = std::make_unique<candidate_type>(
      queue, 0x123456, std::unique_ptr<etcdserverpb::KV::Stub>(), std::unique_ptr<etcdserverpb::Watch::Stub>(),
      "mock-election", "bcd2000");

  auto fut = candidate->campaign();
  EXPECT_TRUE(candidate->elected());
  auto fc = fut.wait_for(0ms);
  EXPECT_EQ(fc, std::future_status::ready);

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).Times(1);
  EXPECT_NO_THROW(candidate->resign());

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).Times(1);
  EXPECT_NO_THROW(candidate.reset(nullptr));
}

/// @test verify gh::detail::election_candidate_impl<> works if the node exists and it cannot be modified.
TEST(election_candidate_impl, existing_node_put_error) {
  // gh::log::instance().add_sink(
  //    gh::make_log_sink([](gh::severity sev, std::string&& msg) { std::cout << msg << std::endl; }));
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;

  using namespace ::testing;
  // We are going to simulate an election where the candidate has to wait for another leader.  Several calls are
  // trivial for this purpose ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_create_rdwr_stream(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_writes_done(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_finish(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));

  // ... verify that the candidate makes a valid query to create the node, in this case we respond with a failed
  // creation and with a different existing value that we desire ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/create_node";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::TxnRequest, etcdserverpb::TxnResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);

    // ... the node already exists ...
    op->response.set_succeeded(false);
    op->response.mutable_header()->set_revision(2000);
    // ... but we are lucky, the value in the node is exactly the same we want to insert ...
    auto& range = *op->response.add_responses()->mutable_response_range();
    auto& kv = *range.add_kvs();
    kv.set_value("bcd1200");
    kv.set_key("mock-election/123456");
    kv.set_create_revision(1500);
    bop->callback(*bop, true);
  }));

  // ... because the value was mismatched we expect a call to publish_value(), simulate an error ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/publish_value";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::TxnRequest, etcdserverpb::TxnResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);

    ASSERT_EQ(op->request.success_size(), 1);
    ASSERT_TRUE(op->request.success(0).has_request_put());
    EXPECT_EQ(op->request.success(0).request_put().key(), "mock-election/123456");
    EXPECT_EQ(op->request.success(0).request_put().value(), "bcd2000");
    // ... this indicates that either some external changes to the etcd server state took place, or maybe permissions
    // changed, or maybe a lease expired?  In any case, very rare errors, we just want to make sure they are reported
    // correctly ...
    op->response.set_succeeded(false);
    bop->callback(*bop, true);
  }));

  auto candidate = std::make_unique<candidate_type>(
    queue, 0x123456, std::unique_ptr<etcdserverpb::KV::Stub>(), std::unique_ptr<etcdserverpb::Watch::Stub>(),
    "mock-election", "bcd2000");

  ASSERT_THROW(candidate->campaign(), std::exception);
  EXPECT_FALSE(candidate->elected());

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).Times(1);
  EXPECT_NO_THROW(candidate.reset(nullptr));
}

/// @test verify election_candidate_impl<> can handle a compaction when setting up watchers.
TEST(election_candidate_impl, compaction_before_watch) {
  // gh::log::instance().add_sink(
  //   gh::make_log_sink([](gh::severity sev, std::string&& msg) { std::cout << msg << std::endl; }));
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;

  using namespace ::testing;
  // We are going to simulate an election where the candidate has to wait for another leader.  Several calls are
  // trivial for this purpose ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_create_rdwr_stream(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_writes_done(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_finish(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));

  // ... verify that the candidate makes a valid query to create the node, and give it a reasonable response ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/create_node";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::TxnRequest, etcdserverpb::TxnResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);

    op->response.set_succeeded(true);
    op->response.mutable_header()->set_revision(2000);
    bop->callback(*bop, true);
  }));

  // ... verify that the candidate uses the previous information to create a valid query for its predecessor, and
  // return one predecessor to the query ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/query_predecessor";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::RangeRequest, etcdserverpb::RangeResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    EXPECT_EQ(op->request.key(), "mock-election/");
    EXPECT_EQ(op->request.max_create_revision(), 1999);

    op->response.mutable_header()->set_revision(3000);
    auto& kv = *op->response.add_kvs();
    kv.set_create_revision(1000);
    kv.set_key("mock-election/123000");
    kv.set_value("abc1000");
    bop->callback(*bop, true);
  }));

  // ... verify the candidate creates a watcher on the predecessor reported in the range call ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(Truly([](auto op) {
    return op->name == "election_candidate/on_range_request/watch";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = candidate_type::watch_write_op;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    ASSERT_TRUE(op->request.has_create_request());
    auto const& create = op->request.create_request();
    EXPECT_EQ(create.key(), "mock-election/123000");
    EXPECT_EQ(create.start_revision(), 3000);
    EXPECT_EQ(create.prev_kv(), true);
    bop->callback(*bop, true);
  }));

  std::shared_ptr<candidate_type::watch_read_op> pending_read;
  auto handle_on_read = [r = std::ref(pending_read)](auto bop) {
    auto* op = dynamic_cast<candidate_type::watch_read_op*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    r.get() = std::shared_ptr<candidate_type::watch_read_op>(bop, op);
  };
  EXPECT_CALL(*queue.interceptor().shared_mock, async_read(Truly([](auto op) {
    return op->name == "election_candidate/on_watch_create/read";
  }))).WillRepeatedly(Invoke(handle_on_read));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_read(Truly([](auto op) {
    return op->name == "election_candidate/on_watch_read/read";
  }))).WillRepeatedly(Invoke(handle_on_read));

  auto candidate = std::make_unique<candidate_type>(
      queue, 0x123456, std::unique_ptr<etcdserverpb::KV::Stub>(), std::unique_ptr<etcdserverpb::Watch::Stub>(),
      "mock-election", "bcd2000");

  auto fut = candidate->campaign();
  EXPECT_FALSE(candidate->elected());

  auto fc = fut.wait_for(0ms);
  EXPECT_EQ(fc, std::future_status::timeout);

  // ... prepare for the compaction response in the watcher, that should trigger another query_predecessor(),
  // but with the right revision now ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/query_predecessor";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::RangeRequest, etcdserverpb::RangeResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    EXPECT_EQ(op->request.key(), "mock-election/");
    EXPECT_EQ(op->request.max_create_revision(), 1999);

    op->response.mutable_header()->set_revision(5000);
    auto& kv = *op->response.add_kvs();
    kv.set_create_revision(1000);
    kv.set_key("mock-election/123000");
    kv.set_value("abc1000");
    bop->callback(*bop, true);
  }));

  // ... verify the candidate creates a watcher on the predecessor reported in the range call ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(Truly([](auto op) {
    return op->name == "election_candidate/on_range_request/watch";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = candidate_type::watch_write_op;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    ASSERT_TRUE(op->request.has_create_request());
    auto const& create = op->request.create_request();
    EXPECT_EQ(create.key(), "mock-election/123000");
    EXPECT_EQ(create.start_revision(), 5000);
    EXPECT_EQ(create.prev_kv(), true);
    bop->callback(*bop, true);
  }));

  // ... for the first read to return a "compaction", indicating that the history between the desired revision and the
  // current state is lost ...
  ASSERT_TRUE((bool)pending_read);
  pending_read->response.set_compact_revision(5000);

  auto pr = std::move(pending_read);
  ASSERT_FALSE((bool)pending_read);
  pr->callback(*pr, true);

  EXPECT_FALSE(candidate->elected());
  fc = fut.wait_for(0ms);
  EXPECT_EQ(fc, std::future_status::timeout);

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).WillOnce(Invoke([&pr]() { pr->callback(*pr, false); }));
  EXPECT_NO_THROW(candidate->resign());

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).Times(1);
  EXPECT_NO_THROW(candidate.reset(nullptr));
}

/**
 * @test verify gh::detail::election_candidate_impl<> works when the predecessor is deleted.
 *
 * In this scenario we assume that there are existing nodes with names and creation revisions:
 *     (mock-election/100000, 1000), (mock-election/110000, 1100), (mock-election/120000, 1200)
 * we are going to create a new node (mock-election/130000, 1300), and we then delete /120000, /100000, and finally
 * /110000.  We want to observe that deleting /120000 triggers a new search for a predecessor and a watch, while
 * deleting /100000 does not.  And only after deleting /110000 does the new node become the leader.
 */
TEST(election_candidate_impl, another_predecessor) {
  // gh::log::instance().add_sink(
  //    gh::make_log_sink([](gh::severity sev, std::string&& msg) { std::cout << msg << std::endl; }));
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;

  using namespace ::testing;
  // We are going to simulate an election where the candidate has to wait for another leader.  Several calls are
  // trivial for this purpose ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_create_rdwr_stream(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_writes_done(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_finish(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));

  // ... verify that the candidate makes a valid query to create the node, and give it a reasonable response ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/create_node";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::TxnRequest, etcdserverpb::TxnResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);

    op->response.set_succeeded(true);
    op->response.mutable_header()->set_revision(1300);
    bop->callback(*bop, true);
  }));

  // ... verify that the candidate uses the previous information to create a valid query for its predecessor, and
  // return one predecessor to the query ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/query_predecessor";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::RangeRequest, etcdserverpb::RangeResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    EXPECT_EQ(op->request.key(), "mock-election/");
    EXPECT_EQ(op->request.max_create_revision(), 1299);

    op->response.mutable_header()->set_revision(1300);
    auto& kv = *op->response.add_kvs();
    kv.set_create_revision(1200);
    kv.set_key("mock-election/120000");
    kv.set_value("abc1200");
    bop->callback(*bop, true);
  }));

  // ... verify the candidate creates a watcher on the predecessor reported in the range call ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(Truly([](auto op) {
    return op->name == "election_candidate/on_range_request/watch";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = candidate_type::watch_write_op;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    ASSERT_TRUE(op->request.has_create_request());
    auto const& create = op->request.create_request();
    EXPECT_EQ(create.key(), "mock-election/120000");
    EXPECT_EQ(create.start_revision(), 1300);
    EXPECT_EQ(create.prev_kv(), true);
    bop->callback(*bop, true);
  }));

  std::shared_ptr<candidate_type::watch_read_op> pending_read;
  auto handle_on_read = [r = std::ref(pending_read)](auto bop) {
    auto* op = dynamic_cast<candidate_type::watch_read_op*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    r.get() = std::shared_ptr<candidate_type::watch_read_op>(bop, op);
  };
  EXPECT_CALL(*queue.interceptor().shared_mock, async_read(Truly([](auto op) {
    return op->name == "election_candidate/on_watch_create/read";
  }))).WillRepeatedly(Invoke(handle_on_read));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_read(Truly([](auto op) {
    return op->name == "election_candidate/on_watch_read/read";
  }))).WillRepeatedly(Invoke(handle_on_read));

  // ... finally create the /130000 node ...
  auto candidate = std::make_unique<candidate_type>(
    queue, 0x130000, std::unique_ptr<etcdserverpb::KV::Stub>(), std::unique_ptr<etcdserverpb::Watch::Stub>(),
    "mock-election", "abc1300");

  auto fut = candidate->campaign();
  EXPECT_FALSE(candidate->elected());

  auto fc = fut.wait_for(0ms);
  EXPECT_EQ(fc, std::future_status::timeout);

  // ... prepare for handling of a DELETE message about the /120000, the library should check which node is the
  // new predecessor, prepare /110000 as the response ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/query_predecessor";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::RangeRequest, etcdserverpb::RangeResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    EXPECT_EQ(op->request.key(), "mock-election/");
    EXPECT_EQ(op->request.max_create_revision(), 1299);

    op->response.mutable_header()->set_revision(1400);
    auto& kv = *op->response.add_kvs();
    kv.set_create_revision(1100);
    kv.set_key("mock-election/110000");
    kv.set_value("abc1100");
    bop->callback(*bop, true);
  }));

  // ... verify the candidate creates a watcher on the /110000 predecessor reported in the range call ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(Truly([](auto op) {
    return op->name == "election_candidate/on_range_request/watch";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = candidate_type::watch_write_op;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    ASSERT_TRUE(op->request.has_create_request());
    auto const& create = op->request.create_request();
    EXPECT_EQ(create.key(), "mock-election/110000");
    EXPECT_EQ(create.start_revision(), 1400);
    EXPECT_EQ(create.prev_kv(), true);
    bop->callback(*bop, true);
  }));

  // ... delete the /120000 node, that should trigger a search ...
  ASSERT_TRUE((bool)pending_read);
  pending_read->response.set_watch_id(3000);
  {
    auto& ev = *pending_read->response.add_events();
    ev.set_type(mvccpb::Event::DELETE);
    ev.mutable_prev_kv()->set_create_revision(1000);
    ev.mutable_prev_kv()->set_key("mock-election/120000");
    ev.mutable_prev_kv()->set_value("abc1200");
  }
  auto pr = std::move(pending_read);
  ASSERT_FALSE((bool)pending_read);
  pr->callback(*pr, true);

  // ... we still should not be elected ...
  EXPECT_FALSE(candidate->elected());
  fc = fut.wait_for(0ms);
  EXPECT_EQ(fc, std::future_status::timeout);

  // ... prepare to simulate of /100000 and /110000, they should result in yet another search, but no watcher ...
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_candidate/query_predecessor";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::RangeRequest, etcdserverpb::RangeResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    EXPECT_EQ(op->request.key(), "mock-election/");
    EXPECT_EQ(op->request.max_create_revision(), 1299);

    op->response.mutable_header()->set_revision(1500);
    bop->callback(*bop, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(Truly([](auto op) {
    return op->name == "election_candidate/on_range_request/watch";
  }))).Times(0);
  ASSERT_TRUE((bool)pending_read);
  pending_read->response.set_watch_id(3000);
  {
    auto& ev = *pending_read->response.add_events();
    ev.set_type(mvccpb::Event::DELETE);
    ev.mutable_prev_kv()->set_create_revision(1100);
    ev.mutable_prev_kv()->set_key("mock-election/110000");
    ev.mutable_prev_kv()->set_value("abc1100");
  }
  // ... execute the delete action ...
  pr = std::move(pending_read);
  ASSERT_FALSE((bool)pending_read);
  pr->callback(*pr, true);

  EXPECT_TRUE(candidate->elected());
  fc = fut.wait_for(0ms);
  EXPECT_EQ(fc, std::future_status::ready);

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).Times(1);
  EXPECT_NO_THROW(candidate->resign());

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).Times(1);
  EXPECT_NO_THROW(candidate.reset(nullptr));
}
