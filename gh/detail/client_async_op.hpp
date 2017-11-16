#ifndef gh_client_async_op_hpp
#define gh_client_async_op_hpp
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
#include <gh/detail/async_rpc_op.hpp>
#include <gh/detail/rpc_backoff_policy.hpp>
#include <gh/detail/rpc_retry_policy.hpp>

namespace gh {
namespace detail {

template <typename Response>
struct noop_promise_notify {
  void operator()(std::promise<Response>& /*unused*/) const {
  }
};

template <typename Response, typename Functor>
class functor_promise_notify {
public:
  functor_promise_notify(Functor&& functor)
      : functor_(std::move(functor)) {
  }

  void operator()(std::promise<Response>& promise) {
    auto future = promise.get_future();
    return functor_(future);
  }

private:
  Functor functor_;
};

/**
 * Represent a pending operation in the etcd client.
 *
 * The etcd client class (::gh::etcd_client) creates objects of this type to represent a pending operation.  A client
 * might have multiple such pending operations running at any one time.  An instance of this class is created for
 * each operation, and it captures the current RPC policies (and their state).  The class is parametric on the type
 * of RPC call to make.
 *
 * @tparam Stub the type of the stub for RPC calls.
 * @tparam Member the member function pointer type, i.e. its signature.
 * @tparam Ptr the member function to call in the @p Stub.
 * @tparam Notifier where to notify changes of the promise state.
 */
template <typename Stub, typename Request, typename Response, typename Member, Member MemPtr, typename Notifier>
class client_async_op
    : public std::enable_shared_from_this<client_async_op<Stub, Request, Response, Member, MemPtr, Notifier>> {
public:
  using response_type = Response;
  using request_type = Request;
  client_async_op(
      std::unique_ptr<rpc_backoff_policy> backoff, std::unique_ptr<rpc_retry_policy> retry, request_type&& request,
      Notifier&& notifier)
      : request_(std::forward<request_type>(request))
      , backoff_(std::move(backoff))
      , retry_(std::move(retry))
      , notifier_(std::forward<Notifier>(notifier))
      , promise_() {
  }

  void start(std::shared_ptr<gh::completion_queue<>> queue, std::shared_ptr<grpc::Channel> channel) {
    auto self = this->shared_from_this();
    auto stub = std::make_unique<Stub>(channel);
    queue->async_rpc(stub.get(), MemPtr, std::move(request_), "foo", [self](auto const& op, bool ok) {
      if (not ok) {
        self->notify_failure(std::make_exception_ptr(std::runtime_error("aborted rpc")));
        return;
      }
      if (op.status.ok()) {
        self->notify_value(op.response);
        return;
      }
    });
  }

  std::future<response_type> get_future() {
    return promise_.get_future();
  }

private:
  void notify_value(response_type response) {
    promise_.set_value(std::forward<response_type>(response));
    notifier_(promise_);
  }

  void notify_failure(std::exception_ptr ex) {
    promise_.set_exception(ex);
    notifier_(promise_);
  }

private:
  request_type request_;
  std::unique_ptr<rpc_backoff_policy> backoff_;
  std::unique_ptr<rpc_retry_policy> retry_;
  Notifier notifier_;
  std::promise<response_type> promise_;
};

template <typename T>
struct create_client_async_op {
  using pass = std::false_type;
};

template <typename Response, typename Class, typename Request>
struct create_client_async_op<std::unique_ptr<grpc::ClientAsyncResponseReader<Response>> (Class::*)(
    grpc::ClientContext*, Request const&, grpc::CompletionQueue*)> {
  using pass = std::true_type;
  using response_t = Response;
  using request_t = Request;
  using class_t = Class;
  using member_t = std::unique_ptr<grpc::ClientAsyncResponseReader<Response>> (Class::*)(
      grpc::ClientContext*, Request const&, grpc::CompletionQueue*);

  using noop_t = detail::noop_promise_notify<response_t>;
  template <member_t ptr>
  using future_op = gh::detail::client_async_op<class_t, request_t, response_t, member_t, ptr, noop_t>;
};

} // namespace detail
} // namespace gh

#endif // gh_client_async_op_hpp
