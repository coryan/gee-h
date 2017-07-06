#ifndef gh_completion_queue_hpp
#define gh_completion_queue_hpp

#include <gh/detail/base_completion_queue.hpp>
#include <gh/detail/base_async_op.hpp>
#include <gh/detail/deadline_timer.hpp>
#include <gh/detail/default_grpc_interceptor.hpp>

#include <functional>
#include <future>
#include <memory>

namespace gh {

/// A struct to indicate the APIs should return futures instead of invoking a callback.
struct use_future {};

/**
 * Wrap a gRPC completion queue.
 *
 * Alternative name: less_awful_completion_queue.  The grpc::CompletionQueue is not much of an abstraction, nor is it
 * idiomatic C++11 (or C++14 or C++17).  This wrapper makes it easier to write asynchronous operations that call
 * functors (lambdas, std::function<>, etc) when the operation completes.
 *
 * @tparam grpc_interceptor_t mediate all calls to the gRPC library, though mostly to grpc::CompletionQueue.  The
 * default inlines all the calls, so it is basically zero overhead.  The main reason to change it is to mock the
 * gRPC++ APIs in tests.
 */
template <typename grpc_interceptor_t = detail::default_grpc_interceptor>
class completion_queue : public detail::base_completion_queue {
public:
  //@{
  /**
   * @name type traits
   */
  using grpc_interceptor_type = grpc_interceptor_t;
  //}

  explicit completion_queue(
      grpc_interceptor_type interceptor = grpc_interceptor_type())
      : detail::base_completion_queue()
      , interceptor_(std::move(interceptor)) {
  }

  explicit completion_queue(grpc_interceptor_type&& interceptor)
      : detail::base_completion_queue()
      , interceptor_(std::move(interceptor)) {
  }

  /**
   * Call the functor when the deadline timer expires.
 *
 * Notice that system_clock is not guaranteed to be monotonic, which
 * makes it a poor choice in some cases.  gRPC is not dealing with
 * time intervals small enough to make a difference, so it is Okay,
 * I guess.
 */
  template <typename Functor>
  std::shared_ptr<detail::deadline_timer> make_deadline_timer(
      std::chrono::system_clock::time_point deadline, std::string name,
      Functor&& f) {
    auto op = create_op<detail::deadline_timer>(std::move(name), std::move(f));
    void* tag = register_op("deadline_timer()", op);
    op->deadline = deadline;
    interceptor_.make_deadline_timer(op, cq(), tag);
    return op;
  }

  /// Call the functor N units of time from now.
  template <typename duration_type, typename Functor>
  std::shared_ptr<detail::deadline_timer> make_relative_timer(
      duration_type duration, std::string name, Functor&& functor) {
    auto deadline = std::chrono::system_clock::now() + duration;
    return make_deadline_timer(deadline, std::move(name), std::move(functor));
  }

#if 0
  /// Return the underlying
  operator grpc::CompletionQueue*() {
    return cq();
  }

  grpc_interceptor_type& interceptor() {
    return interceptor_;
  }

  /**
   * Start an asynchronous RPC call and invoke a functor with the results.
   *
   * Consider a typical gRPC:
   *
   * @code
   * service Echo {
   *    rpc Echo(Request) returns (Response) {}
   * }
   * @endcode
   *
   * When making an asynchronous request use:
   *
   * @code
   * completion_queue queue = ...;
   * std::unique_ptr<Echo::Stub> client = ...;
   * auto op = queue.async_rpc(
   *     "debug string", stub, Echo::Stub::AsyncEcho,
   *     [](auto op, bool ok) { });
   * @endcode
   *
   * The jb::etcd::completion_queue will call the lambda expression you
   * provided.  The @a ok flag indicates if the operation was canceled.
   * The @a op parameter will be of type:
   *
   * @code
   * async_op<EchoResponse> const&
   * @endcode
   *
   * This function deduces the type of Request and Response parameter
   * based on the member function argument.
   */
  template <typename C, typename M, typename W, typename Functor>
  void async_rpc(
      C* async_client, M C::*call, W&& request, std::string name, Functor&& f) {
    using requirements = detail::async_op_requirements<M>;
    static_assert(
        requirements::matches::value,
        "The member function signature does not match: "
        "std::unique_ptr<grpc::ClientResponseReader<R>>("
        "grpc::ClientContext*,W const&,grpc::CompletionQueue*)");
    using request_type = typename requirements::request_type;
    using response_type = typename requirements::response_type;
    static_assert(
        std::is_same<W, request_type>::value,
        "Mismatch request parameter type vs. operation signature");

    using op_type = detail::async_op<request_type, response_type>;
    auto op = create_op<op_type>(std::move(name), std::move(f));
    void* tag = register_op("async_rpc()", op);
    op->request.Swap(&request);
    interceptor_.async_rpc(async_client, call, op, cq(), tag);
  }

