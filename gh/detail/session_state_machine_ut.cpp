#include "gh/detail/session_state_machine.hpp"

#include <gtest/gtest.h>

/**
 * @test Verify that the iostream operator for gh::session_state works as expected.
 */
TEST(session_state_machine, basic) {
  using s = gh::detail::session_state;
  gh::detail::session_state_machine machine;

  ASSERT_EQ(machine.current(), s::constructing);
  ASSERT_TRUE(machine.change_state("test-1", s::connecting));
  ASSERT_FALSE(machine.change_state("test-2", s::constructing));

  bool called = false;
  auto check_called = [&called]() { called = true; };
  ASSERT_TRUE(machine.change_state_action("test-3", s::connected, check_called));
  ASSERT_TRUE(called);
  called = false;
  ASSERT_FALSE(machine.change_state_action("test-4", s::connecting, check_called));
  ASSERT_FALSE(called);

  // ... then test easy state transitions ...
  ASSERT_TRUE(machine.change_state("test", s::obtaining_lease));
  ASSERT_TRUE(machine.change_state("test", s::lease_obtained));
  ASSERT_TRUE(machine.change_state("test", s::waiting_for_timer));
  ASSERT_TRUE(machine.change_state("test", s::waiting_for_keep_alive_write));
  ASSERT_TRUE(machine.change_state("test", s::waiting_for_keep_alive_read));
  ASSERT_TRUE(machine.change_state("test", s::waiting_for_timer));
  ASSERT_TRUE(machine.change_state("test", s::revoking));
  ASSERT_TRUE(machine.change_state("test", s::revoked));
  ASSERT_TRUE(machine.change_state("test", s::shutting_down));
  ASSERT_FALSE(machine.change_state("test", s::waiting_for_timer));
  ASSERT_FALSE(machine.change_state("test", s::waiting_for_keep_alive_read));
  ASSERT_FALSE(machine.change_state("test", s::revoking));
  ASSERT_FALSE(machine.change_state("test", s::revoked));
  ASSERT_TRUE(machine.change_state("test", s::shutdown));
}

/**
 * @test Verify that the iostream operator for gh::session_state works as expected.
 */
TEST(session_state, streaming) {
  using s = gh::detail::session_state;
  std::ostringstream os;
  os << " " << s::constructing << " " << s::connecting << " " << s::connected << " " << s::obtaining_lease << " "
     << s::lease_obtained << " " << s::waiting_for_timer << " " << s::waiting_for_keep_alive_write << " "
     << s::waiting_for_keep_alive_read << " " << s::revoking << " " << s::revoked << " " << s::shutting_down << " "
     << s::shutdown;
  ASSERT_EQ(
      os.str(), " constructing connecting connected obtaining_lease lease_obtained waiting_for_timer "
                "waiting_for_keep_alive_write waiting_for_keep_alive_read revoking revoked shutting_down shutdown");
}
