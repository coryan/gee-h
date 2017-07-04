#ifndef gh_log_sink_hpp
#define gh_log_sink_hpp

#include <gh/log_severity.hpp>

#include <memory>
#include <string>
#include <utility>

namespace gh {

/**
 * A destination for logging messages.
 *
 * Applications can configure the destination for logging messages by setting one more more instances of gh::log_sink
 * in the global logger.
 */
class log_sink {
public:
  virtual ~log_sink() {}

  /**
   * Log the given message to the sink.
   *
   * @param sev the severity of the message.
   * @param message the message value.
   */
  virtual void log(severity sev, std::string&& message) = 0;
};

/**
 * An adaptor that converts any Functor into a @c gh::log_sink.
 *
 * Often it is easier for the application developer to declare a long sink in-situ using a lambda or another functor
 * type.  This class makes it easy to adapt such objects to the @c gh::log_sink interface.
 *
 * @tparam Functor the type of the functor to adapt.
 */
template<typename Functor>
class log_to_functor : public log_sink {
public:
  log_to_functor(Functor &&f)
      : functor(f) {
  }
  log_to_functor(Functor const& f)
      : log_to_functor(std::move(f)) {
  }

  /// Forward logging to the functor.
  virtual void log(severity sev, std::string&& message) override {
    functor(sev, std::move(message));
  }

private:
  Functor functor;
};

/**
 * Create a @c gh::log_sink shared pointer from a functor.
 *
 * @tparam Functor the type of the functor object @a f.
 * @param f the functor object to forward calls to.
 * @return a log_sink that forwards log() calls to the given functor @a f.
 */
template<typename Functor>
std::shared_ptr<log_sink> make_log_sink(Functor&& f) {
  return std::shared_ptr<log_sink>(new log_to_functor<Functor>(std::move(f)));
}

} // namespace gh

#endif // gh_log_sink_hpp
