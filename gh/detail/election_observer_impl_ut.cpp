#define GH_MIN_SEVERITY trace
#include "gh/detail/election_observer_impl.hpp"
#include <gh/completion_queue.hpp>
#include <gh/detail/mocked_grpc_interceptor.hpp>

#include <gmock/gmock.h>

/// Define helper types and functions used in these tests
namespace {
using completion_queue_type = gh::completion_queue<gh::detail::mocked_grpc_interceptor>;
using observer_type = gh::detail::election_observer_impl<completion_queue_type>;
} // anonymous namespace

TEST(election_observer_impl, basic) {
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;
  using namespace ::testing;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_create_rdwr_stream(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(_)).WillRepeatedly(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  std::shared_ptr<observer_type::watch_read_op> pending_read;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_read(_))
      .WillRepeatedly(Invoke([r = std::ref(pending_read)](auto bop) {
        auto* op = dynamic_cast<observer_type::watch_read_op*>(bop.get());
        ASSERT_TRUE(op != nullptr);
        r.get() = std::shared_ptr<observer_type::watch_read_op>(bop, op);
      }));

  auto observer = std::make_unique<observer_type>(
      "mock-election", queue, std::unique_ptr<etcdserverpb::KV::Stub>(), std::unique_ptr<etcdserverpb::Watch::Stub>());
  observer->startup();

  auto pr = std::move(pending_read);
  ASSERT_FALSE((bool)pending_read);
  ASSERT_TRUE((bool)pr);
  pr->callback(*pr, true);

  EXPECT_FALSE(observer->has_leader());
  EXPECT_THROW(observer->current_key(), std::exception);
  EXPECT_THROW(observer->current_value(), std::exception);

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).WillOnce(Invoke([r = std::ref(pending_read)]() {
    auto pr = std::move(r.get());
    ASSERT_FALSE((bool)r.get());
    ASSERT_TRUE((bool)pr);
    pr->callback(*pr, false);
  }));

  EXPECT_NO_THROW(observer.reset(nullptr));
}

