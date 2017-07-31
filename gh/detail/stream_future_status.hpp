#ifndef gh_detail_stream_future_status_hpp
#define gh_detail_stream_future_status_hpp

#include <future>
#include <iostream>

// Technically reopening the std:: namespace is a violation of the standard, I think that is Okay in tests.
namespace std {
/// Define a streaming operator to use in tests.
inline std::ostream& operator<<(std::ostream& os, std::future_status x) {
  switch(x) {
  case std::future_status::timeout:
    return os << "[timeout]";
  case std::future_status::ready:
    return os << "[ready]";
  case std::future_status::deferred:
    return os << "[deferred]";
  }
  throw std::runtime_error("unknown std::future_status");
}
} // namespace std

#endif // gh_detail_stream_future_status_hpp
