#include "gh/log.hpp"

#include <gmock/gmock.h>

/**
 * @test Verify that the GH_LOG_I() and the supporting classes all work in the normal case.
 */
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

/**
 * @test Verify that the GH_LOG_I() and the supporting classes all work when a log level is disabled.
 */
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

/**
 * @test Verify that the GH_LOG_I() and the supporting classes all work when a log level is disabled at compile-time.
 */
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
  // ... use a level that is enabled at runtime, but disabled at compile-time ...
  lg.min_severity(gh::severity::trace);
  ASSERT_NO_THROW(
      GH_LOG_I(debug, lg) << "testing 123"
                          << " " << f());
  ASSERT_EQ(logs.size(), 0UL);
  ASSERT_EQ(cnt, 0);
  ASSERT_EQ(f(), 42);
}

/**
 * @test Verify that the GH_LOG() macro and the supporting singleton work as expected.
 */
TEST(log, instance_basic) {
  gh::log& lg = gh::log::instance();
  std::vector<std::pair<gh::severity, std::string>> logs;
  lg.add_sink(
      gh::make_log_sink([&logs](gh::severity sev, std::string&& msg) { logs.emplace_back(sev, std::move(msg)); }));

  using namespace ::testing;
  ASSERT_NO_THROW(GH_LOG(info) << "testing 123 " << 42);
  ASSERT_EQ(logs.size(), 1UL);
  ASSERT_EQ(logs[0].first, gh::severity::info);
  ASSERT_THAT(logs[0].second, StartsWith("[info] testing 123 42"));
  ASSERT_NO_THROW(lg.clear_sinks());
}

/**
 * @test Verify that the GH_LOG_I() and the supporting classes work with multiple sinks.
 */
TEST(log, multiple_sinks) {
  // Creates a log that appends to a vector ...
  gh::log lg;
  // First what basically amounts to a compilation test
  ASSERT_NO_THROW(GH_LOG_I(error, lg) << "foo" << 4 << 2);
  std::vector<std::pair<gh::severity, std::string>> logs;
  lg.add_sink(
      gh::make_log_sink([&logs](gh::severity sev, std::string&& msg) { logs.emplace_back(sev, std::move(msg)); }));
  lg.add_sink(gh::make_log_sink([&logs](gh::severity sev, std::string&& msg) {
    auto s = std::string("(2) ") + msg;
    logs.emplace_back(sev, std::move(s));
  }));

  using namespace ::testing;
  ASSERT_NO_THROW(
      GH_LOG_I(error, lg) << "testing 123"
                          << " " << 42);
  ASSERT_EQ(logs.size(), 2UL);
  ASSERT_EQ(logs[0].first, gh::severity::error);
  ASSERT_EQ(logs[1].first, gh::severity::error);
  ASSERT_THAT(logs[0].second, StartsWith("[error] testing 123 42"));
  ASSERT_THAT(logs[1].second, StartsWith("(2) [error] testing 123 42"));
}

/**
 * @test Complete code coverage for the gh::logger<true> class.
 */
TEST(log, logger_disabled) {
  // In the normal operation of the gh::logger<true> class neither the get() nor the write_to() member functions are
  // ever used, they are needed to make sure the code compiles.  We want to make sure the functions do behave
  // "correctly", meaning they are no-op's:
  gh::log lg;
  std::vector<std::pair<gh::severity, std::string>> logs;
  lg.add_sink(
      gh::make_log_sink([&logs](gh::severity sev, std::string&& msg) { logs.emplace_back(sev, std::move(msg)); }));
  gh::logger<true> logger(gh::severity::error, __func__, __FILE__, __LINE__, lg);

  ASSERT_EQ((bool)logger, false);
  ASSERT_NO_THROW(logger.get() << "testing " << 123 << std::string(" ") << 42);
  ASSERT_TRUE((std::is_same<decltype(logger.get()), gh::detail::null_stream&>::value));
  ASSERT_NO_THROW(logger.write_to(lg));
  ASSERT_EQ(logs.size(), 0U);
}
