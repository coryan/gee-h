#include <gh/detail/exponential_backoff.hpp>

#include <gtest/gtest.h>

TEST(exponential_backoff, basic) {
  using namespace std::chrono_literals;
  EXPECT_THROW(gh::detail::exponential_backoff(1s, 500ms, 5), std::exception);
  EXPECT_THROW(gh::detail::exponential_backoff(1s, 1500ms, -1), std::exception);
  EXPECT_THROW(gh::detail::exponential_backoff(1s, 1500ms, 0), std::exception);

  EXPECT_NO_THROW(gh::detail::exponential_backoff(1s, 1500ms, 1));

  gh::detail::exponential_backoff backoff(10ms, 50ms, 5);
  EXPECT_EQ(backoff.current_delay().count(), 10);
  EXPECT_NO_THROW(backoff.record_failure());
  EXPECT_EQ(backoff.current_delay().count(), 20);
  EXPECT_NO_THROW(backoff.record_failure());
  EXPECT_EQ(backoff.current_delay().count(), 40);
  EXPECT_NO_THROW(backoff.record_failure());
  EXPECT_EQ(backoff.current_delay().count(), 50);

  EXPECT_NO_THROW(backoff.record_success());
  EXPECT_EQ(backoff.current_delay().count(), 10);

  EXPECT_NO_THROW(backoff.record_failure());
  EXPECT_NO_THROW(backoff.record_failure());
  EXPECT_NO_THROW(backoff.record_failure());
  EXPECT_NO_THROW(backoff.record_failure());
  EXPECT_THROW(backoff.record_failure(), std::exception);
}
