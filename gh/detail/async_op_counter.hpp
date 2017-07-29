#ifndef gh_detail_async_op_counter_hpp
#define gh_detail_async_op_counter_hpp

#include <gh/detail/append_annotations.hpp>
#include <gh/log.hpp>

#include <condition_variable>
#include <mutex>
#include <utility>

namespace gh {
namespace detail {

/**
 * Helper class to track pending asynchronous operations.
 *
 * Objects that create multiple asynchronous operations sometimes need to block until all of them are finished,
 * otherwise when the operations complete they can crash the application.
 */
class async_op_counter {
public:
  async_op_counter()
      : mu_()
      , cv_()
      , pending_(0)
      , shutdown_(false) {
  }

  /**
   * Block until all pending asynchronous operations complete.
   *
   * Do not call this operation from the thread running the completion queue event loop.
   */
  void block_until_all_done();

  /// Shutdown all future asynchronous operations, return false in async_op_start()
  void shutdown() {
    std::lock_guard<std::mutex> lock(mu_);
    shutdown_ = true;
  }

  bool in_shutdown() const {
    std::lock_guard<std::mutex> lock(mu_);
    return shutdown_;
  }

  /**
   * Increment the count of pending asynchronous operations.
   *
   * This should be called just before the asynchronous operation starts.  If called afterwards the asynchronous
   * operation may complete before this function is called.  The annotations can be helpful in debugging.
   *
   * @tparam Annotations a variable list of annotations to help in debugging.
   * @param a the value of the annotations.
   * @return true if the operation should be started, false if the system is shutting down
   */
  template <typename... Annotations>
  bool async_op_start(Annotations&&... a) {
    GH_LOGGER_DECL(info, gh::log::instance(), logger);
    if (logger) {
      append_annotations(logger.get(), "async_op_start(): ", pending_, " ", std::forward<Annotations>(a)...);
      logger.write_to(gh::log::instance());
    }
    return add_op();
  }

  /**
   * Decrement the count of pending asynchronous operations.
   *
   * This should be called just after an asynchronous operation completes, either successfully or because it is
   * cancelled.  The annotations can be helpful in debugging.
   *
   * @tparam Annotations a variable list of annotations to help in debugging.
   * @param a the value of the annotations.
   */
  template <typename... Annotations>
  void async_op_done(Annotations&&... a) {
    GH_LOGGER_DECL(info, gh::log::instance(), logger);
    if (logger) {
      append_annotations(logger.get(), "async_op_done(): ", pending_, " ", std::forward<Annotations>(a)...);
      logger.write_to(gh::log::instance());
    }
    return del_op();
  }

private:
  /// Refactor non-template part of async_op_start()
  bool add_op() {
    std::unique_lock<std::mutex> lock(mu_);
    if (shutdown_) {
      return false;
    }
    ++pending_;
    return true;
  }

  /// Refactor non-template part of async_op_done()
  void del_op() {
    std::unique_lock<std::mutex> lock(mu_);
    if (--pending_ == 0) {
      lock.unlock();
      cv_.notify_one();
    }
  }

private:
  mutable std::mutex mu_;
  std::condition_variable cv_;
  int pending_;
  bool shutdown_;
};

/**
 * Helper class to increment/decrement the counter in operations that block using gh::use_future().
 */
class async_op_tracer {
public:
  async_op_tracer(async_op_counter& counter, char const* name)
      : counter_(counter)
      , name_(name)
      , status_(false) {
    status_ = counter_.async_op_start(name_);
  }
  ~async_op_tracer() {
    counter_.async_op_done(name_);
  }

  operator bool() const {
    return status_;
  }

  async_op_tracer(async_op_tracer&&) = delete;
  async_op_tracer& operator=(async_op_tracer&&) = delete;
  async_op_tracer(async_op_tracer const&) = delete;
  async_op_tracer& operator=(async_op_tracer const&) = delete;

private:
  async_op_counter& counter_;
  char const* name_;
  bool status_;
};

} // namespace detail
} // namespace gh

#endif // gh_detail_async_op_counter_hpp
