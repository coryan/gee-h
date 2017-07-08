#include "gh/detail/candidate_state_machine.hpp"

#include <gtest/gtest.h>

/**
 * @test Verify that the iostream operator for gh::candidate_state works as expected.
 */
TEST(candidate_state_machine, basic) {
  using s = gh::detail::candidate_state;
  gh::detail::candidate_state_machine machine;

  ASSERT_EQ(machine.current(), s::constructing);
  EXPECT_TRUE(machine.change_state("test-1", s::connecting));
  EXPECT_FALSE(machine.change_state("test-2", s::constructing));

  bool called = false;
  auto check_called = [&called]() { called = true; };
  ASSERT_TRUE(machine.change_state_action("test-3", s::connected, check_called));
  ASSERT_TRUE(called);
  called = false;
  ASSERT_FALSE(machine.change_state_action("test-4", s::connecting, check_called));
  ASSERT_FALSE(called);

  // ... then test easy state transitions ...
  ASSERT_TRUE(machine.change_state("test", s::creating_node));
  ASSERT_TRUE(machine.change_state("test", s::updating_node));
  ASSERT_TRUE(machine.change_state("test", s::node_created));
  ASSERT_TRUE(machine.change_state("test", s::querying_predecessor));
  ASSERT_TRUE(machine.change_state("test", s::predecessor_fetched));
  ASSERT_TRUE(machine.change_state("test", s::creating_watcher));
  ASSERT_TRUE(machine.change_state("test", s::watcher_created));
  ASSERT_TRUE(machine.change_state("test", s::waiting_for_watcher_read));
  ASSERT_TRUE(machine.change_state("test", s::resigning));
  ASSERT_FALSE(machine.change_state("test", s::waiting_for_watcher_read));
  ASSERT_FALSE(machine.change_state("test", s::watcher_created));
  ASSERT_FALSE(machine.change_state("test", s::resigning));
  ASSERT_TRUE(machine.change_state("test", s::resigned));
  ASSERT_TRUE(machine.change_state("test", s::shutting_down));
  ASSERT_TRUE(machine.change_state("test", s::shutdown));
  ASSERT_FALSE(machine.change_state("test", s::shutting_down));
}

/**
 * @test Verify that the iostream operator for gh::candidate_state works as expected.
 */
TEST(candidate_state, streaming) {
  using s = gh::detail::candidate_state;
  std::ostringstream os;
  os << " " << s::constructing << " " << s::connecting << " " << s::connected << " " << s::creating_node << " "
     << s::updating_node << " " << s::node_created << " " << s::querying_predecessor << " " << s::predecessor_fetched
     << " " << s::creating_watcher << " " << s::watcher_created << " " << s::waiting_for_watcher_read << " "
     << s::resigning << " " << s::resigned << " " << s::shutting_down << " " << s::shutdown;
  EXPECT_EQ(
      os.str(),
      " constructing connecting connected creating_node updating_node node_created querying_predecessor"
      " predecessor_fetched creating_watcher watcher_created waiting_for_watcher_read resigning"
      " resigned shutting_down shutdown");
}
