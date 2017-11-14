#ifndef gh_rpc_retry_policy_hpp
#define gh_rpc_retry_policy_hpp
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

namespace gh {
namespace detail {
/**
 * Define how the etcd client manages RPC failures.
 */
class rpc_retry_policy {
public:
  virtual ~rpc_retry_policy() = default;

  /// Return true if the application should try again.
  virtual bool on_failure() = 0;

  /// Create a copy of this policy
  virtual std::unique_ptr<rpc_retry_policy> clone() const = 0;
};
} // namespace detail
} // namespace gh

#endif // gh_rpc_retry_policy_hpp
