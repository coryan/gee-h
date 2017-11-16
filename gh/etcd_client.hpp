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
#include <gh/detail/client_async_op.hpp>
#include <gh/detail/rpc_backoff_policy.hpp>
#include <gh/detail/rpc_retry_policy.hpp>

#include <memory>
#include <string>

#include <etcd/etcdserver/etcdserverpb/rpc.grpc.pb.h>

namespace gh {

/**
 * A client to access a etcd cluster.
 */
class etcd_client {
public:
  etcd_client(std::shared_ptr<grpc::ChannelCredentials> credentials, std::string url);

  template <typename Functor>
  void grant_lease(std::int64_t lease, std::shared_ptr<gh::completion_queue<>> cq, Functor&& functor) {
    auto rpc_retry = rpc_retry_->clone();
    auto rpc_backoff = rpc_backoff_->clone();
    detail::functor_promise_notify<etcdserverpb::LeaseGrantResponse, Functor> notifier(std::move(functor));
  }

  std::future<etcdserverpb::LeaseGrantResponse>
  grant_lease(std::int64_t lease, std::shared_ptr<gh::completion_queue<>> cq, gh::use_future) {
    using d = detail::create_client_async_op<decltype(&etcdserverpb::Lease::Stub::AsyncLeaseGrant)>;
    static_assert(d::pass::value, "no match");
    using op_t = d::template future_op<&etcdserverpb::Lease::Stub::AsyncLeaseGrant>;
    d::request_t request;
    request.set_id(lease);
    request.set_ttl(100);
    auto op = std::make_shared<op_t>(rpc_backoff_->clone(), rpc_retry_->clone(), std::move(request), d::noop_t());

    op->start(cq, current_channel());

    return op->get_future();
  }

private:
  std::shared_ptr<grpc::Channel> current_channel();

private:
  std::unique_ptr<gh::detail::rpc_backoff_policy> rpc_backoff_;
  std::unique_ptr<gh::detail::rpc_retry_policy> rpc_retry_;
  std::shared_ptr<grpc::ChannelCredentials> credentials_;

  mutable std::mutex mu_;
  std::vector<std::string> urls_;
};
} // namespace gh

#endif // gh_etcd_client_hpp
