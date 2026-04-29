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
  const std::string input = test_utils::make_bytes({0xE2, 0x82});
  std::u32string output;

  EXPECT_FALSE(utf_utils::utf8_to_utf32(input, output));
}

TEST_F(Utf32DecodeTest, Utf8ToUtf32RejectsOverlongEncoding) {
  const std::string input = test_utils::make_bytes({0xC0, 0xAF});
  std::u32string output;

  EXPECT_FALSE(utf_utils::utf8_to_utf32(input, output));
}

TEST_F(Utf32DecodeTest, Utf8ToUtf32RejectsUtf16SurrogateRange) {
  const std::string input = test_utils::make_bytes({0xED, 0xA0, 0x80});
  std::u32string output;

  EXPECT_FALSE(utf_utils::utf8_to_utf32(input, output));
}

TEST_F(Utf32DecodeTest, Utf8ToUtf32RejectsCodePointsOutsideUnicodeRange) {
  const std::string input = test_utils::make_bytes({0xF4, 0x90, 0x80, 0x80});
  std::u32string output;

  EXPECT_FALSE(utf_utils::utf8_to_utf32(input, output));
}
