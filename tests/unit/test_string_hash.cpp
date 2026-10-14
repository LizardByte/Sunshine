/**
 * @file tests/unit/test_string_hash.cpp
 * @brief Regression coverage for heterogeneous owning-string containers.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <string>
#include <string_view>
#include <unordered_map>

// local includes
#include <src/string_hash.h>

TEST(TransparentStringHashTest, EquivalentKeyRepresentationsFindTheSameEntry) {
  const std::string key(128, 'k');
  const std::string padded = "prefix" + key + "suffix";
  const auto view = std::string_view {padded}.substr(6, key.size());
  std::unordered_map<std::string, int, util::transparent_string_hash, std::equal_to<>> entries {{key, 42}};

  EXPECT_EQ(entries.find(key)->second, 42);
  EXPECT_EQ(entries.find(key.c_str())->second, 42);
  EXPECT_EQ(entries.find(view)->second, 42);
  EXPECT_FALSE(entries.contains(std::string_view {padded}));
  EXPECT_EQ(entries.size(), 1);
}

TEST(TransparentStringHashTest, EmptyAndEmbeddedNullKeysRetainTheirLengths) {
  using namespace std::literals;
  std::unordered_map<std::string, int, util::transparent_string_hash, std::equal_to<>> entries {
    {"", 1},
    {"key\0suffix"s, 2},
    {"key", 3},
  };

  EXPECT_EQ(entries.find(""sv)->second, 1);
  EXPECT_EQ(entries.find("key\0suffix"sv)->second, 2);
  EXPECT_EQ(entries.find("key")->second, 3);
  EXPECT_FALSE(entries.contains("key\0other"sv));
}
