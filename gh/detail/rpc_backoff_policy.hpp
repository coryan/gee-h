#ifndef gh_detail_rpc_backoff_policy_hpp
#define gh_detail_rpc_backoff_policy_hpp
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

#include <chrono>
#include <memory>

namespace gh {
namespace detail {
/**
 * Define the interface for a backoff strategy.
 *
 * The etcd client needs to retry some operations, this interface allows the application to define different strategies
 * to pace the attempts to execute the operations.  The most common implementation would use a simple exponential
 * backoff, where the time between attempts doubles after a failure.  More complex strategies including exponential
 * backoff with limits, and with jitter are sometimes necessary to avoid accidental DoS attacks on the server.
 */
class rpc_backoff_policy {
public:
  virtual ~rpc_backoff_policy() = default;

  /**
   * Report a failure to the backoff strategy.
   *
   * @returns the delay the application should use before trying again.
   */
  virtual std::chrono::milliseconds on_failure() = 0;

  /// Create a copy of this policy
  virtual std::unique_ptr<rpc_backoff_policy> clone() const = 0;
};
} // namespace detail
} // namespace gh

#endif // gh_detail_rpc_backoff_policy_hpp
