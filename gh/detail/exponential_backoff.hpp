#ifndef gh_detail_exponential_backoff_hpp
#define gh_detail_exponential_backoff_hpp

#include <gh/detail/backoff_strategy.hpp>
#include <stdexcept>

namespace gh {
namespace detail {
class exponential_backoff : public backoff_strategy {
public:
  template <typename min_duration_type, typename max_duration_type>
  exponential_backoff(min_duration_type min_delay, max_duration_type max_delay, int max_attempts)
      : min_delay_(std::chrono::duration_cast<std::chrono::milliseconds>(min_delay))
      , max_delay_(std::chrono::duration_cast<std::chrono::milliseconds>(max_delay))
      , max_attempts_(max_attempts)
      , current_delay_(min_delay)
      , attempt_count_(0) {
    validate_arguments();
  }

  std::chrono::milliseconds record_failure() override;
  std::chrono::milliseconds record_success() override;
  std::chrono::milliseconds current_delay() const override;

private:
  void validate_arguments();
  void too_many_attempts();

private:
  std::chrono::milliseconds min_delay_;
  std::chrono::milliseconds max_delay_;
  int max_attempts_;
  std::chrono::milliseconds current_delay_;
  int attempt_count_;
};
} // namespace detail
} // namespace gh

#endif // gh_detail_exponential_backoff_hpp
