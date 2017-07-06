#ifndef gh_detail_deadline_timer_hpp
#define gh_detail_deadline_timer_hpp

#include "gh/detail/base_async_op.hpp"

#include "grpc++/alarm.h"

namespace gh {
namespace detail {
/**
 * A wrapper for deadline timers.
 */
struct deadline_timer : public base_async_op {
  // Safely cancel the timer, in the thread that cancels the timer we simply flag it as canceled.  We only release
  // resources in the thread where the timer is fired, i.e., the thread running the completion queue loop.
  void cancel() {
    if ((bool)alarm_) {
      alarm_->Cancel();
    }
  }

  std::chrono::system_clock::time_point deadline;

private:
  friend struct default_grpc_interceptor;
  std::unique_ptr<grpc::Alarm> alarm_;
};
} // namespace detail
} // namespace gh

#endif // gh_detail_deadline_timer_hpp
