/**
 * @file tests/unit/platform/test_utf_utils.cpp
 * @brief Test src/platform/utf_utils.cpp UTF conversion functions.
 */
// test includes
#include "../../tests_common.h"

// standard includes
#include <string>

// local includes
#include <src/platform/utf_utils.h>

class Utf32DecodeTest: public testing::Test {};

TEST_F(Utf32DecodeTest, Utf8ToUtf32WithEmptyString) {
  std::u32string output = U"not empty";

  EXPECT_TRUE(utf_utils::utf8_to_utf32({}, output));
  EXPECT_TRUE(output.empty());
}

TEST_F(Utf32DecodeTest, Utf8ToUtf32WithAsciiAndMultibyteText) {
  const std::string input = "Hello π ñ 👱";
  std::u32string output;

  ASSERT_TRUE(utf_utils::utf8_to_utf32(input, output));
  EXPECT_EQ(output, U"Hello π ñ 👱");
}

TEST_F(Utf32DecodeTest, Utf8ToUtf32RejectsTruncatedSequence) {
  const std::string input("\xE2\x82", 2);
  std::u32string output;

  EXPECT_FALSE(utf_utils::utf8_to_utf32(input, output));
}

TEST_F(Utf32DecodeTest, Utf8ToUtf32RejectsOverlongEncoding) {
  const std::string input("\xC0\xAF", 2);
  std::u32string output;

  EXPECT_FALSE(utf_utils::utf8_to_utf32(input, output));
}

TEST_F(Utf32DecodeTest, Utf8ToUtf32RejectsUtf16SurrogateRange) {
  const std::string input("\xED\xA0\x80", 3);
  std::u32string output;

  EXPECT_FALSE(utf_utils::utf8_to_utf32(input, output));
}

TEST_F(Utf32DecodeTest, Utf8ToUtf32RejectsCodePointsOutsideUnicodeRange) {
  const std::string input("\xF4\x90\x80\x80", 4);
  std::u32string output;

  EXPECT_FALSE(utf_utils::utf8_to_utf32(input, output));
}
