#include "gh/watch_election.hpp"
#include <gh/detail/session_impl.hpp>
#include <gh/detail/stream_future_status.hpp>
#include <gh/leader_election.hpp>

#include <gtest/gtest.h>

namespace {
using election_session_type = gh::detail::session_impl<gh::completion_queue<>>;
}

/**
 * @test Verify that one can create, and delete a election participant.
 */
TEST(watch_election, basic) {
  std::string const etcd_address = "localhost:22379";
  auto etcd_channel = grpc::CreateChannel(etcd_address, grpc::InsecureChannelCredentials());
  auto queue = std::make_shared<gh::active_completion_queue>();

  // ... create a unique election name ...
  using namespace std::chrono_literals;
  election_session_type election_session(queue->cq(), etcdserverpb::Lease::NewStub(etcd_channel), 3000ms);
  EXPECT_NE(election_session.lease_id(), 0U);

  std::ostringstream os;
  os << "test-election/" << std::hex << election_session.lease_id();
  auto election_name = os.str();
  EXPECT_TRUE(true) << "testing with election-name=" << election_name;

  // Create a watcher to observe the election ...
  gh::watch_election watcher(queue, etcd_channel, election_name);
  EXPECT_NO_THROW(watcher.startup());
  EXPECT_FALSE(watcher.has_leader());

  gh::leader_election candidate(queue, etcd_channel, election_name, "42", 3000ms);
  EXPECT_TRUE(true) << "participant object constructed";
  EXPECT_NO_THROW(candidate.campaign().wait());
  EXPECT_TRUE(candidate.elected());
  EXPECT_EQ(candidate.value(), "42");

  auto sleep_until = [](auto predicate) {
    auto s = 10ms;
    for (int i = 0; i != 10; ++i) {
      std::this_thread::sleep_for(s);
      if (predicate()) {
        break;
      }
      s *= 2;
    }
  };

  sleep_until([&watcher]() { return watcher.has_leader(); });
  EXPECT_TRUE(watcher.has_leader());
  EXPECT_EQ(watcher.current_key(), candidate.key());
  EXPECT_EQ(watcher.current_value(), candidate.value());

  std::string key;
  std::string value;
  auto token = watcher.subscribe([&key, &value](std::string const& k, std::string const& v) {
    key = k;
    value = v;
  });
  candidate.proclaim("abc123");

  sleep_until([&key]() { return not key.empty(); });
  EXPECT_EQ(key, candidate.key());
  EXPECT_EQ(value, "abc123");
  EXPECT_NO_THROW(watcher.unsubscribe(token));

  candidate.proclaim("bcd234");
  sleep_until([&watcher]() { return watcher.current_value() == "bcd234"; });
  EXPECT_EQ(watcher.current_value(), "bcd234");
  EXPECT_EQ(value, "abc123");

  EXPECT_TRUE(true) << "revoking auxiliary session leases";
  election_session.revoke();
}
