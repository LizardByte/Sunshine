/**
 * @file tests/unit/test_network.cpp
 * @brief Test src/network.*
 */
#include <src/network.h>

#include "../tests_common.h"

TEST(MdnsInstanceNameTests, ValidLength) {
  auto name = net::mdns_instance_name();

  // The instance name must be 63 characters or less
  EXPECT_LT(name.size(), 64);
}

TEST(MdnsInstanceNameTests, ValidCharacters) {
  auto name = net::mdns_instance_name();

  // The string must not contain invalid hostname characters
  for (const char& c : name) {
    EXPECT_TRUE(std::isalnum(c) || c == '-');
  }
}
