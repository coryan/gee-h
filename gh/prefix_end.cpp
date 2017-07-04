#include "gh/prefix_end.hpp"

namespace gh {

std::string prefix_end(std::string const& prefix) {
  // ... iterate in reverse order, find the first element (in
  // iteration order) that is not 0xFF, increment it, now we have a
  // key that is one bit higher than the prefix ...
  std::string range_end = prefix;
  bool needs_append = true;
  for (auto i = range_end.rbegin(); i != range_end.rend(); ++i) {
    if (std::uint8_t(*i) == 0xFF) {
      *i = 0x00;
      continue;
    }
    *i = *i + 1;
    needs_append = false;
    break;
  }
  // ... if all elements were 0xFF the loop converted them to 0x00 and
  // we just need to add a 0x01 at the end ...
  if (needs_append) {
    range_end.push_back(0x01);
  }
  return range_end;
}

} // namespace gh
