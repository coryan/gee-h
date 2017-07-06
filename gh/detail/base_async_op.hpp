#ifndef gh_detail_base_async_op_hpp
#define gh_detail_base_async_op_hpp

#include <functional>
#include <memory>
#include <string>

namespace gh {
namespace detail {

/**
 * Base class for all asynchronous operations containers.
 *
 * These are helper classes used in the implementation of gh::completion_queue, and not intended by direct use by
 * the application.  In general the application requests an asynchronous operation from a completion queue, and
 * provides a functor that the completion queue should call when the operation is completed (or cancelled).  The
 * completion queue creates an object with dynamic type derived from @c gh::detail::base_async_op.  The completion
 * queue saves the functor and any other data required to complete the operation in that object.  Then it issues the
 * asynchronous call.  When it completes it invokes the functor provided by the application, passing as a parameter
 * any relevant information, such as the operation results, any error status, and whether the operation completed
 * successfully or was canceled.  After the functor returns the completion queue releases all resources associated
 * with the asynchronous operation.  It is the application's responsibility to make copies of any parameters provided
 * in the functor callback.
 *
 * In general the classes are pretty raw abstractions because they are not intended for general use.
 */
struct base_async_op {
  base_async_op() {
  }

  /// Make sure full destructor of derived class is called.
  virtual ~base_async_op() {
  }

  /**
   * Callback for the completion queue.
   *
   * It seems more natural to use a virtual function, but the derived classes will just create a std::function<> to
   * wrap the user-supplied functor, so this is actually less code.
   */
  std::function<void(base_async_op&, bool)> callback;

  /// For debugging
  // TODO() - consider using a tag like a char const*
  std::string name;
};

} // namespace detail
} // namespace gh

#endif // gh_detail_base_async_op_hpp
