#include "gh/session_state.hpp"

#include <gtest/gtest.h>
#include <sstream>

/**
 * @test Verify that the iostream operator for gh::session_state works as expected.
 */
TEST(session_state, streaming) {
  using s = gh::session_state;
  std::ostringstream os;
  os << s::constructing << " " << s::connected << " " << s::connecting;
  ASSERT_EQ(os.str(), "constructing connected connecting");
}
