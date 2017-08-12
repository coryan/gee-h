#include <gh/detail/exponential_backoff.hpp>

#include <sstream>

namespace gh {
namespace detail {
std::chrono::milliseconds exponential_backoff::record_failure() {
  if (++attempt_count_ >= max_attempts_) {
    too_many_attempts();
  }
  current_delay_ = 2 * current_delay_;
  if (current_delay_ > max_delay_) {
    current_delay_ = max_delay_;
  }
  return current_delay_;
}

std::chrono::milliseconds exponential_backoff::record_success() {
  attempt_count_ = 0;
  current_delay_ = min_delay_;
  return current_delay_;
}

std::chrono::milliseconds exponential_backoff::current_delay() const {
  return current_delay_;
}

void exponential_backoff::validate_arguments() {
  if (min_delay_ > max_delay_) {
    std::ostringstream os;
    os << "exponential_backoff() - min_delay (" << min_delay_.count() << "ms) should be <= max_delay ("
       << max_delay_.count() << "ms)";
    throw std::invalid_argument(os.str());
  }
  if (max_attempts_ <= 0) {
    std::ostringstream os;
    os << "exponential_backoff() - max_attempts (" << max_attempts_ << ") should be > 0";
    throw std::invalid_argument(os.str());
  }
}

void exponential_backoff::too_many_attempts() {
  std::ostringstream os;
  os << "exponential_backoff::too_many_attempts (" << attempt_count_ << ")";
  throw std::runtime_error(os.str());
}

} // namespace detail
} // namespace gh
