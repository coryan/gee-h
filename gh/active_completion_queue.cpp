#include "gh/active_completion_queue.hpp"
#include <gh/log.hpp>

namespace gh {

active_completion_queue::~active_completion_queue() {
  GH_LOG(trace) << "delete active completion queue";
}

active_completion_queue::defer_shutdown::~defer_shutdown() {
  if (queue) {
    GH_LOG(trace) << "shutdown active completion queue";
    queue->shutdown();
  }
}

active_completion_queue::defer_join::~defer_join() {
  if (thread->joinable()) {
    GH_LOG(trace) << "join active completion queue";
    thread->join();
  }
}

} // namespace jb
