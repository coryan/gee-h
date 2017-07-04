#ifndef gh_detail_null_stream_hpp
#define gh_detail_null_stream_hpp

namespace gh {
namespace detail {
/**
 * Implements operator<< for all types, without any effect.
 *
 * It is desirable to disable at compile-time tracing, debugging, and other low severity messages.  The Gee-H logging
 * adaptors return an object of this class when the particular log-line is disabled at compile-time.
 * This class implements the streaming operator<< for
 */
struct null_stream {

  /// Generic do-nothing streaming operator
  template<typename T>
  null_stream& operator<<(T const& ) {
    return *this;
  }

  /// Do-nothing streaming operator for string literals.
  null_stream& operator<<(char const* ) {
    return *this;
  }
};

} // namespace detail
} // namespace gh

#endif // gh_detail_null_stream_hpp
