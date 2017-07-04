#ifndef gh_log_severity_hpp
#define gh_log_severity_hpp
/**
 * @file
 *
 * Define the log severity values and some macros associated with them.
 */

#include <iosfwd>

#ifndef GH_MIN_SEVERITY
/**
 * All log messages below this level are disabled at compile time.
 *
 * Gee-H disable some messages at compile-time, reducing them to very cheap no-op's that the optimizer should be able
 * to eliminate.  That allows developers to write lots of logging to debug problems, but run efficiently in
 * production code.
 */
#define GH_MIN_SEVERITY info
#endif // GH_MIN_SEVERITY

namespace gh {
/**
 * Define the severity levels for Gee-H logging.
 *
 * These are modelled after the severity level in syslog(1) and many derived tools.
 */
enum class severity {
  /// Use this level for messages that indicate the code is entering and leaving functions.
  trace,
  /// Use this level for debug messages that should not be present in production.
  debug,
  /// Informational messages, such as normal progress.
  info,
  /// Informational messages, such as unusual, but expected conditions.
  notice,
  /// An indication of problems, users may need to take action.
  warning,
  /// An error has been detected.  Do not use for normal conditions, such as remote servers disconnecting.
  error,
  /// The system is in a critical state, such as running out of local resources.
  critical,
  /// The system is at risk of immediate failure.
  alert,
  /// The system is about to crash or terminate.
  fatal,
  /// The highest possible severity level.
  HIGHEST = int(fatal),
  /// The lowest possible severity level.
  LOWEST = int(trace),
  /// The lowest level that is enabled at compile-time.
  LOWEST_ENABLED = int(GH_MIN_SEVERITY),
};

/// Streaming operator, writes a human readable representation.
std::ostream& operator<<(std::ostream& os, severity x);

} // namespace gh

#endif // gh_log_severity_hpp
