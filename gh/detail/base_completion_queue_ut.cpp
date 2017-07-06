#include "gh/detail/base_completion_queue.hpp"

#include <gtest/gtest.h>
#include <future>

/**
 * @test Verify that we can run and shutdown a completion queue.
 */
TEST(base_completion_queue, run_shutdown) {
  gh::detail::base_completion_queue queue;
  // run the event loop in a separate thread ...
  std::promise<void > start;
  std::promise<void> end;
  std::thread t([&]() {
    start.set_value();
    queue.run();
    end.set_value();
  });

  using namespace std::chrono_literals;

  auto start_fut = start.get_future();
  bool start_ready = false;
  for (int i = 0; i != 10; ++i) {
    if (start_fut.wait_for(50ms) == std::future_status::ready) {
      start_ready = true;
      break;
    }
  }
  ASSERT_TRUE(start_ready);

  queue.shutdown();
  std::this_thread::sleep_for(gh::detail::base_completion_queue::loop_timeout);

  auto end_fut = end.get_future();
  bool end_ready = false;
  for (int i = 0; i != 10; ++i) {
    if (end_fut.wait_for(50ms) == std::future_status::ready) {
      end_ready = true;
      break;
    }
  }
  ASSERT_TRUE(end_ready);

  t.join();
}