  /**
   * Start an asynchronous RPC call and return a future to wait until
   * it completes.
   *
   * Consider a typical gRPC:
   *
   * @code
   * service Echo {
   *    rpc Echo(Request) returns (Response) {}
   * }
   * @endcode
   *
   * When making an asynchronous request use:
   *
   * @code
   * completion_queue queue = ...;
   * std::unique_ptr<Echo::Stub> client = ...;
   * auto fut = queue.async_rpc(
   *     "debug string", stub, Echo::Stub::AsyncEcho, jb::etcd::use_future());
   * // block until completed ..
   * auto result = fut.get();
   * @endcode
   *
   * The application can block (using std::future::get()) or poll (using
   * std::future::wait_for()) until the asynchronous stream creation
   * completes.  The future will hold a result of whatever type the
   * RPC returns.
   *
   * Why use this instead of simply making a synchronous RPC?  Mainly
   * because most of the gRPC operations that jb::etcd makes are
   * asynchronous, so this fits in the framework.  Also because it was
   * easier to mock the RPCs and do fault injection with the
   * asynchronous APIs.
   *
   * @tparam C the type of the stub to make the request on
   * @tparam M the type of the member function on the stub to make the
   * request
   * @tparam W the type of the request
   *
   * @returns a shared future to wait until the operation completes, of type
   * std::shared_future<ResponseType>.
   */
  template <typename C, typename M, typename W>
  std::shared_future<typename detail::async_op_requirements<M>::response_type>
  async_rpc(
      C* async_client, M C::*call, W&& request, std::string name, use_future) {
    auto promise = std::make_shared<std::promise<
        typename detail::async_op_requirements<M>::response_type>>();
    this->async_rpc(
        async_client, call, std::move(request), std::move(name),
        [promise](auto& op, bool ok) {
          if (not ok) {
            promise->set_exception(std::make_exception_ptr(
                std::runtime_error("async rpc cancelled")));
            return;
          }
          // TODO() - we would want to use std::move(), but (a)
          // protobufs do not have move semantics (yuck), and (b) we
          // have a const& op parameter, so we would need to change that
          // too.
          promise->set_value(op.response);
        });
    return promise->get_future().share();
  }

