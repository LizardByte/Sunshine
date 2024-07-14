/**
 * @file tests/unit/test_stream.cpp
 * @brief Test src/stream.*
 */

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace stream {
  std::vector<uint8_t>
  concat_and_insert(uint64_t insert_size, uint64_t slice_size, const std::string_view &data1, const std::string_view &data2);
}

#include <tests/conftest.cpp>

TEST(ConcatAndInsertTests, ConcatNoInsertionTest) {
  char b1[] = { 'a', 'b' };
  char b2[] = { 'c', 'd', 'e' };
  auto res = stream::concat_and_insert(0, 2, std::string_view { b1, sizeof(b1) }, std::string_view { b2, sizeof(b2) });
  auto expected = std::vector<uint8_t> { 'a', 'b', 'c', 'd', 'e' };
  ASSERT_EQ(res, expected);
}

TEST(ConcatAndInsertTests, ConcatLargeStrideTest) {
  char b1[] = { 'a', 'b' };
  char b2[] = { 'c', 'd', 'e' };
  auto res = stream::concat_and_insert(1, sizeof(b1) + sizeof(b2) + 1, std::string_view { b1, sizeof(b1) }, std::string_view { b2, sizeof(b2) });
  auto expected = std::vector<uint8_t> { 0, 'a', 'b', 'c', 'd', 'e' };
  ASSERT_EQ(res, expected);
}

TEST(ConcatAndInsertTests, ConcatSmallStrideTest) {
  char b1[] = { 'a', 'b' };
  char b2[] = { 'c', 'd', 'e' };
  auto res = stream::concat_and_insert(1, 1, std::string_view { b1, sizeof(b1) }, std::string_view { b2, sizeof(b2) });
  auto expected = std::vector<uint8_t> { 0, 'a', 0, 'b', 0, 'c', 0, 'd', 0, 'e' };
  ASSERT_EQ(res, expected);
}
