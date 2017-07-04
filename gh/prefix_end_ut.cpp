#include "gh/prefix_end.hpp"

#include <gmock/gmock.h>

/**
 * @test Verify that gh::prefix_end works as expected.
 */
TEST(prefix_end, basic) {
  ASSERT_EQ(u'/' + 1, u'0');
  ASSERT_EQ(gh::prefix_end("foo/"), std::string("foo0"));
  std::string actual = gh::prefix_end(u8"\xFF\xFF");
  char buf0[] = u8"\x00\x00\x01";
  std::string expected(buf0, buf0 + sizeof(buf0) - 1);

  using namespace ::testing;
  ASSERT_THAT(actual, ElementsAre(u'\x00', u'\x00', u'\x01'));

  actual = gh::prefix_end(u8"ABC\xFF");
  char buf1[] = u8"ABD\x00";
  expected.assign(buf1, buf1 + sizeof(buf1) - 1);
  ASSERT_THAT(actual, ElementsAre(u'A', u'B', u'D', u'\x00'));

}
