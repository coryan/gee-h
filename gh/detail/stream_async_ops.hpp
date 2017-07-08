#ifndef gh_detail_async_stream_ops_hpp
#define gh_detail_async_stream_ops_hpp
/**
 * @file
 *
 * Define the async operations necessary to work with streaming RPCs.
 */

#include <gh/detail/base_async_op.hpp>
#include <grpc++/grpc++.h>

namespace gh {
namespace detail {

/**
 * A wrapper to run an asynchronous Write() operation.
 *
 * Please see the documentation gh::completion_queue::async_write for details.
 */
template <typename W>
struct write_op : public base_async_op {
  W request;
};

/**
 * A wrapper to run an asynchronous Read() operation.
 *
 * Please see the documentation gh::completion_queue::async_read for details.
 */
template <typename R>
struct read_op : public base_async_op {
  R response;
};

/**
 * A wrapper around read-write RPC streams.
 */
template <typename W, typename R>
struct async_rdwr_stream {
  grpc::ClientContext context;
  std::unique_ptr<grpc::ClientAsyncReaderWriter<W, R>> client;

  using write_op = ::gh::detail::write_op<W>;
  using read_op = ::gh::detail::read_op<R>;
};

/// Match an operation to create ClientAsyncReaderWriter to its
/// signature - mismatch case.
template <typename M>
struct async_stream_create_requirements {
  using matches = std::false_type;
};

/// Match an operation to create ClientAsyncReaderWriter to its
/// signature - match case.
template <typename W, typename R>
struct async_stream_create_requirements<std::unique_ptr<grpc::ClientAsyncReaderWriter<W, R>>(
    grpc::ClientContext*, grpc::CompletionQueue*, void*)> {
  using matches = std::true_type;

  using write_type = W;
  using read_type = R;
  using stream_type = async_rdwr_stream<write_type, read_type>;
};

/**
 * A wrapper for a bi-directional streaming RPC client.
 *
 * Alternative name: a less awful grpc::ClientAsyncReaderWriter<W,R>.
 *
 * Please see the documentation of
 * gh::completion_queue::async_create_rdwr_stream for details.
 *
 * @tparam W the type of the requests in the streaming RPC.
 * @tparam R the type of the responses in the streaming RPC.
 *
 */
template <typename W, typename R>
struct create_async_rdwr_stream : public base_async_op {
  create_async_rdwr_stream()
      : stream(new async_rdwr_stream<W, R>) {
  }
  std::shared_ptr<async_rdwr_stream<W, R>> stream;

  using write_op = ::gh::detail::write_op<W>;
  using read_op = ::gh::detail::read_op<R>;
};

/**
 * A wrapper to run an asynchronous WritesDone() operation.
 *
 * Please see the documentation
 * gh::completion_queue::async_writes_done for details.
 */
struct writes_done_op : public base_async_op {};

/**
 * A wrapper to run an asynchronous Finish() operation.
 *
 * Please see the documentation
 * gh::completion_queue::async_finish for details.
 */
struct finish_op : public base_async_op {
  grpc::Status status;
};

} // namespace detail
} // namespace gh

#endif // gh_detail_async_stream_ops_hpp
