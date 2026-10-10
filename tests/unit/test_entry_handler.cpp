/**
 * @file tests/unit/test_entry_handler.cpp
 * @brief Test src/entry_handler.*.
 */

// test includes
#include "../tests_common.h"
#include "../tests_log_checker.h"

// local includes
#include <src/entry_handler.h>

/**
 * @brief Test fixture that restores Web UI URL configuration after each test.
 */
class LaunchUiUrlTest: public BaseTest {
private:
  std::string original_bind_address;
  std::uint16_t original_port;

protected:
  void SetUp() override {
    BaseTest::SetUp();
    original_bind_address = config::sunshine.bind_address;
    original_port = config::sunshine.port;
    config::sunshine.port = 47989;
  }

  void TearDown() override {
    config::sunshine.bind_address = original_bind_address;
    config::sunshine.port = original_port;
    BaseTest::TearDown();
  }
};

/**
 * @brief Test that Web UI URLs use localhost when no bind address is configured.
 */
TEST_F(LaunchUiUrlTest, UsesLocalhostWithoutBindAddress) {
  config::sunshine.bind_address = "";

  EXPECT_EQ(get_launch_ui_url(), "https://localhost:47990");
  EXPECT_EQ(get_launch_ui_url("/pin"), "https://localhost:47990/pin");
}

/**
 * @brief Test that Web UI URLs use the configured IPv4 bind address.
 */
TEST_F(LaunchUiUrlTest, UsesConfiguredIPv4Address) {
  config::sunshine.bind_address = "198.51.100.56";

  EXPECT_EQ(get_launch_ui_url(), "https://198.51.100.56:47990");
}

/**
 * @brief Test that Web UI URLs enclose configured IPv6 bind addresses in brackets.
 */
TEST_F(LaunchUiUrlTest, EscapesConfiguredIPv6Address) {
  config::sunshine.bind_address = "2001:db8::1";

  EXPECT_EQ(get_launch_ui_url("/config"), "https://[2001:db8::1]:47990/config");
}

TEST(EntryHandlerTests, LogPublisherDataTest) {
  // call log_publisher_data
  log_publisher_data();

  // check if specific log messages exist
  ASSERT_TRUE(log_checker::line_starts_with("test_sunshine.log", "Info: Package Publisher: "));
  ASSERT_TRUE(log_checker::line_starts_with("test_sunshine.log", "Info: Publisher Website: "));
  ASSERT_TRUE(log_checker::line_starts_with("test_sunshine.log", "Info: Get support: "));
}