  /**
   * Create a new asynchronous read-write stream and call the functor
   * when it is constructed and ready.
   *
   * Consider a typical bi-directional streaming gRPC:
   *
   * @code
   * service Echo {
   *    rpc Echo(stream Request) returns (stream Response) {}
   * }
   * @endcode
   *
   * When asynchronously creating the stream use:
   *
   * @code
   * completion_queue queue = ...;
   * std::unique_ptr<Echo::Stub> client = ...;
   * auto op = queue.async_create_rdwr_stream(
   *     "debug string", stub, Echo::Stub::AsyncEcho,
   *     [](auto stream, bool ok) { });
   * @endcode
   *
   * The jb::etcd::completion_queue will call the lambda expression you
   * provided.  The @a ok flag indicates if the operation was canceled.
   * The @a stream parameter will be of type:
   *
   * @code
   * std::shared_ptr<async_rdwr_stream<Request, Response>>
   * @endcode
   *
   * This function deduces the type of read-write stream to create
   * based on the member function argument.  Typically it would be
   * used as follows:
   */
  template <typename C, typename M, typename Functor>
  void async_create_rdwr_stream(
      C* async_client, M C::*call, std::string name, Functor&& f) {
    using requirements = detail::async_stream_create_requirements<M>;
    static_assert(
        requirements::matches::value,
        "The member function signature does not match: "
        "std::unique_ptr<grpc::ClientAsyncReaderWriter<W, R>>("
        "grpc::ClientContext*,grpc::CompletionQueue*,void*)");
    using write_type = typename requirements::write_type;
    using read_type = typename requirements::read_type;

    using op_type = detail::create_async_rdwr_stream<write_type, read_type>;
    auto op = std::make_shared<op_type>();
    op->callback = [functor = std::move(f)](
        detail::base_async_op & bop, bool ok) {
      auto& op = dynamic_cast<op_type&>(bop);
      functor(op.stream, ok);
    };
    op->name = std::move(name);
    void* tag = register_op("async_create_rdwr_stream()", op);
    interceptor_.async_create_rdwr_stream(async_client, call, op, cq(), tag);
  }

  /**
   * Start the creation of a new asynchronous read-write stream and
   * return a future to wait until it is constructed and ready.
   *
   * Consider a typical bi-directional streaming gRPC:
   *
   * @code
   * service Echo {
   *    rpc Echo(stream Request) returns (stream Response) {}
   * }
   * @endcode
   *
   * If you want to use that stream asynchronously you must also use
   * the asynchronous APIs to create it.  Sometimes it is necessary or
   * convenient to block until the asynchronous creation completes.
   * This function makes it easy to do so, taking care of the promise
   * creation and reporting:
   *
   * @code
   * completion_queue queue = ...;
   * std::unique_ptr<Echo::Stub> client = ...;
   * auto fut = queue.async_create_rdwr_stream(
   *     "debug string", stub, Echo::Stub::AsyncEcho,
   *     jb::etcd::use_future());
   * // block until the result is ready or an exception ...
   * auto result = fut.get();
   * @endcode
   *
   * The application can block (using std::future::get()) or poll (using
   * std::future::wait_for()) until the asynchronous stream creation
   * completes.  The future will hold a result of type:
   *
   * @code
   * std::shared_ptr<async_rdwr_stream<Request, Response>>
   * @endcode
   *
   * When the operation is successful, and an exception otherwise.  For
   * applications that prefer a callback see the separate overload of
   * this function with a Functor parameter.
   *
   * @tparam C the type of the stub to make the request on
   * @tparam M the type of the member function on the stub to make the
   * request
   *
   * @returns a shared future to wait until the operation completes, of type
   * std::shared_future<std::shared_ptr<async_rdwr_stream<Request, Response>>>
   */
  template <typename C, typename M>
  std::shared_future<std::shared_ptr<
      typename detail::async_stream_create_requirements<M>::stream_type>>
  async_create_rdwr_stream(
      C* async_client, M C::*call, std::string name, use_future) {
    using ret_type = std::shared_ptr<
        typename detail::async_stream_create_requirements<M>::stream_type>;
    auto promise = std::make_shared<std::promise<ret_type>>();
    this->async_create_rdwr_stream(
        async_client, call, name, [promise](auto stream, bool ok) {
          // intercept canceled operations and raise an exception ...
          if (not ok) {
            promise->set_exception(std::make_exception_ptr(
                std::runtime_error("async create_rdwr_stream cancelled")));
            return;
          }
          promise->set_value(stream);
        });
    return promise->get_future().share();
  }

  /**
   * Make an asynchronous call to Write() and call the functor
   * when it is completed.
   */
  template <typename W, typename R, typename Functor>
  void async_write(
      detail::async_rdwr_stream<W, R>& stream, W&& request, std::string name,
      Functor&& f) {
    auto op = create_op<detail::write_op<W>>(std::move(name), std::move(f));
    void* tag = register_op("async_write()", op);
    op->request.Swap(&request);
    interceptor_.async_write(stream, op, tag);
  }

