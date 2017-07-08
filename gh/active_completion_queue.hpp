#ifndef gh_active_completion_queue_hpp
#define gh_active_completion_queue_hpp

#include <gh/completion_queue.hpp>
#include <thread>

namespace gh {

/**
 * Create a command_queue with an associated thread running its loop.
 *
 * This deals with the awful order of construction problems.  It holds
 * a completion queue and a thread running the event loop for the
 * queue.  On destruction it shutdown the completion queue first, and
 * then joins the thread.
 */
class active_completion_queue {
public:
  /// Constructor, creates new completion queue and thread.
  active_completion_queue()
      : queue_(std::make_shared<completion_queue<>>())
      , thread_([q = queue()]() { q->run(); })
      , join_(&thread_)
      , shutdown_(queue()) {
  }

  /// Constructor from existing queue and thread.  Assumes thread
  /// calls q->run().
  active_completion_queue(
      std::shared_ptr<completion_queue<>> q, std::thread&& t)
      : queue_(std::move(q))
      , thread_(std::move(t))
      , join_(&thread_)
      , shutdown_(queue()) {
  }

  active_completion_queue(active_completion_queue&& rhs)
      : queue_(std::move(rhs.queue_))
      , thread_(std::move(rhs.thread_))
      , join_(&thread_)
      , shutdown_(queue()) {
    rhs.shutdown_.release();
  }
  active_completion_queue& operator=(active_completion_queue&& rhs) {
    active_completion_queue tmp(std::move(*this));
    queue_ = std::move(rhs.queue_);
    thread_ = std::move(rhs.thread_);
    shutdown_.queue = queue();
    return *this;
  }
  active_completion_queue(active_completion_queue const&) = delete;
  active_completion_queue& operator=(active_completion_queue const&) = delete;

  ~active_completion_queue();

  operator bool () const {
    return (bool)queue_;
  }

  completion_queue<>& cq() {
    return *queue_;
  }

private:
  /// A helper for the lambdas in the constructor
  std::shared_ptr<completion_queue<>> queue() {
    return queue_;
  }

  /// Shutdown a completion queue
  struct defer_shutdown {
    explicit defer_shutdown(std::shared_ptr<completion_queue<>> q)
        : queue(q) {
    }
    ~defer_shutdown();
    void release() {
      queue.reset();
    }
    std::shared_ptr<completion_queue<>> queue;
  };

  /// Join a thread
  struct defer_join {
    explicit defer_join(std::thread* t)
        : thread(t) {
    }
    ~defer_join();
    void release() {
      thread = nullptr;
    }
    std::thread* thread;
  };

private:
  std::shared_ptr<completion_queue<>> queue_;
  std::thread thread_;
  defer_join join_;
  defer_shutdown shutdown_;
};

} // namespace gh

#endif // gh_active_completion_queue_hpp
