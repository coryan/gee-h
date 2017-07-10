#ifndef gh_detail_append_annotations_hpp
#define gh_detail_append_annotations_hpp

#include <utility>

namespace gh {
namespace detail {

/**
 * Append an (empty) list of annotations to a stream.
 *
 * @tparam Stream the type of the stream, typically std::ostream.
 * @param os the value of the stream.
 */
template<typename Stream>
inline void append_annotations(Stream& os) {
}

/**
 * Append a single annotation to a stream.
 *
 * @tparam Stream the type of the stream, typically std::ostream.
 * @tparam T the type of the annotation to append.
 * @param os the value of the stream.
 * @param t the value of the annotation.
 */
template <typename Stream, typename T>
inline void append_annotations(Stream& os, T&& t) {
  os << t;
}

/**
 * Append a list of annotations to a stream.
 *
 * @tparam Stream the type of the stream, typically std::ostream.
 * @tparam H the type of the first annotation in the list.
 * @tparam Tail the type of the remaining annotations in the list.
 * @param os the value of the stream.
 * @param h the value of the first annotation.
 * @param t the value of the remaining annotations.
 */
template <typename Stream, typename H, typename... Tail>
inline void append_annotations(Stream& os, H&& h, Tail&&... t) {
  append_annotations(os << h, std::forward<Tail>(t)...);
}

} // namespace detail
} // namespace gh

#endif // gh_detail_append_annotations_hpp
