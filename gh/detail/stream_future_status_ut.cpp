#include <gh/detail/stream_future_status.hpp>

#include <gtest/gtest.h>

/// @test Verify the streaming operator for std::future_status works as expected.
TEST(stream_future_status, basic) {
  std::ostringstream os;
  os << std::future_status::deferred << " " << std::future_status::ready << " " << std::future_status::timeout;
  EXPECT_EQ(os.str(), "[deferred] [ready] [timeout]");
}
