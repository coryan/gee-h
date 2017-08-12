#ifndef gh_detail_backoff_strategy_hpp
#define gh_detail_backoff_strategy_hpp

#include <chrono>

namespace gh {
namespace detail {
/**
 * Define the interface for a backoff strategy.
 *
 * The etcd client needs to retry some operations, this interface allows the application to define different strategies
 * to pace the attempts to execute the operations.  The most common implementation would use a simple exponential
 * backoff, where the time between attempts doubles after a failure.  More complex strategies including exponential
 * backoff with limits, and with jitter are sometimes necessary to avoid accidental DoS attacks on the server.
 */
class backoff_strategy {
public:
  virtual ~backoff_strategy() = default;

  /// Report a failure to the backoff strategy, returns current delay.
  virtual std::chrono::milliseconds record_failure() = 0;
  /// Report a success to the backoff strategy, returns current delay.
  virtual std::chrono::milliseconds record_success() = 0;
  /// Returns the current delay in the backoff strategy.
  virtual std::chrono::milliseconds current_delay() const = 0;
};
} // namespace detail
} // namespace gh

#endif // gh_detail_backoff_strategy_hpp
