#include "gh/completion_queue.hpp"

#include <gtest/gtest.h>

namespace gh {
namespace detail {
struct base_completion_queue_test_only {
  static grpc::CompletionQueue* get_raw_queue(base_completion_queue& q) {
    return q.cq();
  }
};
}
}

/**
 * @test Verify that one can create, run, and stop a gh::completion_queue.
 */
TEST(completion_queue, basic) {
  gh::completion_queue<> queue;

  std::atomic<int> cnt(0);
  std::atomic<int> cxl(0);
  auto functor = [&cnt, &cxl](auto const& op, bool ok) {
    if (not ok) {
      ++cxl;
    } else {
      ++cnt;
    }
  };

  using namespace std::chrono_literals;

  auto canceled = queue.make_relative_timer(5ms, "test-canceled", functor);
  canceled->cancel();
  auto timer = queue.make_relative_timer(5ms, "test-timer", functor);
  std::thread t([&queue]() { queue.run(); });

  for (int i = 0; i != 100 and cnt.load() == 0; ++i) {
    std::this_thread::sleep_for(10ms);
  }
  ASSERT_EQ(cnt.load(), 1);
  ASSERT_EQ(cxl.load(), 1);

  queue.shutdown();
  t.join();
}

/**
 * @test Make sure gh::completion_queue handles errors gracefully.
 */
TEST(completion_queue, error) {
  using namespace std::chrono_literals;

  gh::completion_queue<> queue;
  std::thread t([&queue]() { queue.run(); });

  // ... manually create a timer with a null tag, that requires going
  // around the API ...
  grpc::CompletionQueue* cq = gh::detail::base_completion_queue_test_only::get_raw_queue(queue);

  std::atomic<int> cnt(0);
  auto op = queue.make_relative_timer(30ms, "alarm-after", [&cnt](auto const& op, bool ok) { ++cnt; });
  // ... also set an earlier alarm with an unused nullptr tag ...
  grpc::Alarm al1(cq, std::chrono::system_clock::now() + 10ms, nullptr);
  // ... and an alarm with a tag the queue does not know about ...
  grpc::Alarm al2(cq, std::chrono::system_clock::now() + 20ms, (void*)&cnt);

  for (int i = 0; i != 100 and cnt.load() == 0; ++i) {
    std::this_thread::sleep_for(40ms);
  }
  ASSERT_EQ(cnt.load(), 1);

  queue.shutdown();
  t.join();
}
