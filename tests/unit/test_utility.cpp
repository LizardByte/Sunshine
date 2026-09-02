/**
 * @file tests/unit/test_utility.cpp
 * @brief Tests for general utility helpers.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// local includes
#include <src/utility.h>

struct HexConversionTest: testing::TestWithParam<std::tuple<std::uint32_t, bool, std::string>> {};

TEST_P(HexConversionTest, ToStringProducesExpectedHex) {
  const auto &[input, rev, expected] = GetParam();
  const auto hex = util::hex(input, rev);
  EXPECT_EQ(hex.to_string(), expected);
}

INSTANTIATE_TEST_SUITE_P(
  UtilityTests,
  HexConversionTest,
  testing::Values(
    std::make_tuple(0x00000000, false, "00000000"),
    std::make_tuple(0xDEADBEEF, false, "DEADBEEF"),
    std::make_tuple(0x12345678, false, "12345678"),
    std::make_tuple(0x000000FF, false, "000000FF"),
    std::make_tuple(0xDEADBEEF, true, "EFBEADDE"),
    std::make_tuple(0x12345678, true, "78563412")
  )
);

struct HexUint8Test: testing::TestWithParam<std::tuple<std::uint8_t, bool, std::string>> {};

TEST_P(HexUint8Test, SingleByteHex) {
  const auto &[input, rev, expected] = GetParam();
  const auto hex = util::hex(input, rev);
  EXPECT_EQ(hex.to_string(), expected);
}

INSTANTIATE_TEST_SUITE_P(
  UtilityTests,
  HexUint8Test,
  testing::Values(
    std::make_tuple(std::uint8_t {0x00}, false, "00"),
    std::make_tuple(std::uint8_t {0xFF}, false, "FF"),
    std::make_tuple(std::uint8_t {0xAB}, false, "AB"),
    std::make_tuple(std::uint8_t {0x0F}, false, "0F")
  )
);

TEST(UtilityHexVecTests, PreservesByteOrderWhenRevIsTrue) {
  const std::vector<std::uint8_t> data {0xDE, 0xAD, 0xBE, 0xEF};
  const std::string result = util::hex_vec(data, true);
  EXPECT_EQ(result, "DEADBEEF");
}

TEST(UtilityHexVecTests, ReversesByteOrderWhenRevIsFalse) {
  const std::vector<std::uint8_t> data {0xDE, 0xAD, 0xBE, 0xEF};
  const std::string result = util::hex_vec(data, false);
  EXPECT_EQ(result, "EFBEADDE");
}

TEST(UtilityHexVecTests, EmptyVector) {
  const std::vector<std::uint8_t> data;
  EXPECT_TRUE(util::hex_vec(data, true).empty());
  EXPECT_TRUE(util::hex_vec(data, false).empty());
}

TEST(UtilityHexVecTests, SingleByte) {
  const std::vector<std::uint8_t> data {0x42};
  const std::string result = util::hex_vec(data, true);
  EXPECT_EQ(result, "42");
}

TEST(UtilityFromHexTests, ParseHexToUint32) {
  const auto result = util::from_hex<std::uint32_t>("DEADBEEF", true);
  EXPECT_EQ(result, util::endian::big(std::uint32_t {0xDEADBEEF}));
}

TEST(UtilityFromHexTests, ParseHexToUint32NonReversed) {
  const auto result = util::from_hex<std::uint32_t>("DEADBEEF", false);
  EXPECT_EQ(result, util::endian::little(std::uint32_t {0xDEADBEEF}));
}

TEST(UtilityFromHexTests, ParseHexLowercase) {
  const auto result = util::from_hex<std::uint32_t>("deadbeef", true);
  EXPECT_EQ(result, util::endian::big(std::uint32_t {0xDEADBEEF}));
}

TEST(UtilityFromHexTests, ParseHexToUint16) {
  const auto result = util::from_hex<std::uint16_t>("ABCD", true);
  EXPECT_EQ(result, util::endian::big(std::uint16_t {0xABCD}));
}

TEST(UtilityFromHexTests, ParseHexWithSeparators) {
  // from_hex skips non-hex characters
  const auto result = util::from_hex<std::uint32_t>("DE:AD:BE:EF", true);
  EXPECT_EQ(result, util::endian::big(std::uint32_t {0xDEADBEEF}));
}

TEST(UtilityFromHexVecTests, ParseHexStringToBytes) {
  const std::string result = util::from_hex_vec("DEADBEEF", true);
  ASSERT_EQ(result.size(), 4U);
  EXPECT_EQ(static_cast<std::uint8_t>(result[0]), 0xDE);
  EXPECT_EQ(static_cast<std::uint8_t>(result[1]), 0xAD);
  EXPECT_EQ(static_cast<std::uint8_t>(result[2]), 0xBE);
  EXPECT_EQ(static_cast<std::uint8_t>(result[3]), 0xEF);
}

TEST(UtilityFromHexVecTests, ParseHexStringNonReversed) {
  const std::string result = util::from_hex_vec("DEADBEEF", false);
  ASSERT_EQ(result.size(), 4U);
  EXPECT_EQ(static_cast<std::uint8_t>(result[0]), 0xEF);
  EXPECT_EQ(static_cast<std::uint8_t>(result[1]), 0xBE);
  EXPECT_EQ(static_cast<std::uint8_t>(result[2]), 0xAD);
  EXPECT_EQ(static_cast<std::uint8_t>(result[3]), 0xDE);
}

struct FromViewTest: testing::TestWithParam<std::tuple<std::string_view, std::int64_t>> {};

TEST_P(FromViewTest, ParsesCorrectly) {
  const auto &[input, expected] = GetParam();
  EXPECT_EQ(util::from_view(input), expected);
}

INSTANTIATE_TEST_SUITE_P(
  UtilityTests,
  FromViewTest,
  testing::Values(
    std::make_tuple("0", std::int64_t {0}),
    std::make_tuple("1", std::int64_t {1}),
    std::make_tuple("42", std::int64_t {42}),
    std::make_tuple("12345", std::int64_t {12345}),
    std::make_tuple("-1", std::int64_t {-1}),
    std::make_tuple("-999", std::int64_t {-999}),
    std::make_tuple("2147483647", std::int64_t {2147483647}),
    std::make_tuple("-2147483648", std::int64_t {-2147483648LL})
  )
);

TEST(UtilityFromViewTests, EmptyStringReturnsZero) {
  EXPECT_EQ(util::from_view(""), 0);
}

TEST(UtilityEitherTests, HasLeftWhenConstructedWithLeft) {
  util::Either<int, std::string> either {std::in_place_type<int>, 42};
  EXPECT_TRUE(either.has_left());
  EXPECT_FALSE(either.has_right());
  EXPECT_EQ(either.left(), 42);
}

TEST(UtilityEitherTests, HasRightWhenConstructedWithRight) {
  util::Either<int, std::string> either {std::in_place_type<std::string>, "hello"};
  EXPECT_FALSE(either.has_left());
  EXPECT_TRUE(either.has_right());
  EXPECT_EQ(either.right(), "hello");
}

TEST(UtilityEitherTests, DefaultConstructedHasNeither) {
  util::Either<int, std::string> either;
  EXPECT_FALSE(either.has_left());
  EXPECT_FALSE(either.has_right());
}

TEST(UtilityFailGuardTests, ExecutesOnDestruction) {
  bool executed = false;
  {
    auto guard = util::fail_guard([&executed]() {
      executed = true;
    });
  }
  EXPECT_TRUE(executed);
}

TEST(UtilityFailGuardTests, DoesNotExecuteWhenDisabled) {
  bool executed = false;
  {
    auto guard = util::fail_guard([&executed]() {
      executed = true;
    });
    guard.disable();
  }
  EXPECT_FALSE(executed);
}

TEST(UtilityFailGuardTests, MoveDoesNotDoubleExecute) {
  int count = 0;
  {
    auto guard1 = util::fail_guard([&count]() {
      count++;
    });
    auto guard2 = std::move(guard1);
  }
  EXPECT_EQ(count, 1);
}

TEST(UtilityBufferTests, ConstructWithSize) {
  util::buffer_t<int> buf(10);
  EXPECT_EQ(buf.size(), 10U);
}

TEST(UtilityBufferTests, ConstructWithSizeAndValue) {
  util::buffer_t buf(5, 42);
  for (const auto value : buf) {
    EXPECT_EQ(value, 42);
  }
}

TEST(UtilityBufferTests, DefaultConstructIsEmpty) {
  util::buffer_t<int> buf;
  EXPECT_EQ(buf.size(), 0U);
}

TEST(UtilityBufferTests, IndexAccess) {
  util::buffer_t<int> buf(3);
  buf[0] = 10;
  buf[1] = 20;
  buf[2] = 30;
  EXPECT_EQ(buf[0], 10);
  EXPECT_EQ(buf[1], 20);
  EXPECT_EQ(buf[2], 30);
}

TEST(UtilityBufferTests, BeginEndIterators) {
  util::buffer_t buf(3, 7);
  int sum = 0;
  for (const auto value : buf) {
    sum += value;
  }
  EXPECT_EQ(sum, 21);
}

TEST(UtilityBufferTests, MoveConstruction) {
  util::buffer_t buf1(3, 99);
  util::buffer_t<int> buf2(std::move(buf1));
  EXPECT_EQ(buf2.size(), 3U);
  EXPECT_EQ(buf2[0], 99);
}

TEST(UtilityBufferTests, CopyConstruction) {
  util::buffer_t buf1(3, 55);
  util::buffer_t<int> buf2(buf1);
  EXPECT_EQ(buf2.size(), 3U);
  EXPECT_EQ(buf2[0], 55);
  EXPECT_EQ(buf1.size(), 3U);
  EXPECT_EQ(buf1[0], 55);
}

TEST(UtilityAppendStructTests, AppendsDataCorrectly) {
  struct test_struct_t {
    std::uint8_t a;
    std::uint8_t b;
    std::uint8_t c;
  };

  const test_struct_t s {0xAA, 0xBB, 0xCC};
  std::vector<std::uint8_t> buf {0x11};
  util::append_struct(buf, s);

  const std::vector<std::uint8_t> expected {0x11, 0xAA, 0xBB, 0xCC};
  EXPECT_EQ(buf, expected);
}

TEST(UtilityEndianTests, BigEndianConversion) {
  constexpr std::uint32_t val = 0x01020304;
  const auto big = util::endian::big(val);

  if constexpr (util::endian::endianness<>::little) {
    EXPECT_EQ(big, 0x04030201);
  } else {
    EXPECT_EQ(big, val);
  }
}

TEST(UtilityEndianTests, LittleEndianConversion) {
  constexpr std::uint32_t val = 0x01020304;
  const auto little_val = util::endian::little(val);

  if constexpr (util::endian::endianness<>::little) {
    EXPECT_EQ(little_val, val);
  } else {
    EXPECT_EQ(little_val, 0x04030201);
  }
}

TEST(UtilityLogHexTests, FormatsWithPrefix) {
  constexpr std::uint8_t val = 0xAB;
  const std::string result = util::log_hex(val);
  EXPECT_EQ(result, "0xAB");
}

TEST(UtilityLogHexTests, FormatsZero) {
  constexpr std::uint8_t val = 0x00;
  const std::string result = util::log_hex(val);
  EXPECT_EQ(result, "0x00");
}

TEST(UtilityLogHexTests, Formats16Bit) {
  constexpr std::uint16_t val = 0x1234;
  const std::string result = util::log_hex(val);
  EXPECT_EQ(result, "0x1234");
}