TEST(election_observer_impl, normal_lifecycle) {
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;
  using namespace ::testing;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_create_rdwr_stream(_)).WillOnce(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_observer/discover_node_with_lowest_creation_revision";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::RangeRequest, etcdserverpb::RangeResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    EXPECT_EQ(op->request.key(), "mock-election/");

    auto& kv = *op->response.add_kvs();
    kv.set_create_revision(1000);
    kv.set_key("mock-election/2000");
    kv.set_value("abc2000");
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(Truly([](auto op) {
    return op->name == "election_observer/create_watcher";
  }))).WillOnce(Invoke([](auto op) { op->callback(*op, true); }));

  std::shared_ptr<observer_type::watch_read_op> pending_read;
  auto handle_on_read = [r = std::ref(pending_read)](auto bop) {
    auto* op = dynamic_cast<observer_type::watch_read_op*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    r.get() = std::shared_ptr<observer_type::watch_read_op>(bop, op);
  };
  EXPECT_CALL(*queue.interceptor().shared_mock, async_read(Truly([](auto op) {
    return op->name == "election_observer/on_watch_create/watch_read";
  }))).WillOnce(Invoke(handle_on_read));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_read(Truly([](auto op) {
    return op->name == "election_observer/on_watch_read/watch_read";
  }))).WillRepeatedly(Invoke(handle_on_read));

  auto observer = std::make_unique<observer_type>(
      "mock-election", queue, std::unique_ptr<etcdserverpb::KV::Stub>(), std::unique_ptr<etcdserverpb::Watch::Stub>());

  // ... create at least one subscription ...
  std::vector<std::pair<std::string, std::string>> observations;
  auto subscriber = [&observations](std::string const& key, std::string const& value) {
    observations.emplace_back(key, value);
  };
  auto token = observer->subscribe(std::move(subscriber));

  // ... connect to the (mocked) etcd server and start receiving events ...
  observer->startup();

  EXPECT_TRUE(observer->has_leader());
  EXPECT_EQ(observer->election_name(), std::string("mock-election"));
  EXPECT_EQ(observer->current_key(), std::string("mock-election/2000"));
  EXPECT_EQ(observer->current_value(), std::string("abc2000"));

  ASSERT_GE(observations.size(), 1UL);
  auto last_observation = observations.back();
  ASSERT_EQ(last_observation.first, observer->current_key());
  ASSERT_EQ(last_observation.second, observer->current_value());
  observations.clear(); // ... makes next tests easier ...

  ASSERT_TRUE((bool)pending_read);
  pending_read->response.set_watch_id(3000);
  {
    auto& ev = *pending_read->response.add_events();
    ev.set_type(mvccpb::Event::PUT);
    ev.mutable_kv()->set_create_revision(1000);
    ev.mutable_kv()->set_key("mock-election/2000");
    ev.mutable_kv()->set_value("abc2000");
  }
  {
    auto& ev = *pending_read->response.add_events();
    ev.set_type(mvccpb::Event::PUT);
    ev.mutable_kv()->set_create_revision(1100);
    ev.mutable_kv()->set_key("mock-election/2200");
    ev.mutable_kv()->set_value("abc2200");
  }

  auto pr = std::move(pending_read);
  ASSERT_FALSE((bool)pending_read);
  pr->callback(*pr, true);

  // ... because the events do not provide any new information, there should be no callback ...
  ASSERT_TRUE(observations.empty());

  ASSERT_TRUE((bool)pending_read);
  pending_read->response.set_watch_id(3000);
  {
    auto& ev = *pending_read->response.add_events();
    ev.set_type(mvccpb::Event::DELETE);
    ev.mutable_prev_kv()->set_create_revision(1000);
    ev.mutable_prev_kv()->set_key("mock-election/2000");
    ev.mutable_prev_kv()->set_value("abc2000");
  }
  {
    auto& ev = *pending_read->response.add_events();
    ev.set_type(mvccpb::Event::PUT);
    ev.mutable_kv()->set_create_revision(1100);
    ev.mutable_kv()->set_key("mock-election/2200");
    ev.mutable_kv()->set_value("bcd2200");
  }

  pr = std::move(pending_read);
  pr->callback(*pr, true);

  ASSERT_TRUE(observer->has_leader());
  EXPECT_EQ(observer->current_key(), std::string("mock-election/2200"));
  EXPECT_EQ(observer->current_value(), std::string("bcd2200"));

  ASSERT_GE(observations.size(), 1UL);
  last_observation = observations.back();
  EXPECT_EQ(last_observation.first, observer->current_key());
  EXPECT_EQ(last_observation.second, observer->current_value());
  observations.clear(); // ... makes next tests easier ...

  // ... unsubscribe and push a new event, nothing should be received by the subscriber ...
  EXPECT_NO_THROW(observer->unsubscribe(token));

  ASSERT_TRUE((bool)pending_read);
  pending_read->response.set_watch_id(3000);
  {
    auto& ev = *pending_read->response.add_events();
    ev.set_type(mvccpb::Event::PUT);
    ev.mutable_kv()->set_create_revision(1100);
    ev.mutable_kv()->set_key("mock-election/2200");
    ev.mutable_kv()->set_value("cde2200");
  }
  pr = std::move(pending_read);
  pr->callback(*pr, true);

  ASSERT_TRUE(observer->has_leader());
  EXPECT_EQ(observer->current_key(), std::string("mock-election/2200"));
  EXPECT_EQ(observer->current_value(), std::string("cde2200"));

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).WillOnce(Invoke([r = std::ref(pending_read)]() {
    auto pr = std::move(r.get());
    ASSERT_FALSE((bool)r.get());
    ASSERT_TRUE((bool)pr);
    pr->callback(*pr, false);
  }));

  EXPECT_NO_THROW(observer.reset(nullptr));
}

