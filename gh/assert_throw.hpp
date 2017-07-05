#ifndef gh_assert_throw_hpp
#define gh_assert_throw_hpp
/**
 * @file
 *
 * Define a useful macro to check assertions at runtime.
 */

#ifndef GH_ASSERT_THROW
/**
 * Check the predicate @a P and if false raises an exception describing the problem.
 */
#define GH_ASSERT_THROW(P)                                                                                             \
  do {                                                                                                                 \
    if (not(P)) {                                                                                                      \
      gh::assert_throw_impl(#P, __func__, __FILE__, __LINE__);                                                         \
    }                                                                                                                  \
  } while (false)
#endif // GH_ASSERT_THROW

namespace gh {

/**
 * Implement the @c GH_ASSERT_THROW macro out-of-line.
 *
 * @param what the text description of the predicate
 * @param function the location (function) where the predicate was asserted.
 * @param filename the location (source code filename) where the predicate was asserted.
 * @param lineno the location (line number) where the predicate was asserted.
 */
[[noreturn]] void assert_throw_impl(char const* what, char const* function, char const* filename, int lineno);
} // namespace gh

#endif // gh_assert_throw_hpp
