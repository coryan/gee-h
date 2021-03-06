/**
 * @file
 *
 * Helper functions to handle errors reported by gRPC++
 */
#ifndef gh_detail_grpc_errors_hpp
#define gh_detail_grpc_errors_hpp

#include <gh/detail/append_annotations.hpp>

#include <google/protobuf/message.h>
#include <grpc++/grpc++.h>
#include <sstream>

namespace gh {
namespace detail {

/**
 * Create an exception given a gRPC error.
 *
 * @param where a string to let the user know where the error took place.
 * @param status the status to format
 * @param a a list of additional annotations to append (using operator<<) to the end of the exception what() message.
 * @returns a std::runtime_error with the contents of the status error and any annotations.
 */
template <typename Location, typename... Annotations>
std::runtime_error make_exception(grpc::Status const& status, Location const& where, Annotations&&... a) {
  std::ostringstream os;
  os << where << " grpc error: " << status.error_message() << " [" << status.error_code() << "]";
  detail::append_annotations(os, std::forward<Annotations>(a)...);
  return std::runtime_error(os.str());
}

/**
 * Raise an exception if the grpc::Status indicates there was an error.
 *
 * @param where a string to let the user know where the error took place.
 * @param status the status to format
 * @param a a list of additional annotations to append (using operator<<) to the end of the exception what() message.
 * @throws a std::exception if the @a status.ok() is false.
 */
template <typename Location, typename... Annotations>
void check_grpc_status(grpc::Status const& status, Location const& where, Annotations&&... a) {
  if (status.ok()) {
    return;
  }
  throw make_exception(status, where, std::forward<Annotations>(a)...);
}

/**
 * Print a protobuf on a std::ostream.
 *
 * Uses google::protobuf::TextFormat::PrintToString to print a protobuf.  Typically one would use is as in:
 *
 * @code
 * blah::ProtoName const& proto = ...;
 * std::ostream& os = ...;
 *
 * os << "foo " << 1 << print_to_stream(proto) << " blah";
 * @endcode
 */
struct print_to_stream {
  explicit print_to_stream(google::protobuf::Message const& m)
      : msg(m) {
  }

  google::protobuf::Message const& msg;
};

/// Streaming operator
std::ostream& operator<<(std::ostream& os, print_to_stream const& x);

} // namespace detail
} // namespace gh

#endif // gh_detail_grpc_errors_hpp