TEST(election_observer_impl, late_subscription) {
  // gh::log::instance().add_sink(
  //    gh::make_log_sink([](gh::severity sev, std::string&& msg) { std::cout << msg << std::endl; }));
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;
  using namespace ::testing;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_create_rdwr_stream(_)).WillOnce(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_observer/discover_node_with_lowest_creation_revision";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::RangeRequest, etcdserverpb::RangeResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    EXPECT_EQ(op->request.key(), "late-subscription/");

    auto& kv = *op->response.add_kvs();
    kv.set_create_revision(1000);
    kv.set_key("late-subscription/2000");
    kv.set_value("abc2000");
    op->callback(*op, true);
  }));

  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(Truly([](auto op) {
    return op->name == "election_observer/create_watcher";
  }))).WillOnce(Invoke([](auto op) { op->callback(*op, true); }));

  std::shared_ptr<observer_type::watch_read_op> pending_read;
  auto handle_on_read = [r = std::ref(pending_read)](auto bop) {
    auto* op = dynamic_cast<observer_type::watch_read_op*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    r.get() = std::shared_ptr<observer_type::watch_read_op>(bop, op);
  };
  EXPECT_CALL(*queue.interceptor().shared_mock, async_read(Truly([](auto op) {
    return op->name == "election_observer/on_watch_create/watch_read";
  }))).WillOnce(Invoke(handle_on_read));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_read(Truly([](auto op) {
    return op->name == "election_observer/on_watch_read/watch_read";
  }))).WillRepeatedly(Invoke(handle_on_read));

  auto observer = std::make_unique<observer_type>(
      "late-subscription", queue, std::unique_ptr<etcdserverpb::KV::Stub>(),
      std::unique_ptr<etcdserverpb::Watch::Stub>());

  // ... connect to the (mocked) etcd server and start receiving events ...
  observer->startup();

  EXPECT_TRUE(observer->has_leader());
  EXPECT_EQ(observer->election_name(), std::string("late-subscription"));
  EXPECT_EQ(observer->current_key(), std::string("late-subscription/2000"));
  EXPECT_EQ(observer->current_value(), std::string("abc2000"));

  // ... create at least one subscription ...
  std::vector<std::pair<std::string, std::string>> observations;
  auto subscriber = [&observations](std::string const& key, std::string const& value) {
    observations.emplace_back(key, value);
  };
  auto token = observer->subscribe(std::move(subscriber));

  ASSERT_GE(observations.size(), 1UL);
  auto last_observation = observations.back();
  ASSERT_EQ(last_observation.first, observer->current_key());
  ASSERT_EQ(last_observation.second, observer->current_value());
  observations.clear(); // ... makes next tests easier ...

  EXPECT_NO_THROW(observer->unsubscribe(token));

  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(Truly([](auto op) {
    return op->name == "election_observer/cancel_watcher";
  }))).WillOnce(Invoke([](auto op) { op->callback(*op, true); }));
  EXPECT_NO_THROW(observer->shutdown());

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).WillOnce(Invoke([r = std::ref(pending_read)]() {
    auto pr = std::move(r.get());
    ASSERT_FALSE((bool)r.get());
    ASSERT_TRUE((bool)pr);
    pr->callback(*pr, false);
  }));
  EXPECT_NO_THROW(observer.reset(nullptr));
}

