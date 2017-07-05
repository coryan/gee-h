#include "gh/log.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

TEST(log, basic) {

  // Creates a log that appends to a vector ...
  gh::log lg;
  // First what basically amounts to a compilation test
  ASSERT_NO_THROW(GH_LOG_I(error, lg) << "foo" << 4 << 2);
  std::vector<std::pair<gh::severity, std::string>> logs;
  lg.add_sink(
      gh::make_log_sink([&logs](gh::severity sev, std::string&& msg) { logs.emplace_back(sev, std::move(msg)); }));

  using namespace ::testing;
  ASSERT_NO_THROW(
      GH_LOG_I(error, lg) << "testing 123"
                          << " " << 42);
  ASSERT_EQ(logs.size(), 1UL);
  ASSERT_EQ(logs[0].first, gh::severity::error);
  ASSERT_THAT(logs[0].second, StartsWith("[error] testing 123 42"));
}

TEST(log, run_time_disable) {
  // Creates a log that appends to a vector ...
  gh::log lg;
  std::vector<std::pair<gh::severity, std::string>> logs;
  lg.add_sink(
      gh::make_log_sink([&logs](gh::severity sev, std::string&& msg) { logs.emplace_back(sev, std::move(msg)); }));

  using namespace ::testing;
  ASSERT_NO_THROW(
      GH_LOG_I(info, lg) << "testing 123"
                         << " " << 42);
  ASSERT_EQ(logs.size(), 1UL);
  ASSERT_EQ(logs[0].first, gh::severity::info);
  ASSERT_THAT(logs[0].second, StartsWith("[info] testing 123 42"));

  logs.clear();
  int cnt = 0;
  auto f = [&cnt]() {
    ++cnt;
    return 42;
  };
  lg.min_severity(gh::severity::warning);
  ASSERT_NO_THROW(
      GH_LOG_I(info, lg) << "testing 123"
                         << " " << f());
  ASSERT_EQ(logs.size(), 0UL);
  // ... also verify that disabled expressions are not even called ...
  ASSERT_EQ(cnt, 0);
  ASSERT_EQ(f(), 42);
  ASSERT_EQ(cnt, 1);
}

TEST(log, compile_time_disable) {
  // Creates a log that appends to a vector ...
  gh::log lg;
  std::vector<std::pair<gh::severity, std::string>> logs;
  lg.add_sink(
      gh::make_log_sink([&logs](gh::severity sev, std::string&& msg) { logs.emplace_back(sev, std::move(msg)); }));

  using namespace ::testing;
  int cnt = 0;
  auto f = [&cnt]() {
    ++cnt;
    return 42;
  };
  ASSERT_NO_THROW(
        GH_LOG_I(trace, lg) << "testing 123"
                            << " " << f());
  ASSERT_EQ(logs.size(), 0UL);
  ASSERT_EQ(cnt, 0);
  ASSERT_EQ(f(), 42);
}
