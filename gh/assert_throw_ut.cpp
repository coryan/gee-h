#include "gh/assert_throw.hpp"

#include <gtest/gtest.h>

/**
 * @test Verify that GH_ASSERT_THROW() works as expected.
 */
TEST(assert_throw, basic) {
  ASSERT_THROW(gh::assert_throw_impl("foo", "bar()", "bar.cc", 20), std::exception);

  ASSERT_THROW(GH_ASSERT_THROW(false), std::exception);
  ASSERT_NO_THROW(GH_ASSERT_THROW(true));
}
