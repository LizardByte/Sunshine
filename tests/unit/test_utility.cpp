/**
 * @file tests/unit/test_utility.cpp
 * @brief Test src/utility.h.
 */
#include "../tests_common.h"

#include <src/utility.h>

// ========== Hex conversion tests ==========

struct HexConversionTest: testing::TestWithParam<std::tuple<uint32_t, bool, std::string>> {};

TEST_P(HexConversionTest, ToStringProducesExpectedHex) {
  auto [input, rev, expected] = GetParam();
  auto hex = util::hex(input, rev);
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

struct HexUint8Test: testing::TestWithParam<std::tuple<uint8_t, bool, std::string>> {};

TEST_P(HexUint8Test, SingleByteHex) {
  auto [input, rev, expected] = GetParam();
  auto hex = util::hex(input, rev);
  EXPECT_EQ(hex.to_string(), expected);
}

INSTANTIATE_TEST_SUITE_P(
  UtilityTests,
  HexUint8Test,
  testing::Values(
    std::make_tuple(uint8_t {0x00}, false, "00"),
    std::make_tuple(uint8_t {0xFF}, false, "FF"),
    std::make_tuple(uint8_t {0xAB}, false, "AB"),
    std::make_tuple(uint8_t {0x0F}, false, "0F")
  )
);

// ========== hex_vec tests ==========

TEST(UtilityHexVecTests, VectorToHexStringReversed) {
  std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
  std::string result = util::hex_vec(data, true);
  EXPECT_EQ(result, "DEADBEEF");
}

TEST(UtilityHexVecTests, VectorToHexStringNonReversed) {
  std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
  std::string result = util::hex_vec(data, false);
  EXPECT_EQ(result, "EFBEADDE");
}

TEST(UtilityHexVecTests, EmptyVector) {
  std::vector<uint8_t> data = {};
  std::string result = util::hex_vec(data, true);
  EXPECT_EQ(result, "");
}

TEST(UtilityHexVecTests, SingleByte) {
  std::vector<uint8_t> data = {0x42};
  std::string result = util::hex_vec(data, true);
  EXPECT_EQ(result, "42");
}


// ========== from_hex tests ==========

TEST(UtilityFromHexTests, ParseHexToUint32) {
  auto result = util::from_hex<uint32_t>("DEADBEEF", true);
  EXPECT_EQ(result, 0xDEADBEEF);
}

TEST(UtilityFromHexTests, ParseHexToUint32NonReversed) {
  auto result = util::from_hex<uint32_t>("DEADBEEF", false);
  EXPECT_EQ(result, 0xEFBEADDE);
}

TEST(UtilityFromHexTests, ParseHexLowercase) {
  auto result = util::from_hex<uint32_t>("deadbeef", true);
  EXPECT_EQ(result, 0xDEADBEEF);
}

TEST(UtilityFromHexTests, ParseHexToUint16) {
  auto result = util::from_hex<uint16_t>("ABCD", true);
  EXPECT_EQ(result, 0xABCD);
}

TEST(UtilityFromHexTests, ParseHexWithSeparators) {
  // from_hex skips non-hex characters
  auto result = util::from_hex<uint32_t>("DE:AD:BE:EF", true);
  EXPECT_EQ(result, 0xDEADBEEF);
}

// ========== from_hex_vec tests ==========

TEST(UtilityFromHexVecTests, ParseHexStringToBytes) {
  std::string result = util::from_hex_vec("DEADBEEF", true);
  EXPECT_EQ(result.size(), 4);
  EXPECT_EQ(static_cast<uint8_t>(result[0]), 0xDE);
  EXPECT_EQ(static_cast<uint8_t>(result[1]), 0xAD);
  EXPECT_EQ(static_cast<uint8_t>(result[2]), 0xBE);
  EXPECT_EQ(static_cast<uint8_t>(result[3]), 0xEF);
}

TEST(UtilityFromHexVecTests, ParseHexStringNonReversed) {
  std::string result = util::from_hex_vec("DEADBEEF", false);
  EXPECT_EQ(result.size(), 4);
  EXPECT_EQ(static_cast<uint8_t>(result[0]), 0xEF);
  EXPECT_EQ(static_cast<uint8_t>(result[1]), 0xBE);
  EXPECT_EQ(static_cast<uint8_t>(result[2]), 0xAD);
  EXPECT_EQ(static_cast<uint8_t>(result[3]), 0xDE);
}

// ========== from_chars / from_view tests ==========

struct FromViewTest: testing::TestWithParam<std::tuple<std::string_view, int64_t>> {};

TEST_P(FromViewTest, ParsesCorrectly) {
  auto [input, expected] = GetParam();
  EXPECT_EQ(util::from_view(input), expected);
}

INSTANTIATE_TEST_SUITE_P(
  UtilityTests,
  FromViewTest,
  testing::Values(
    std::make_tuple("0", int64_t {0}),
    std::make_tuple("1", int64_t {1}),
    std::make_tuple("42", int64_t {42}),
    std::make_tuple("12345", int64_t {12345}),
    std::make_tuple("-1", int64_t {-1}),
    std::make_tuple("-999", int64_t {-999}),
    std::make_tuple("2147483647", int64_t {2147483647}),
    std::make_tuple("-2147483648", int64_t {-2147483648LL})
  )
);

TEST(UtilityFromViewTests, EmptyStringReturnsZero) {
  EXPECT_EQ(util::from_view(""), 0);
}

// ========== Either tests ==========

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

// ========== FailGuard tests ==========

TEST(UtilityFailGuardTests, ExecutesOnDestruction) {
  bool executed = false;
  {
    auto guard = util::fail_guard([&]() { executed = true; });
  }
  EXPECT_TRUE(executed);
}

TEST(UtilityFailGuardTests, DoesNotExecuteWhenDisabled) {
  bool executed = false;
  {
    auto guard = util::fail_guard([&]() { executed = true; });
    guard.disable();
  }
  EXPECT_FALSE(executed);
}

TEST(UtilityFailGuardTests, MoveDoesNotDoubleExecute) {
  int count = 0;
  {
    auto guard1 = util::fail_guard([&]() { count++; });
    auto guard2 = std::move(guard1);
  }
  EXPECT_EQ(count, 1);
}

// ========== buffer_t tests ==========

TEST(UtilityBufferTests, ConstructWithSize) {
  util::buffer_t<int> buf(10);
  EXPECT_EQ(buf.size(), 10u);
}

TEST(UtilityBufferTests, ConstructWithSizeAndValue) {
  util::buffer_t<int> buf(5, 42);
  for (size_t i = 0; i < buf.size(); ++i) {
    EXPECT_EQ(buf[i], 42);
  }
}

TEST(UtilityBufferTests, DefaultConstructIsEmpty) {
  util::buffer_t<int> buf;
  EXPECT_EQ(buf.size(), 0u);
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
  util::buffer_t<int> buf(3, 7);
  int sum = 0;
  for (auto it = buf.begin(); it != buf.end(); ++it) {
    sum += *it;
  }
  EXPECT_EQ(sum, 21);
}

TEST(UtilityBufferTests, MoveConstruction) {
  util::buffer_t<int> buf1(3, 99);
  util::buffer_t<int> buf2(std::move(buf1));
  EXPECT_EQ(buf2.size(), 3u);
  EXPECT_EQ(buf2[0], 99);
  EXPECT_EQ(buf1.size(), 0u);
}

TEST(UtilityBufferTests, CopyConstruction) {
  util::buffer_t<int> buf1(3, 55);
  util::buffer_t<int> buf2(buf1);
  EXPECT_EQ(buf2.size(), 3u);
  EXPECT_EQ(buf2[0], 55);
  // original unchanged
  EXPECT_EQ(buf1.size(), 3u);
  EXPECT_EQ(buf1[0], 55);
}

// ========== append_struct tests ==========

TEST(UtilityAppendStructTests, AppendsDataCorrectly) {
  struct TestStruct {
    uint8_t a;
    uint8_t b;
    uint8_t c;
  };

  TestStruct s {0xAA, 0xBB, 0xCC};
  std::vector<uint8_t> buf;
  util::append_struct(buf, s);

  EXPECT_GE(buf.size(), 3u);
  EXPECT_EQ(buf[0], 0xAA);
  EXPECT_EQ(buf[1], 0xBB);
  EXPECT_EQ(buf[2], 0xCC);
}

// ========== endian tests ==========

TEST(UtilityEndianTests, BigEndianConversion) {
  uint32_t val = 0x01020304;
  auto big = util::endian::big(val);

  // On little-endian systems, big() should reverse bytes
  auto *bytes = reinterpret_cast<uint8_t *>(&big);
  if constexpr (util::endian::endianness<>::little) {
    EXPECT_EQ(bytes[0], 0x04);
    EXPECT_EQ(bytes[1], 0x03);
    EXPECT_EQ(bytes[2], 0x02);
    EXPECT_EQ(bytes[3], 0x01);
  } else {
    EXPECT_EQ(bytes[0], 0x01);
    EXPECT_EQ(bytes[1], 0x02);
    EXPECT_EQ(bytes[2], 0x03);
    EXPECT_EQ(bytes[3], 0x04);
  }
}

TEST(UtilityEndianTests, LittleEndianConversion) {
  uint32_t val = 0x01020304;
  auto little_val = util::endian::little(val);

  auto *bytes = reinterpret_cast<uint8_t *>(&little_val);
  if constexpr (util::endian::endianness<>::little) {
    // Already little endian, should be unchanged
    EXPECT_EQ(bytes[0], 0x04);
    EXPECT_EQ(bytes[1], 0x03);
    EXPECT_EQ(bytes[2], 0x02);
    EXPECT_EQ(bytes[3], 0x01);
  }
}

TEST(UtilityEndianTests, RoundTripBigEndian) {
  uint32_t original = 0xDEADBEEF;
  auto converted = util::endian::big(util::endian::big(original));
  EXPECT_EQ(converted, original);
}

TEST(UtilityEndianTests, RoundTripLittleEndian) {
  uint32_t original = 0xCAFEBABE;
  auto converted = util::endian::little(util::endian::little(original));
  EXPECT_EQ(converted, original);
}

// ========== log_hex tests ==========

TEST(UtilityLogHexTests, FormatsWithPrefix) {
  uint8_t val = 0xAB;
  std::string result = util::log_hex(val);
  EXPECT_EQ(result, "0xAB");
}

TEST(UtilityLogHexTests, FormatsZero) {
  uint8_t val = 0x00;
  std::string result = util::log_hex(val);
  EXPECT_EQ(result, "0x00");
}

TEST(UtilityLogHexTests, Formats16Bit) {
  uint16_t val = 0x1234;
  std::string result = util::log_hex(val);
  EXPECT_EQ(result, "0x1234");
}
