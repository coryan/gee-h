#include "gh/log_sink.hpp"

#include <gtest/gtest.h>

#include <sstream>

/**
 * @test Verify that the gh::make_log_sink works as expected.
 */
TEST(log_sink, basic) {
  std::string value;
  gh::severity sev;
  auto ls = gh::make_log_sink([&value, &sev](gh::severity s, std::string&& m) {
    value = m;
    sev = s;
  });

  ls->log(gh::severity::info, std::string("testing 1 2 3"));
  ASSERT_EQ(sev, gh::severity::info);
  ASSERT_EQ(value, "testing 1 2 3");
}
