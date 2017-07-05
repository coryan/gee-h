#include "gh/assert_throw.hpp"

#include <sstream>
#include <stdexcept>

namespace gh {

[[noreturn]] void assert_throw_impl(char const* what, char const* function, char const* filename, int lineno) {
  std::ostringstream os;
  os << "assertion failure (" << what << ") was not true in " << function << "@ (" << filename << ":" << lineno << ")";
  throw std::runtime_error(os.str());
}

} // namespace gh