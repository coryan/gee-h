#include "gh/log.hpp"

namespace {
std::once_flag log_initialized;
} // anonymous namespace

namespace gh {

std::unique_ptr<log> log::singleton_;

log& log::instance() {
  std::call_once(log_initialized, []() { singleton_.reset(new log); });
  return *singleton_;
}

void log::add_sink(std::shared_ptr<log_sink> sink) {
  std::lock_guard<std::mutex> guard(mu_);
  sinks_.push_back(std::move(sink));
}

void log::clear_sinks() {
  std::lock_guard<std::mutex> guard(mu_);
  sinks_.clear();
}

void log::write(severity sev, std::string&& msg) {
  if (sinks_.empty() or sev < min_severity()) {
    return;
  }
  // Special case, very common and avoid copying the message ...
  if (sinks_.size() == 1) {
    sinks_[0]->log(sev, std::move(msg));
    return;
  }
  for (auto s : sinks_) {
    std::string copy(msg);
    s->log(sev, std::move(copy));
  }
}

logger<false>::logger(severity s, char const* func, char const* file, int l, log& sink)
    : os()
    , sev(s)
    , closed(sev < sink.min_severity()) {
  if (closed) {
    return;
  }
  function = func;
  filename = file;
  lineno = l;
  os << "[" << sev << "] ";
}

void logger<false>::write_to(log& sink) {
  closed = true;
  os << " in " << function << "(" << filename << ":" << lineno << ")";
  sink.write(sev, os.str());
}

} // namespace gh
