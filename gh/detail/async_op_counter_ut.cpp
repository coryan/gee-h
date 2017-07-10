#include "gh/detail/async_op_counter.hpp"

#include <gtest/gtest.h>
#include <thread>

TEST(async_op_counter, basic) {
  gh::detail::async_op_counter counter;

  EXPECT_TRUE(counter.async_op_start());
  EXPECT_TRUE(counter.async_op_start("foo is ", 42, " and bar is ", std::string("7")));

  counter.async_op_done();
  counter.async_op_done("foo is ", 42, "in hex ", std::hex, 42);

  EXPECT_TRUE(counter.async_op_start());
  EXPECT_TRUE(counter.async_op_start());

  counter.shutdown();
  EXPECT_FALSE(counter.async_op_start());

  std::thread t([&counter]() {
    counter.async_op_done();
    counter.async_op_done();
  });

  counter.block_until_all_done();
  EXPECT_FALSE(counter.async_op_start());
  t.join();
}