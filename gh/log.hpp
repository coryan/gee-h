#ifndef gh_log_hpp
#define gh_log_hpp
/**
 * @file
 *
 * Define macros, types, and functions for logging in Gee-H.
 */
#include <gh/detail/null_stream.hpp>
#include <gh/log_severity.hpp>
#include <gh/log_sink.hpp>

#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

/// Concatenate two pre-processor tokens.
#define GH_PP_CAT(a, b) a##b

/**
 * Create a unique, or mostly-likely unique identifier.
 *
 * In GH_LOG() we need an identifier for the logger, we can easily create a C++ scope to make it work with any name,
 * say "foo".  However the user may want to log another "foo" variable.  We try to make it unlikely that there will
 * be collision by using an identifier that depends on the line number:
 */
#define GH_LOGGER_IDENTIFIER GH_PP_CAT(gh_log_, __LINE__)

/**
 * The main entry point for Gee-H logging facilities.
 *
 * Typically this used only in tests, applications should use GH_LOG().
 */
#define GH_LOG_I(level, sink)                                                                                          \
  for (auto GH_LOGGER_IDENTIFIER = gh::logger<level_compile_time_disabled(gh::severity::level)>(                       \
           gh::severity::level, __func__, __FILE__, __LINE__, sink);                                                   \
       (bool)GH_LOGGER_IDENTIFIER; GH_LOGGER_IDENTIFIER.write_to(sink))                                                \
  GH_LOGGER_IDENTIFIER.get()

/**
 * Declare a logger named @a name.
 */
#define GH_LOGGER_DECL(level, sink, name)                                                                              \
  gh::logger<level_compile_time_disabled(gh::severity::level)> name(                                                   \
      gh::severity::level, __func__, __FILE__, __LINE__, sink)

#ifndef GH_LOG
#define GH_LOG(level) GH_LOG_I(level, gh::log::instance())
#endif // GH_LOG_I

/**
 * The main namespace for the Gee-H library.
 */
namespace gh {
/**
 * The logging framework core.
 *
 * Logging is annoying, one wants to be able to log from any point in the code, but one also wants to decouple the
 * code from the log sinks, and the configuration of the logger, and wants to inject a different logger for testing
 * vs. production.  We could introduce a logger template parameter to each class and function in @c gh, that would
 * make the dependencies obvious, and easy to modify.  Presumably users could change the loggers to fit their needs.
 * However this introduces a lot of complexity to all interfaces.
 *
 * We compromise by using a log class which is a singleton.  My least favorite design pattern, but probably
 * appropriate for this case.
 */
class log {
public:
  /// Normally use @c gh::log::instance(), this is useful in testing.
  log()
      : min_severity_(severity::LOWEST)
      , sinks_() {
  }

  /// Return the singleton instance
  static log& instance();

  /// Add a new sink to the core.
  void add_sink(std::shared_ptr<log_sink> sink);

  /**
   * Remove all the current log sinks from the core.
   *
   * TODO(coryan) we need a way to remove a single sink.
   */
  void clear_sinks();

  /// Write a new log message
  void write(severity sev, std::string&& msg);

  /// Set the minimum severity for the following messages, notice that each sink can implement its own filtering.
  void min_severity(severity sev) {
    std::lock_guard<std::mutex> guard(mu_);
    min_severity_ = sev;
  }

  /// Return the
  severity min_severity() const {
    std::lock_guard<std::mutex> guard(mu_);
    return min_severity_;
  }

private:
  /// A mutex to protext access to the shared state
  mutable std::mutex mu_;
  /// The minimum run-time severity
  severity min_severity_;
  /// The list of sinks
  std::vector<std::shared_ptr<log_sink>> sinks_;

  /// The single instance used in the program ...
  static std::unique_ptr<log> singleton_;
};

/**
 * A compile-time disabled log message container.
 *
 * The generic version creates a message container that contains nothing.  All streaming operations are
 * no-op's.  See @c detail::null_logger for more information.
 *
 * @tparam enabled if false, use a compile-time-disabled logger, which does not log anything.
 */
template <bool disabled>
class logger {
public:
  logger(severity s, char const* func, char const* file, int lineno, log& sink) {
  }

  explicit operator bool() const {
    return false;
  }

  /// Get the gh::detail::null_logger to consume the iostream expression.
  detail::null_stream& get() {
    return os;
  }

  void write_to(log& sink) {
  }

private:
  detail::null_stream os;
};

/**
 * A simple log message container.
 *
 * This specialization creates a message container that logs to a std::ostringstream and then sends that stream to
 * the
 * configured sinks, if any.
 */
template <>
class logger<false> {
public:
  logger(severity s, char const* func, char const* file, int lineno, log& sink);

  explicit operator bool() const {
    return not closed;
  }

  /// Get the std::ostream where the message will be formatted.
  std::ostream& get() {
    return os;
  }

  /// Save the message to the log sink
  void write_to(gh::log& sink);

private:
  std::ostringstream os;
  severity sev;
  std::string function;
  std::string filename;
  int lineno;
  bool closed;
};

/**
 * Determine if a given severity level is disabled at compile-time.
 *
 * @param lvl the severity level to check.
 * @returns true if @a lvl is disabled at compile-time.
 */
bool constexpr level_compile_time_disabled(severity lvl) {
  return lvl < gh::severity::GH_MIN_SEVERITY;
}
} // namespace gh

#endif // gh_log_hpp
