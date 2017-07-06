#ifndef gh_detail_default_grpc_interceptor_hpp
#define gh_detail_default_grpc_interceptor_hpp

#include <grpc++/grpc++.h>
#include <memory>

namespace gh {
namespace detail {

/**
 * Provides a dependency injection point to mock the gRPC++ library.
 *
 * In some of the tests we need to simulate the behavior of the gRPC++ library, and even the behavior of the servers
 * that you communicate with using gRPC++.  This class defines a relatively narrow interface where the Gee-H library
 * can intercept all gRPC++ calls.  Please see gh::detail::mocked_grpc_interceptor for a mocked version.
 */
struct default_grpc_interceptor {
  /// Post a timer to the completion queue.
  template <typename op_type>
  void make_deadline_timer(std::shared_ptr<op_type> op, grpc::CompletionQueue* cq, void* tag) {
    op->alarm_ = std::make_unique<grpc::Alarm>(cq, op->deadline, tag);
  }

#if 0
  /// Post an asynchronous RPC operation via the completion queue
  template <typename C, typename M, typename op_type>
  void async_rpc(
      C* async_client, M C::*call, std::shared_ptr<op_type> op,
      grpc::CompletionQueue* cq, void* tag) {
    op->rpc = (async_client->*call)(&op->context, op->request, cq);
    op->rpc->Finish(&op->response, &op->status, tag);
  }

  /// Post an asynchronous operation to create a rdwr RPC stream
  template <typename C, typename M, typename op_type>
  void async_create_rdwr_stream(
      C* async_client, M C::*call, std::shared_ptr<op_type> op,
      grpc::CompletionQueue* cq, void* tag) {
    op->stream->client = (async_client->*call)(&op->stream->context, cq, tag);
  }

  /// Post an asynchronous Write() operation over a rdwr RPC stream
  template <typename W, typename R, typename op_type>
  void async_write(
      async_rdwr_stream<W, R> const& stream, std::shared_ptr<op_type> op,
      void* tag) {
    stream.client->Write(op->request, tag);
  }

  /// Post an asynchronous Read() operation over a rdwr RPC stream
  template <typename W, typename R, typename op_type>
  void async_read(
      async_rdwr_stream<W, R> const& stream, std::shared_ptr<op_type> op,
      void* tag) {
    stream.client->Read(&op->response, tag);
  }

  /// Post an asynchronous WriteDone() operation over a rdwr RPC stream
  template <typename W, typename R, typename op_type>
  void async_writes_done(
      async_rdwr_stream<W, R> const& stream, std::shared_ptr<op_type> op,
      void* tag) {
    stream.client->WritesDone(tag);
  }

  /// Post an asynchronous Finish() operation over a rdwr RPC stream
  template <typename W, typename R, typename op_type>
  void async_finish(
      async_rdwr_stream<W, R> const& stream, std::shared_ptr<op_type> op,
      void* tag) {
    stream.client->Finish(&op->status, tag);
  }
#endif /* 0 */
};

} // namespace detail
} // namespace gh

#endif // gh_detail_default_grpc_interceptor_hpp
