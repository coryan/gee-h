# Supporting etcd Clusters in Gee-H

Author: coryan@<br>
Status: (**draft** | in review | final | abandoned)

## Objective

While [etcd](https://github.com/coreos/etcd) is normally configured as a cluster of servers, [gRPC](https://grpc.io) 
does not natively support multiple addresses with failover.
Moreover, the members of the etcd cluster might change over time, and it is unlikely that any future gRPC mechanism 
will automatically support updates.
This document describes the components in Gee-H that support (a) automatically updating the cluster membership by 
periodically consulting the cluster, and (b) make multiple attempts to execute operations in the cluster, trying all 
the known cluster members if needed.

As usual with Gee-H we have some general requirements:

* The interfaces must look like natural C++14 APIs to any users of the library.
* The interfaces should not block unless the application requests a blocking operation.
* All blocking operations should be on `future<T>` objects allowing for exception reporting.
* The library should only use threads that are provided by the application to run any control loops.
  * The application may have configured those threads specially (e.g. at lower priority or pinned to specific CPUs).
* Control over failover policies must be given to the application.
* The default policies must work well in most circumstances.
* The library should be easy to use in the default case.

## Overview

We propose to implement a minimal etcd client library.  This client will be used in the leader election algorithm 
instead of direct RPC calls to etcd.  The client will conform to the following interface (all code snippets are in 
the `gh` namespace):

```cpp
class etcd_client {
public:
  // Uses the threads associated with @a q to keep the cluster membership updated.
  void keep_cluster_members_updated(std::shared_ptr<completion_queue> q);

  // Create an asynchronous operation to make a lease grant request.
  template<typename Functor> // f(future<std::int64_t>&) should be valid ...
  void grant_lease(
      std::shared_ptr<completion_queue> q, /* parameters unspecified */, Functor&& f);
  
  /// Ditto, but return a future.
  std::future<std::int64_t> grant_lease(
      std::shared_ptr<completion_queue> q, /* parameters unspecified */);
};
```

To create clients the application makes a relatively simple call
```cpp
/// Create a client given one initial URL
std::shared_ptr<etcd_client> create_client(
    std::string initial_url, etcd_client_config const& config);
```

To create a client with explicit policies:
```cpp
/// Create a client given one initial URL with explicit policies.
std::shared_ptr<etcd_client> create_client(
    std::string initial_url, etcd_client_config const& config,
    std::unique_ptr<RPCRetryPolicy> rpc_retry_policy);
```

## Detailed Design

Every function in the client will follow a similar pattern.  First the parameters of the function are wrapped into a 
`operation` object, e.g.:

```cpp
template<typename Functor> // f(future<std::int64_t>&) should be valid ...
void etcd_client::grant_lease(
    std::shared_ptr<completion_queue> q, /* parameters unspecified */, Functor&& f) {
    std::shared_ptr<client_async_op> op = create_async_op</*TBD*/>(
      /* parameters unspecified */, std::forward<Functor>(f),
      channel_source_->clone(),
      rpc_retry_policy_->clone());
    op->start(cq);
}
```

The `op` object will last for as long as the operation is running.
The `op` object contains the callback (which maps to a `std::promise<>` and a callback for the case returning a 
future), and it holds a copy of the current policies (because policies have state), and a copy of the current urls 
and the rules to convert them into channels.

The implementation works more or less like this:
```cpp
template<typename Functor, typename Request, typename Response>
void client_async_op<Functor, Request, Response>::start(
    std::shared_ptr<completion_queue> q) {
    auto self = shared_from_this();
    // ... find a working channel and call self->on_channel(...);
    find_working_channel(
        q, channel_source_, rpc_retry_policy_,
        [q, self](auto channel) { self->on_channel(q, channel); });
}
```

The implementation of `on_channel()` makes the RPC request:
```cpp
template<typename Functor, typename Request, typename Response>
void client_async_op<Functor, Request, Response>::on_channel(
    std::shared_ptr<completion_queue> q,
    std::shared_ptr<grpc::Channel> channel) {
    auto self = shared_from_this();
    auto stub = create_stub(channel);
    q->async_rpc(
        stub, &Blah::AsyncGrantLease, std::move(request),
        [q, self](auto rpc, auto ok) { self->on_rpc_completed(q, rpc, ok); });
}
```

On RPC failures the retry policy is used to schedule a timer that calls `start()`.  The channel source object tracks 
how many pending calls:
```cpp
template<typename Functor, typename Request, typename Response>
void client_async_op<Functor, Request, Response>::on_rpc_completed(
    std::shared_ptr<completion_queue> q,
    gh::async_rpc_op<> const& op, bool ok) {
    if (not ok) {
      // auto [abort, delay] = ... in C++17
      bool abort;
      std::chrono::milliseconds delay;
      std::tie(abort, delay) = retry_policy->on_failure(op.status);
      this->notify_exception(convert_to_exception(op.status));
      q->schedule_relative_timer(delay, [q, self](bool ok) { self->on_timer(ok); }
    }
    // call the functor ...
    this->notify_value(op.response);
}
```

## Alternatives Considered

We could have implemented this as a series of lambdas in the `etcd_client` class itself, but it got unwieldy very 
quickly.

## History

**2017-11-12**: Initial revision by coryan@
