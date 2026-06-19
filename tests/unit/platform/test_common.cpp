/**
 * @file tests/unit/platform/test_common.cpp
 * @brief Test src/platform/common.*.
 */
#include "../../tests_common.h"

#include <boost/asio/ip/host_name.hpp>
#include <src/platform/common.h>

struct SetEnvTest: ::testing::TestWithParam<std::tuple<std::string, std::string, int>> {
protected:
  void TearDown() override {
    // Clean up environment variable after each test
    const auto &[name, value, expected] = GetParam();
    platf::unset_env(name);
  }
};

TEST_P(SetEnvTest, SetEnvironmentVariableTests) {
  const auto &[name, value, expected] = GetParam();
  platf::set_env(name, value);

  const char *env_value = std::getenv(name.c_str());
  if (expected == 0 && !value.empty()) {
    ASSERT_NE(env_value, nullptr);
    ASSERT_EQ(std::string(env_value), value);
  } else {
    ASSERT_EQ(env_value, nullptr);
  }
}

TEST_P(SetEnvTest, UnsetEnvironmentVariableTests) {
  const auto &[name, value, expected] = GetParam();
  platf::unset_env(name);

  const char *env_value = std::getenv(name.c_str());
  if (expected == 0) {
    ASSERT_EQ(env_value, nullptr);
  }
}

INSTANTIATE_TEST_SUITE_P(
  SetEnvTests,
  SetEnvTest,
  ::testing::Values(
    std::make_tuple("SUNSHINE_UNIT_TEST_ENV_VAR", "test_value_0", 0),
    std::make_tuple("SUNSHINE_UNIT_TEST_ENV_VAR", "test_value_1", 0),
    std::make_tuple("", "test_value", -1)
  )
);

TEST(HostnameTests, TestAsioEquality) {
  // These should be equivalent on all platforms for ASCII hostnames
  ASSERT_EQ(platf::get_host_name(), boost::asio::ip::host_name());
}
