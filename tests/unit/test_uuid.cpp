/**
 * @file tests/unit/test_uuid.cpp
 * @brief Tests for UUID byte layout, generation, formatting, and ordering.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <type_traits>

// local includes
#include <src/utility.h>
#include <src/uuid.h>

TEST(UuidTest, PreservesNativeLayoutAndCanonicalFormatting) {
  static_assert(sizeof(uuid_util::uuid_t) == 16);
  static_assert(alignof(uuid_util::uuid_t) == alignof(std::uint64_t));
  static_assert(std::is_trivially_copyable_v<uuid_util::uuid_t>);
  const uuid_util::uuid_t uuid {.b8 = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}};
  EXPECT_EQ(uuid.string(), "00112233-4455-6677-8899-AABBCCDDEEFF");  // NOSONAR(cpp:S6232): Calling a union member function does not access an inactive data member.
}

TEST(UuidTest, GenerationPreservesDrawOrderAndMasks) {
  std::default_random_engine engine {42};  // NOSONAR(cpp:S2245): Deterministic regression fixture; no security-sensitive values are generated.
  auto expected_engine = engine;  // NOSONAR(cpp:S2245): Copy the deterministic fixture to verify draw order independently.
  std::uniform_int_distribution<std::uint8_t> distribution(0, 255);
  std::array<std::uint8_t, 16> expected;
  for (auto &byte : expected) {
    byte = distribution(expected_engine);
  }
  expected[7] = std::to_integer<std::uint8_t>(std::byte {expected[7]} & std::byte {0b00101111});
  expected[9] = std::to_integer<std::uint8_t>(std::byte {expected[9]} & std::byte {0b10011111});

  const auto uuid = uuid_util::uuid_t::generate(engine);
  EXPECT_EQ(uuid.b8, expected);
  EXPECT_EQ(std::byte {uuid.b8[7]} & std::byte {0xD0}, std::byte {0});
  EXPECT_EQ(std::byte {uuid.b8[9]} & std::byte {0x60}, std::byte {0});
  EXPECT_EQ(engine, expected_engine);
  EXPECT_EQ(uuid_util::uuid_t::generate().string().size(), 36U);
}

TEST(UuidTest, ComparesBothWordsInTheirEstablishedOrder) {
  const uuid_util::uuid_t base {.b64 = {2, 3}};
  const uuid_util::uuid_t equal {.b64 = {2, 3}};
  const uuid_util::uuid_t lower_first {.b64 = {1, 9}};
  const uuid_util::uuid_t higher_first {.b64 = {3, 1}};
  const uuid_util::uuid_t lower_second {.b64 = {2, 2}};
  const uuid_util::uuid_t higher_second {.b64 = {2, 4}};
  EXPECT_EQ(base, equal);
  EXPECT_FALSE(base < equal);
  EXPECT_FALSE(base > equal);
  for (const auto &lower : {lower_first, lower_second}) {
    EXPECT_FALSE(base == lower);
    EXPECT_TRUE(lower < base);
    EXPECT_TRUE(base > lower);
    EXPECT_FALSE(base < lower);
    EXPECT_FALSE(lower > base);
  }
  for (const auto &higher : {higher_first, higher_second}) {
    EXPECT_FALSE(base == higher);
    EXPECT_TRUE(base < higher);
    EXPECT_TRUE(higher > base);
    EXPECT_FALSE(higher < base);
    EXPECT_FALSE(base > higher);
  }
}
