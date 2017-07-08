#ifndef gh_detail_async_rpc_op_hpp
#define gh_detail_async_rpc_op_hpp

#include <gh/detail/base_async_op.hpp>
#include <grpc++/grpc++.h>

namespace gh {
namespace detail {

/// Determine the Request and Response parameter for an RPC based on
/// the Stub signature  - mismatch case.
template <typename M>
struct async_rpc_op_requirements {
  using matches = std::false_type;
};

/// Determine the Request and Response parameter for an RPC based on
/// the Stub signature  - mismatch case.
template <typename W, typename R>
struct async_rpc_op_requirements<std::unique_ptr<grpc::ClientAsyncResponseReader<R>>(
    grpc::ClientContext*, W const&, grpc::CompletionQueue*)> {
  using matches = std::true_type;

  using request_type = W;
  using response_type = R;
};

/**
 * A wrapper for asynchronous unary operations.
 *
 * Please see jb::etcd::completion_queue::async_rpc for details.
 *
 * @tparam R the type of the response in the RPC operation.
 */
template <typename W, typename R>
struct async_rpc_op : public base_async_op {
  grpc::ClientContext context;
  grpc::Status status;
  W request;
  R response;
  std::unique_ptr<grpc::ClientAsyncResponseReader<R>> rpc;
};

} // namespace detail
} // namespace gh

#endif // gh_detail_async_rpc_op_hpp
