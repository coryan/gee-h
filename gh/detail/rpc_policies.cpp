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

#include <sstream>

namespace gh {
namespace detail {
std::chrono::milliseconds exponential_backoff::on_failure() {
  auto current = current_delay_;
  current_delay_ = 2 * current_delay_;
  if (current_delay_ > max_delay_) {
    current_delay_ = max_delay_;
  }
  return current;
}

std::unique_ptr<rpc_backoff_policy> exponential_backoff::clone() const {
  return std::unique_ptr<rpc_backoff_policy>(new exponential_backoff(*this));
}

void exponential_backoff::validate_arguments() {
  if (min_delay_ > max_delay_) {
    std::ostringstream os;
    os << "exponential_backoff() - min_delay (" << min_delay_.count() << "ms) should be <= max_delay ("
       << max_delay_.count() << "ms)";
    throw std::invalid_argument(os.str());
  }
}

bool limited_errors::on_failure() {
  return current_count_++ < maximum_count_;
}

std::unique_ptr<rpc_retry_policy> limited_errors::clone() const {
  return std::unique_ptr<rpc_retry_policy>(new limited_errors(*this));
}

void limited_errors::validate_arguments() {
  if (maximum_count_ <= 0) {
    std::ostringstream os;
    os << "limited_errors() - maximum_count (" << maximum_count_ << ") should be >= 0";
    throw std::invalid_argument(os.str());
  }
}

bool limited_time::on_failure() {
  return std::chrono::system_clock::now() < deadline_;
}

std::unique_ptr<rpc_retry_policy> limited_time::clone() const {
  return std::unique_ptr<rpc_retry_policy>(new limited_time(duration_));
}

} // namespace detail
} // namespace gh
