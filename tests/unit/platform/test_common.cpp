/**
 * @file tests/unit/platform/test_common.cpp
 * @brief Test src/platform/common.*.
 */
#include "../../tests_common.h"

#include <boost/asio/ip/host_name.hpp>
#include <src/platform/common.h>

TEST(HostnameTests, TestAsioEquality) {
  // These should be equivalent on all platforms for ASCII hostnames
  ASSERT_EQ(platf::get_host_name(), boost::asio::ip::host_name());
}
