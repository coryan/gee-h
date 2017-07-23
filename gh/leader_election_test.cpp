#include "gh/leader_election.hpp"
#include <gh/detail/session_impl.hpp>
#include <gh/detail/stream_future_status.hpp>

#include <gtest/gtest.h>

namespace {
using election_session_type = gh::detail::session_impl<gh::completion_queue<>>;
}

/**
 * @test Verify that one can create, and delete a election participant.
 */
TEST(leader_election, basic) {
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

  {
    gh::leader_election tested(queue, etcd_channel, election_name, "42", 3000ms);
    EXPECT_TRUE(true) << "participant object constructed";

    EXPECT_FALSE(tested.elected());
    EXPECT_NO_THROW(tested.campaign().wait());
    EXPECT_TRUE(tested.elected());
    EXPECT_EQ(tested.value(), "42");
    EXPECT_EQ(tested.key().substr(0, election_name.size()), election_name);
    EXPECT_GT(tested.creation_revision(), 0);
    EXPECT_GT(tested.lease_id(), 0U);
  }
  EXPECT_TRUE(true) << "destructed participant, revoking session leases";
  election_session.revoke();
}

/**
 * @test Verify that an election participant can become the leader.
 */
TEST(leader_election, switch_leader) {
  std::string const etcd_address = "localhost:22379";
  auto etcd_channel = grpc::CreateChannel(etcd_address, grpc::InsecureChannelCredentials());
  auto queue = std::make_shared<gh::active_completion_queue>();
  EXPECT_TRUE(true) << "queue created and thread requested";

  // ... create a unique election name ...
  using namespace std::chrono_literals;
  election_session_type election_session(queue->cq(), etcdserverpb::Lease::NewStub(etcd_channel), 3000ms);
  EXPECT_NE(election_session.lease_id(), 0U);

  std::ostringstream os;
  os << "test-election/" << std::hex << election_session.lease_id();
  auto election_name = os.str();
  EXPECT_TRUE(true) << "testing with election-name=" << election_name;

  gh::leader_election participant_a(queue, etcd_channel, election_name, "session_a", 3000ms);
  EXPECT_NO_THROW(participant_a.campaign().wait());
  EXPECT_EQ(participant_a.value(), "session_a");

  gh::leader_election participant_b(queue, etcd_channel, election_name, "session_b", 3000ms);
  EXPECT_EQ(participant_b.value(), "session_b");

  gh::leader_election participant_c(queue, etcd_channel, election_name, "session_c", 3s);
  EXPECT_EQ(participant_c.value(), "session_c");

  std::this_thread::sleep_for(5000ms);

  auto future_c = participant_c.campaign();
  EXPECT_EQ(std::future_status::timeout, future_c.wait_for(0ms));
  auto future_b = participant_b.campaign();
  EXPECT_EQ(std::future_status::timeout, future_b.wait_for(0ms));

  for (int i = 0; i != 2; ++i) {
    EXPECT_TRUE(true) << "iteration i=" << i;
    EXPECT_NO_THROW(participant_a.proclaim("I am the best"));
    EXPECT_NO_THROW(participant_b.proclaim("No you are not"));
    EXPECT_NO_THROW(participant_c.proclaim("Both wrong"));
  }

  EXPECT_TRUE(true) << "a::resign";
  EXPECT_NO_THROW(participant_a.resign());

  EXPECT_THROW(participant_a.proclaim("not dead yet"), std::exception);
  try {
    participant_a.proclaim("no really");
  } catch (std::exception const& ex) {
    EXPECT_TRUE(true) << "exception value: " << ex.what();
  }

  EXPECT_TRUE(true) << "b::resign";
  participant_b.resign();

  EXPECT_EQ(future_c.get(), true);

  EXPECT_TRUE(true) << "c::resign";
  participant_c.resign();

  EXPECT_TRUE(true) << "cleanup";
  election_session.revoke();
}

/**
 * @test Verify that an election participant handles aborted elections.
 */
TEST(leader_election, abort) {
  std::string const etcd_address = "localhost:2379";
  auto etcd_channel = grpc::CreateChannel(etcd_address, grpc::InsecureChannelCredentials());
  auto queue = std::make_shared<gh::active_completion_queue>();
  EXPECT_TRUE(true) << "queue created and thread requested";

  // ... create a unique election name ...
  using namespace std::chrono_literals;
  election_session_type election_session(queue->cq(), etcdserverpb::Lease::NewStub(etcd_channel), 3000ms);
  EXPECT_NE(election_session.lease_id(), 0U);

  std::ostringstream os;
  os << "test-election/" << std::hex << election_session.lease_id();
  auto election_name = os.str();
  EXPECT_TRUE(true) << "testing with election-name=" << election_name;

  gh::leader_election participant_a(queue, etcd_channel, election_name, "session_a", 3000ms);
  EXPECT_EQ(participant_a.value(), "session_a");

  gh::leader_election participant_b(queue, etcd_channel, election_name, "session_b", 3s);
  EXPECT_EQ(participant_b.value(), "session_b");

  std::this_thread::sleep_for(500ms);

  auto future_b = participant_b.campaign();
  EXPECT_EQ(std::future_status::timeout, future_b.wait_for(0ms));

  EXPECT_TRUE(true) << "b::resign";
  participant_b.resign();

  // ... if future_b is not ready the next call will block, so we make
  // a hard requirement ...
  ASSERT_EQ(std::future_status::ready, future_b.wait_for(0ms));
  EXPECT_EQ(future_b.get(), false);

  EXPECT_TRUE(true) << "a::resign";
  participant_a.resign();

  EXPECT_TRUE(true) << "cleanup";
  election_session.revoke();
}
