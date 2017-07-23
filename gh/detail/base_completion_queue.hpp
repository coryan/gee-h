#ifndef gh_detail_base_completion_queue_hpp
#define gh_detail_base_completion_queue_hpp

#include <gh/detail/base_async_op.hpp>

#include <grpc++/grpc++.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace gh {
namespace detail {
/// A helper class for testing.
struct base_completion_queue_test_only;

/**
 * The base class for the grpc::CompletionQueue wrappers.
 *
 * Refactor code common to all gh::completion_queue<> template instantiations.
 */
class base_completion_queue {
public:
  /// Stop the loop periodically to check if we should shutdown.
  // TODO() - the timeout should be configurable ...
  static std::chrono::milliseconds constexpr loop_timeout{50};

  base_completion_queue();
  virtual ~base_completion_queue();

  /// Run the completion queue loop.
  void run();

  /// Shutdown the completion queue loop.
  void shutdown();

protected:
  /**
   * The underlying completion queue pointer for the gRPC APIs.
   */
  friend struct ::gh::detail::base_completion_queue_test_only;
  grpc::CompletionQueue* cq() {
    return &queue_;
  }

  /// Create an operation and perform the common initialization
  template <typename op_type, typename Functor>
  std::shared_ptr<op_type> create_op(std::string name, Functor&& f) const {
    auto op = std::make_shared<op_type>();
    op->callback = [functor = std::move(f)](base_async_op & bop, bool ok) {
      auto const& op = dynamic_cast<op_type const&>(bop);
      functor(op, ok);
    };
    op->name = std::move(name);
    return op;
  }

  /// Save a newly created operation and return its gRPC tag.
  void* register_op(char const* where, std::shared_ptr<base_async_op> op);

  /// Get an operation given its gRPC tag.
  std::shared_ptr<base_async_op> unregister_op(void* tag);

private:
  mutable std::mutex mu_;
  using pending_ops_type = std::unordered_map<std::intptr_t, std::shared_ptr<base_async_op>>;
  pending_ops_type pending_ops_;

  grpc::CompletionQueue queue_;
  std::atomic<bool> shutdown_;
};
} // namespace detail
} // namespace gh

#endif // gh_detail_base_completion_queue_hpp