  /**
   * Make an asynchronous call to Read() and call the functor
   * when it is completed.
   */
  template <typename W, typename R, typename Functor>
  void async_read(
      detail::async_rdwr_stream<W, R> const& stream, std::string name,
      Functor&& f) {
    auto op = create_op<detail::read_op<R>>(std::move(name), std::move(f));
    void* tag = register_op("async_read()", op);
    interceptor_.async_read(stream, op, tag);
  }

  /**
   * Make an asynchronous call to WritesDone() and call the functor
   * when it is completed.
   */
  template <typename W, typename R, typename Functor>
  void async_writes_done(
      detail::async_rdwr_stream<W, R> const& stream, std::string name,
      Functor&& f) {
    auto op = create_op<detail::writes_done_op>(std::move(name), std::move(f));
    void* tag = register_op("async_writes_done()", op);
    interceptor_.async_writes_done(stream, op, tag);
  }

  /**
   * Make an asynchronous call to Finish() and return a std::future<>.
   */
  template <typename W, typename R>
  std::shared_future<void> async_writes_done(
      detail::async_rdwr_stream<W, R> const& stream, std::string name,
      use_future) {
    auto promise = std::make_shared<std::promise<void>>();
    this->async_writes_done(stream, name, [promise](auto& op, bool ok) {
      if (not ok) {
        promise->set_exception(std::make_exception_ptr(
            std::runtime_error("async writes done cancelled")));
        return;
      }
      promise->set_value();
    });
    return promise->get_future().share();
  }

  /**
   * Make an asynchronous call to Finish() and call the functor
   * when it is completed.
   */
  template <typename W, typename R, typename Functor>
  void async_finish(
      detail::async_rdwr_stream<W, R> const& stream, std::string name,
      Functor&& f) {
    auto op = create_op<detail::finish_op>(std::move(name), std::move(f));
    void* tag = register_op("async_finish()", op);
    interceptor_.async_finish(stream, op, tag);
  }

  /**
   * Make an asynchronous call to Finish() and return a std::future<>.
   */
  template <typename W, typename R>
  std::shared_future<grpc::Status> async_finish(
      detail::async_rdwr_stream<W, R> const& stream, std::string name,
      use_future) {
    auto promise = std::make_shared<std::promise<grpc::Status>>();
    this->async_finish(stream, name, [promise](auto& op, bool ok) {
      if (not ok) {
        promise->set_exception(std::make_exception_ptr(
            std::runtime_error("async finished cancelled")));
        return;
      }
      promise->set_value(op.status);
    });
    return promise->get_future().share();
  }

#endif /* 0 */

private:
  /**
   * Create an operation and perform the common initialization
   *
   * This template function refactors the code for several member functions of the class.  They all need to create
   * different types of asynchronous operation objects, move the name and functor of the object to them, and then
   * create the callback that will (a) downcast the asynchronous operation object, and (b) call the user-provided
   * functor.
   *
   * @tparam op_type the type derived from gh::base_async_op to create.
   * @tparam Functor the type of the user-provided functor to call when the operation completes.
   * @param name the name (for debugging purposes) of the operation.
   * @param f the user-provided functor to call when the operation completes.
   * @return a new instance of @c op_type with the name and callback fields filled in.
   */
  template <typename op_type, typename Functor>
  std::shared_ptr<op_type> create_op(std::string name, Functor&& f) const {
    auto op = std::make_shared<op_type>();
    op->callback = [functor = std::move(f)](
        detail::base_async_op & bop, bool ok) {
      auto const& op = dynamic_cast<op_type const&>(bop);
      functor(op, ok);
    };
    op->name = std::move(name);
    return op;
  }

private:
  /// The interceptor to catch all interactions with the underlying grpc::CompletionQueue.
  grpc_interceptor_type interceptor_;
};

} // namespace gh

#endif // gh_completion_queue_hpp
