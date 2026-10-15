/**
 * @file tests/unit/test_utility.cpp
 * @brief Tests for general utility helpers.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// local includes
#include <src/utility.h>

/**
 * @brief Whether a type has a supported empty-state sentinel.
 */
template<class T>
concept has_false_value = requires { util::__false_v<T>::value; };

/**
 * @brief Sentinel constraints select empty states without accepting unrelated values.
 */
TEST(UtilityFalseValueTests, SelectsSentinelsForOptionalPointerAndBooleanTypes) {
  static_assert(std::is_same_v<decltype(util::false_v<std::optional<int>>), const std::nullopt_t>);
  static_assert(std::is_same_v<decltype(util::false_v<int *>), const std::nullptr_t>);
  static_assert(std::is_same_v<decltype(util::false_v<std::unique_ptr<int>>), const std::nullptr_t>);
  static_assert(std::is_same_v<decltype(util::false_v<std::shared_ptr<int>>), const std::nullptr_t>);
  static_assert(std::is_same_v<decltype(util::false_v<util::uniq_ptr<int>>), const std::nullptr_t>);
  static_assert(std::is_same_v<decltype(util::false_v<bool>), const bool>);
  static_assert(!has_false_value<int>);

  EXPECT_FALSE(std::optional<int> {util::false_v<std::optional<int>>});
  EXPECT_EQ(util::false_v<int *>, nullptr);
  EXPECT_EQ(util::false_v<std::unique_ptr<int>>, nullptr);
  EXPECT_EQ(util::false_v<std::shared_ptr<int>>, nullptr);
  EXPECT_EQ(util::false_v<util::uniq_ptr<int>>, nullptr);
  EXPECT_FALSE(util::false_v<bool>);
}

/**
 * @brief A const resource deleter invokes its release function exactly once per call.
 */
TEST(UtilityDestroyTest, ConstDeleterInvokesTheConfiguredReleaseFunction) {
  const util::Destroy<int *, void, +[](int *value) {
    ++*value;
  }>
    destroy;
  int releases = 0;
  destroy(&releases);
  EXPECT_EQ(releases, 1);
}

/**
 * @brief Simulate the opposite byte order for a byte-array fixture.
 */
template<>
struct util::endian::endianness<std::array<std::byte, 4>> {
  static constexpr bool little = !util::endian::endianness<>::little;  ///< Opposite native byte order.
  static constexpr bool big = !little;  ///< Whether the fixture simulates big endian.
};

/**
 * @brief Use the simulated byte order for optional byte-array fixtures.
 */
template<>
struct util::endian::endianness<std::optional<std::array<std::byte, 4>>>: util::endian::endianness<std::array<std::byte, 4>> {};

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
  buf1[1] = 66;
  buf1[2] = 77;
  util::buffer_t<int> buf2(buf1);
  EXPECT_EQ(buf2.size(), 3U);
  EXPECT_EQ(buf2[0], 55);
  EXPECT_EQ(buf2[1], 66);
  EXPECT_EQ(buf2[2], 77);
  EXPECT_EQ(buf1.size(), 3U);
  EXPECT_EQ(buf1[0], 55);
}

TEST(UtilityBufferTests, CopiesAnEmptyLogicalRange) {
  util::buffer_t buf(3, 42);
  buf.fake_resize(0);
  const util::buffer_t<int> copy(buf);
  EXPECT_EQ(copy.size(), 0U);
  EXPECT_EQ(copy.begin(), copy.end());
  EXPECT_EQ(buf[0], 42);
}

TEST(UtilityBufferTests, SizeConstructionValueInitializesEveryElement) {
  const util::buffer_t<int> buf(4);
  EXPECT_EQ(buf.end() - buf.begin(), 4);
  EXPECT_TRUE(std::ranges::all_of(buf, [](int value) {
    return value == 0;
  }));
}

TEST(UtilityBufferTests, LogicalResizeRetainsAllocationAndHiddenElements) {
  util::buffer_t buf(4, 7);
  auto *allocation = buf.begin();
  buf[3] = 99;
  buf.fake_resize(2);
  EXPECT_EQ(buf.size(), 2U);
  EXPECT_EQ(buf.begin(), allocation);
  EXPECT_EQ(buf.end(), allocation + 2);

  const util::buffer_t<int> copy(buf);
  EXPECT_EQ(copy.size(), 2U);
  EXPECT_NE(copy.begin(), allocation);
  EXPECT_EQ(copy[0], 7);
  EXPECT_EQ(copy[1], 7);
  buf[0] = 10;
  EXPECT_EQ(copy[0], 7);

  buf.fake_resize(0);
  EXPECT_EQ(buf.begin(), buf.end());
  buf.fake_resize(4);
  EXPECT_EQ(buf.begin(), allocation);
  EXPECT_EQ(buf[3], 99);
}

TEST(UtilityBufferTests, MoveConstructionTransfersAllocationAndEmptiesSource) {
  util::buffer_t source(4, 9);
  source.fake_resize(2);
  auto *allocation = source.begin();
  const util::buffer_t<int> destination(std::move(source));
  EXPECT_EQ(destination.size(), 2U);
  EXPECT_EQ(destination.begin(), allocation);
  EXPECT_EQ(destination[1], 9);
  EXPECT_EQ(source.size(), 0U);  // NOSONAR(cpp:S5272): buffer_t specifies a zero logical count after move construction.
  EXPECT_EQ(source.begin(), source.end());
}

