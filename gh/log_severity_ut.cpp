#include "gh/log_severity.hpp"

#include <gtest/gtest.h>

TEST(log_severity, base) {
  ASSERT_LT(gh::severity::LOWEST, gh::severity::HIGHEST);

  using s = gh::severity;
  std::ostringstream os;
  os << s::trace << " " << s::debug << " " << s::info << " " << s::notice << " " << s::warning << " " << s::error << " "
     << s::critical << " " << s::alert << " " << s::fatal;
  ASSERT_EQ(os.str(), "trace debug info notice warning error critical alert fatal");
}
