//   Copyright 2017 Carlos O'Ryan
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.

#include <gh/detail/rpc_policies.hpp>

#include <gtest/gtest.h>
#include <thread>

TEST(rpc_policies, exponential_backoff) {
  using namespace std::chrono_literals;
  EXPECT_THROW(gh::detail::exponential_backoff(1s, 500ms), std::exception);

  EXPECT_NO_THROW(gh::detail::exponential_backoff(1s, 1500ms));

  gh::detail::exponential_backoff backoff(10ms, 50ms);
  EXPECT_EQ(backoff.on_failure().count(), 10);
  EXPECT_EQ(backoff.on_failure().count(), 20);
  EXPECT_EQ(backoff.on_failure().count(), 40);
  EXPECT_EQ(backoff.on_failure().count(), 50);
  EXPECT_EQ(backoff.on_failure().count(), 50);
}

TEST(rpc_policies, limited_errors) {
  EXPECT_THROW(gh::detail::limited_errors(0), std::exception);
  EXPECT_THROW(gh::detail::limited_errors(-1), std::exception);

  EXPECT_NO_THROW(gh::detail::limited_errors(10));

  gh::detail::limited_errors policy(3);
  EXPECT_TRUE(policy.on_failure());
  EXPECT_TRUE(policy.on_failure());
  EXPECT_TRUE(policy.on_failure());
  EXPECT_FALSE(policy.on_failure());
  EXPECT_FALSE(policy.on_failure());
}

TEST(rpc_policies, limited_time) {
  using namespace std::chrono_literals;
  EXPECT_NO_THROW(gh::detail::limited_time(3ms));

  gh::detail::limited_time policy(200us);
  bool failed = false;
  for (int i = 0; i != 10; ++i) {
    std::this_thread::sleep_for(200us);
    if (not policy.on_failure()) {
      failed = true;
      break;
    }
  }
  EXPECT_TRUE(failed);
}
