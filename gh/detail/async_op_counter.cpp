#include "gh/detail/async_op_counter.hpp"

namespace gh {
namespace detail {

void async_op_counter::block_until_all_done() {
  std::unique_lock<std::mutex> lock(mu_);
  shutdown_ = true;
  cv_.wait(lock, [this]() { return pending_ == 0; });
}

} // namespace detail
} // namespace gh
