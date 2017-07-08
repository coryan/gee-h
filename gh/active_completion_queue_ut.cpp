#include "gh/active_completion_queue.hpp"

#include <gtest/gtest.h>

/**
 * @test Verify that gh::active_completion_queue works as expected.
 */
TEST(active_completion_queue, basic) {
  auto shq = std::make_shared<gh::active_completion_queue>();
  EXPECT_NO_THROW(shq.reset());

  EXPECT_NO_THROW(gh::active_completion_queue());

  {
    gh::active_completion_queue orig;
    gh::active_completion_queue copy(std::move(orig));
    EXPECT_FALSE(orig);
    EXPECT_TRUE(copy);
  }

  {
    gh::active_completion_queue orig;
    EXPECT_TRUE(orig);
    gh::active_completion_queue copy;
    EXPECT_TRUE(copy);

    copy = std::move(orig);
    EXPECT_FALSE(orig);
    EXPECT_TRUE(copy);
  }

  auto cq = std::make_shared<gh::completion_queue<>>();
  std::thread t([cq]() { cq->run(); });
  EXPECT_TRUE(t.joinable());

  {
    gh::active_completion_queue owner(std::move(cq), std::move(t));
    EXPECT_TRUE(owner);
    EXPECT_FALSE(t.joinable());
  }
}
