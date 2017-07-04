#include "gh/detail/null_stream.hpp"

#include <gtest/gtest.h>
#include <string>

TEST(null_stream, base) {
  gh::detail::null_stream n;

  EXPECT_NO_THROW(n << "foo");
  EXPECT_NO_THROW(n << "bar" << 42 << std::string("blah blah"));
}