TEST(election_observer_impl, full_shutdown) {
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;
  using namespace ::testing;
  EXPECT_CALL(*queue.interceptor().shared_mock, async_create_rdwr_stream(_)).WillOnce(Invoke([](auto op) {
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_rpc(Truly([](auto op) {
    return op->name == "election_observer/discover_node_with_lowest_creation_revision";
  }))).WillOnce(Invoke([](auto bop) {
    using op_type = async_rpc_op<etcdserverpb::RangeRequest, etcdserverpb::RangeResponse>;
    auto* op = dynamic_cast<op_type*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    EXPECT_EQ(op->request.key(), "mock-election/");

    auto& kv = *op->response.add_kvs();
    kv.set_create_revision(1000);
    kv.set_key("mock-election/2000");
    kv.set_value("abc2000");
    op->callback(*op, true);
  }));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(Truly([](auto op) {
    return op->name == "election_observer/create_watcher";
  }))).WillOnce(Invoke([](auto op) { op->callback(*op, true); }));

  std::shared_ptr<observer_type::watch_read_op> pending_read;
  auto handle_on_read = [r = std::ref(pending_read)](auto bop) {
    auto* op = dynamic_cast<observer_type::watch_read_op*>(bop.get());
    ASSERT_TRUE(op != nullptr);
    r.get() = std::shared_ptr<observer_type::watch_read_op>(bop, op);
  };
  EXPECT_CALL(*queue.interceptor().shared_mock, async_read(Truly([](auto op) {
    return op->name == "election_observer/on_watch_create/watch_read";
  }))).WillOnce(Invoke(handle_on_read));
  EXPECT_CALL(*queue.interceptor().shared_mock, async_read(Truly([](auto op) {
    return op->name == "election_observer/on_watch_read/watch_read";
  }))).WillRepeatedly(Invoke(handle_on_read));

  auto observer = std::make_unique<observer_type>(
      "mock-election", queue, std::unique_ptr<etcdserverpb::KV::Stub>(), std::unique_ptr<etcdserverpb::Watch::Stub>());

  // ... create at least one subscription ...
  std::vector<std::pair<std::string, std::string>> observations;
  auto subscriber = [&observations](std::string const& key, std::string const& value) {
    observations.emplace_back(key, value);
  };
  auto token = observer->subscribe(std::move(subscriber));

  // ... connect to the (mocked) etcd server and start receiving events ...
  observer->startup();

  EXPECT_TRUE(observer->has_leader());
  EXPECT_EQ(observer->election_name(), std::string("mock-election"));
  EXPECT_EQ(observer->current_key(), std::string("mock-election/2000"));
  EXPECT_EQ(observer->current_value(), std::string("abc2000"));

  ASSERT_GE(observations.size(), 1UL);
  auto last_observation = observations.back();
  ASSERT_EQ(last_observation.first, observer->current_key());
  ASSERT_EQ(last_observation.second, observer->current_value());
  observations.clear(); // ... makes next tests easier ...

  EXPECT_NO_THROW(observer->unsubscribe(token));

  EXPECT_CALL(*queue.interceptor().shared_mock, async_write(Truly([](auto op) {
    return op->name == "election_observer/cancel_watcher";
  }))).WillOnce(Invoke([](auto op) { op->callback(*op, true); }));
  EXPECT_NO_THROW(observer->shutdown());

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).WillOnce(Invoke([r = std::ref(pending_read)]() {
    auto pr = std::move(r.get());
    ASSERT_FALSE((bool)r.get());
    ASSERT_TRUE((bool)pr);
    pr->callback(*pr, false);
  }));
  EXPECT_NO_THROW(observer.reset(nullptr));
}

TEST(election_observer_impl, early_shutdown) {
  using namespace std::chrono_literals;
  using namespace gh::detail;

  completion_queue_type queue;
  using namespace ::testing;

  auto observer = std::make_unique<observer_type>(
      "mock-election", queue, std::unique_ptr<etcdserverpb::KV::Stub>(), std::unique_ptr<etcdserverpb::Watch::Stub>());
  EXPECT_THROW(observer->shutdown(), std::exception);
  EXPECT_FALSE(observer->has_leader());

  EXPECT_CALL(*queue.interceptor().shared_mock, try_cancel()).Times(1);
  
  EXPECT_NO_THROW(observer.reset(nullptr));
}
