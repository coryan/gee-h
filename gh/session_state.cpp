#include "gh/session_state.hpp"

namespace gh {

std::ostream& operator<<(std::ostream& os, session_state x) {
  char const* values[] = {"constructing", "connecting", "connected"};
  return os << values[int(x)];
}

} // namespace gh
