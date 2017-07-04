#include "gh/log_severity.hpp"

#include <iostream>

namespace gh {

std::ostream& operator<<(std::ostream& os, severity x) {
  char const* names[] = {
      "trace", "debug", "info", "notice", "warning", "error", "critical", "alert", "fatal",
  };
  return os << names[int(x)];
};

} // namespace gh
