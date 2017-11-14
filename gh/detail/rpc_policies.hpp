#ifndef gh_detail_rpc_policies_hpp
#define gh_detail_rpc_policies_hpp
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

#include <gh/detail/rpc_backoff_policy.hpp>
#include <gh/detail/rpc_retry_policy.hpp>
#include <stdexcept>

namespace gh {
namespace detail {
class exponential_backoff : public rpc_backoff_policy {
public:
  template <typename min_duration_type, typename max_duration_type>
  exponential_backoff(min_duration_type min_delay, max_duration_type max_delay)
      : min_delay_(std::chrono::duration_cast<std::chrono::milliseconds>(min_delay))
      , max_delay_(std::chrono::duration_cast<std::chrono::milliseconds>(max_delay))
      , current_delay_(min_delay) {
    validate_arguments();
  }

  std::chrono::milliseconds on_failure() override;
  std::unique_ptr<rpc_backoff_policy> clone() const override;

private:
  void validate_arguments();

private:
  std::chrono::milliseconds min_delay_;
  std::chrono::milliseconds max_delay_;
  std::chrono::milliseconds current_delay_;
};

class limited_errors : public rpc_retry_policy {
public:
  limited_errors(int maximum_count)
      : maximum_count_(maximum_count)
      , current_count_(0) {
    validate_arguments();
  }

  bool on_failure() override;
  std::unique_ptr<rpc_retry_policy> clone() const override;

private:
  void validate_arguments();

private:
  int maximum_count_;
  int current_count_;
};

class limited_time : public rpc_retry_policy {
public:
  template <typename Rep, typename Period>
  limited_time(std::chrono::duration<Rep, Period> const& maximum_duration)
      : duration_(std::chrono::duration_cast<std::chrono::milliseconds>(maximum_duration))
      , deadline_(std::chrono::system_clock::now() + duration_) {
  }

  bool on_failure() override;
  std::unique_ptr<rpc_retry_policy> clone() const override;

private:
  std::chrono::milliseconds duration_;
  std::chrono::system_clock::time_point deadline_;
};
} // namespace detail
} // namespace gh

#endif // gh_detail_rpc_policies_hpp
