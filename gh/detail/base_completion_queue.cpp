#include "gh/detail/base_completion_queue.hpp"
#include <gh/assert_throw.hpp>
#include <gh/log.hpp>

namespace gh {
namespace detail {

std::chrono::milliseconds constexpr base_completion_queue::loop_timeout;

base_completion_queue::base_completion_queue()
    : mu_()
    , pending_ops_()
    , queue_()
    , shutdown_(false) {
}

base_completion_queue::~base_completion_queue() {
  if (not pending_ops_.empty()) {
    // At this point there is not much to do, we could try to call the operations and tell them they are cancelled,
    // but that is risky, hey might be pointing to objects already deleted.  We print the best debug message we can,
    // and just continue on our way to whatever fate awits the application.
    // TODO() - consider calling std::terminate(), raising an exception, or making the decision here a policy.
    std::ostringstream os;
    for (auto const& op : pending_ops_) {
      os << op.second->name << "\n";
    }
    GH_LOG(error) << " completion queue deleted while holding " << pending_ops_.size()
                  << " pending operations: " << os.str();
  }
}

void base_completion_queue::run() {
  void* tag = nullptr;
  bool ok = false;
  while (not shutdown_.load()) {
    auto deadline = std::chrono::system_clock::now() + loop_timeout;

    auto status = queue_.AsyncNext(&tag, &ok, deadline);
    if (status == grpc::CompletionQueue::SHUTDOWN) {
      GH_LOG(trace) << "shutdown, exit loop";
      break;
    }
    if (status == grpc::CompletionQueue::TIMEOUT) {
      GH_LOG(trace) << "timeout, continue loop";
      continue;
    }
    if (tag == nullptr) {
      // TODO() - I think tag == nullptr should never happen,
      // consider using JB_ASSERT()
      continue;
    }

    // ... try to find the operation in our list of known operations ...
    std::shared_ptr<base_async_op> op = unregister_op(tag);
    if (not op) {
      GH_LOG(error) << "Unknown tag reported in asynchronous operation: " << std::hex << std::intptr_t(tag);
      continue;
    }
    // ... it was there, now it is removed, and the lock is safely
    // unreleased, call it ...
    op->callback(*op, ok);
  }
}

void base_completion_queue::shutdown() {
  GH_LOG(trace) << "shutting down queue";
  shutdown_.store(true);
  queue_.Shutdown();
}

void* base_completion_queue::register_op(char const* where, std::shared_ptr<base_async_op> op) {
  void* tag = static_cast<void*>(op.get());
  auto key = reinterpret_cast<std::intptr_t>(tag);
  std::lock_guard<std::mutex> lock(mu_);
  auto r = pending_ops_.emplace(key, op);
  GH_ASSERT_THROW(r.second != false);
  return tag;
}

std::shared_ptr<base_async_op> base_completion_queue::unregister_op(void* tag) {
  std::lock_guard<std::mutex> lock(mu_);
  pending_ops_type::iterator i = pending_ops_.find(reinterpret_cast<std::intptr_t>(tag));
  if (i != pending_ops_.end()) {
    auto op = i->second;
    pending_ops_.erase(i);
    return op;
  }
  return std::shared_ptr<base_async_op>();
}

} // namespace detail
} // namespace gh
