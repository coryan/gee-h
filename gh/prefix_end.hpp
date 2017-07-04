/**
 * @file
 *
 * Helper function to compute the end of a prefix range.
 */
#ifndef gh_prefix_end_hpp
#define gh_prefix_end_hpp

#include <string>

namespace gh {

/**
 * Returns the end of a prefix range.
 *
 * In etcd all searches are expressed as either "give me this key" or
 * "give me all the keys between A and B".  In leader election we want
 * to say "give me all the keys that start with A".  Fortunately that
 * is equivalent to "give me all the keys between A and A + 1-bit".
 * This function computes "A + 1-bit".
 *
 * @param prefix the beginning of the prefix range
 * @returns the end of the prefix range
 */
std::string prefix_end(std::string const& prefix);

} // namespace jb

#endif // gh_prefix_end_hpp