TEST(UtilityBufferTests, MoveAssignmentExchangesBothAllocationsAndLogicalSizes) {
  util::buffer_t source(4, 9);
  source.fake_resize(2);
  util::buffer_t destination(3, 7);
  auto *source_allocation = source.begin();
  auto *destination_allocation = destination.begin();

  EXPECT_EQ(&(destination = std::move(source)), &destination);
  EXPECT_EQ(destination.size(), 2U);
  EXPECT_EQ(destination.begin(), source_allocation);
  EXPECT_EQ(destination[1], 9);
  EXPECT_EQ(source.size(), 3U);  // NOSONAR(cpp:S5272): buffer_t move assignment exchanges both allocations and logical counts.
  EXPECT_EQ(source.begin(), destination_allocation);
  EXPECT_EQ(source[2], 7);
}

TEST(UtilityHexTests, IteratorsAndViewsExposeExactlyTheFormattedBytes) {
  auto hex = util::hex(std::uint16_t {0xABCD});
  EXPECT_EQ(hex.end() - hex.begin(), 4);
  EXPECT_EQ(std::string(hex.begin(), hex.end()), "ABCD");
  *hex.begin() = '0';
  const auto &constant_hex = hex;
  EXPECT_EQ(constant_hex.end() - constant_hex.begin(), 4);
  EXPECT_EQ(constant_hex.cbegin(), constant_hex.begin());
  EXPECT_EQ(constant_hex.cend(), constant_hex.end());
  EXPECT_EQ(constant_hex.to_string_view(), "0BCD");
  EXPECT_EQ(constant_hex.to_string(), "0BCD");
}

TEST(UtilityFromHexTests, ShortInputPadsUnusedBytesWithZeros) {
  EXPECT_EQ(util::from_hex<std::uint64_t>("ABCD"), util::endian::little(std::uint64_t {0xABCD}));
  EXPECT_EQ(util::from_hex<std::uint64_t>("ABCD", true), util::endian::big(std::uint64_t {0xABCD}));
}

TEST(UtilityFromHexTests, LongInputKeepsTheLowOrderBytes) {
  EXPECT_EQ(util::from_hex<std::uint32_t>("123456789A"), util::endian::little(std::uint32_t {0x3456789A}));
  EXPECT_EQ(util::from_hex<std::uint32_t>("123456789A", true), util::endian::big(std::uint32_t {0x3456789A}));
}

TEST(UtilityFromHexTests, DecodesIntoAByteArrayWithoutPointerTypePunning) {
  const auto bytes = util::from_hex<std::array<std::uint8_t, 4>>("12:34:56:78", true);
  EXPECT_EQ(bytes, (std::array<std::uint8_t, 4> {0x12, 0x34, 0x56, 0x78}));
}

TEST(UtilityFromHexTests, DecodesMixedCaseNibblesAndSkipsSeparators) {
  const auto bytes = util::from_hex<std::array<std::byte, 6>>("0a:B1-cD.2e 3F 49", true);
  const std::array expected {std::byte {0x0A}, std::byte {0xB1}, std::byte {0xCD}, std::byte {0x2E}, std::byte {0x3F}, std::byte {0x49}};
  EXPECT_EQ(bytes, expected);
  EXPECT_EQ(util::from_hex<std::uint32_t>(""), 0U);
}

TEST(UtilityFromHexVecTests, PreservesBinaryBytesWithMixedCaseAndSeparators) {
  const std::string expected {0, static_cast<char>(0x80), static_cast<char>(0xFF), static_cast<char>(0xAB), 0x19};
  EXPECT_EQ(util::from_hex_vec("00:80-fF aB.19", true), expected);
  EXPECT_EQ(util::from_hex_vec("00:80-fF aB.19"), (std::string {expected.rbegin(), expected.rend()}));
  EXPECT_TRUE(util::from_hex_vec("").empty());
}

TEST(UtilityHashTests, MatchesStringViewHashOfAllObjectBytes) {
  const std::array<std::uint8_t, 6> value {0x00, 0x80, 0xFF, 0x12, 0x00, 0xAB};
  const std::string expected_bytes {0, static_cast<char>(0x80), static_cast<char>(0xFF), 0x12, 0, static_cast<char>(0xAB)};
  EXPECT_EQ(util::hash<decltype(value)> {}(value), std::hash<std::string_view> {}(expected_bytes));
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

TEST(UtilityEndianTests, OptionalValuesPreserveEmptyStateAndConvertContainedBytes) {
  const std::optional<std::uint32_t> value {0x01020304};
  EXPECT_EQ(util::endian::big(value), std::optional {util::endian::big(*value)});
  EXPECT_EQ(util::endian::little(value), std::optional {util::endian::little(*value)});
  EXPECT_FALSE(util::endian::big(std::optional<std::uint32_t> {}));
  EXPECT_FALSE(util::endian::little(std::optional<std::uint32_t> {}));
}

TEST(UtilityEndianTests, ConvertsValuesAndOptionalsOnTheOppositeByteOrder) {
  const std::array value {std::byte {0x12}, std::byte {0x34}, std::byte {0x56}, std::byte {0x78}};
  const std::array reversed {std::byte {0x78}, std::byte {0x56}, std::byte {0x34}, std::byte {0x12}};
  const std::optional optional_value {value};
  if constexpr (util::endian::endianness<>::little) {
    EXPECT_EQ(util::endian::little(value), reversed);
    EXPECT_EQ(util::endian::big(value), value);
    EXPECT_EQ(util::endian::little(optional_value), std::optional {reversed});
    EXPECT_EQ(util::endian::big(optional_value), optional_value);
  } else {
    EXPECT_EQ(util::endian::big(value), reversed);
    EXPECT_EQ(util::endian::little(value), value);
    EXPECT_EQ(util::endian::big(optional_value), std::optional {reversed});
    EXPECT_EQ(util::endian::little(optional_value), optional_value);
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
