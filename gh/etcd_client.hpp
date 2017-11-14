#ifndef gh_etcd_client_hpp
#define gh_etcd_client_hpp
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

#include <gh/completion_queue.hpp>
#include <gh/detail/rpc_backoff_policy.hpp>
#include <gh/detail/rpc_retry_policy.hpp>
#include <memory>
#include <string>

namespace gh {
/**
 * A client to access a etcd cluster.
 */
class etcd_client {
public:
  template <typename Functor>
  void grant_lease(std::int64_t lease, std::shared_ptr<gh::completion_queue<>> cq, Functor&& f) {
  }

  std::future<std::int64_t> grant_lease(
      std::int64_t lease, std::shared_ptr<gh::completion_queue<>> cq, gh::use_future) {
    // gh::detail::client_op<std::int64_t>
    return std::future<std::int64_t>();
  }

private:
  etcd_client(std::string url);

private:
  std::unique_ptr<gh::detail::rpc_backoff_policy> rpc_backoff_;
  std::unique_ptr<gh::detail::rpc_retry_policy> rpc_retry_;
};
} // namespace gh

#endif // gh_etcd_client_hpp
