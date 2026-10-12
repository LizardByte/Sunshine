/**
 * @file tests/unit/test_entry_handler.cpp
 * @brief Test src/entry_handler.*.
 */

// test includes
#include "../tests_common.h"
#include "../tests_log_checker.h"

// standard includes
#include <string_view>

// local includes
#include <src/entry_handler.h>

/**
 * @brief Test fixture that owns Web UI URL settings for each test.
 */
class LaunchUiUrlTest: public BaseTest {
protected:
  /**
   * @brief Select the address used to construct Web UI URLs.
   * @param address Hostname or numeric bind address, empty for localhost.
   */
  void set_bind_address(std::string_view address) {
    configuration.edit_startup().sunshine.bind_address = address;
  }

  /**
   * @brief Borrow the settings passed to the URL constructor.
   * @return Read-only settings retained by this fixture.
   */
  const config::settings_t &url_settings() const {
    return configuration.values();
  }

private:
  config::store_t configuration;  ///< Configuration isolated to this fixture.
};

/**
 * @brief Test that Web UI URLs use localhost when no bind address is configured.
 */
TEST_F(LaunchUiUrlTest, UsesLocalhostWithoutBindAddress) {
  set_bind_address("");

  EXPECT_EQ(get_launch_ui_url(url_settings()), "https://localhost:47990");
  EXPECT_EQ(get_launch_ui_url(url_settings(), "/pin"), "https://localhost:47990/pin");
}

/**
 * @brief Test that Web UI URLs use the configured IPv4 bind address.
 */
TEST_F(LaunchUiUrlTest, UsesConfiguredIPv4Address) {
  set_bind_address("198.51.100.56");

  EXPECT_EQ(get_launch_ui_url(url_settings()), "https://198.51.100.56:47990");
}

/**
 * @brief Test that Web UI URLs enclose configured IPv6 bind addresses in brackets.
 */
TEST_F(LaunchUiUrlTest, EscapesConfiguredIPv6Address) {
  set_bind_address("2001:db8::1");

  EXPECT_EQ(get_launch_ui_url(url_settings(), "/config"), "https://[2001:db8::1]:47990/config");
}

TEST(EntryHandlerTests, LogPublisherDataTest) {
  // call log_publisher_data
  log_publisher_data();

  // check if specific log messages exist
  ASSERT_TRUE(log_checker::line_starts_with("test_sunshine.log", "Info: Package Publisher: "));
  ASSERT_TRUE(log_checker::line_starts_with("test_sunshine.log", "Info: Publisher Website: "));
  ASSERT_TRUE(log_checker::line_starts_with("test_sunshine.log", "Info: Get support: "));
}

TEST(EntryHandlerTests, CredentialsCommandShowsHelpWithoutReadingMissingArguments) {
  const config::store_t configuration;
  EXPECT_EQ(args::creds(configuration.values(), "sunshine", 0, nullptr), 0);
}

TEST(EntryHandlerTests, ControllersKeepArgumentsAndExitStatusIndependent) {
  char first_name[] = "first";
  char second_name[] = "second";
  char *first_args[] {first_name, nullptr};
  char *second_args[] {second_name, nullptr};
  lifetime::controller_t first {first_args};
  lifetime::controller_t second {second_args};
  EXPECT_EQ(first.get_argv(), first_args);
  EXPECT_EQ(second.get_argv(), second_args);
  EXPECT_EQ(first.exit_code(), 0);
  EXPECT_EQ(second.exit_code(), 0);

  int signals = 0;
  lifetime::signal_guard_t guard {first};
  guard.on_signal(SIGINT, [&signals]() {
    ++signals;
  });
  first.exit_sunshine(7, true);
  first.exit_sunshine(9, true);
  EXPECT_EQ(first.exit_code(), 7);
  EXPECT_EQ(second.exit_code(), 0);
  EXPECT_EQ(signals, 2);
}

/**
 * @brief Termination signals remain registered before callbacks issue another signal.
 */
TEST(EntryHandlerTests, RearmsTerminationSignalsBeforeInvokingOwnedCallbacks) {
  for (const auto signal : {SIGINT, SIGTERM}) {
    lifetime::controller_t controller;
    lifetime::signal_guard_t guard {controller};
    int signals = 0;
    int repeated_result = -1;
    guard.on_signal(signal, [&signal, &signals, &repeated_result]() {
      if (++signals == 1) {
        repeated_result = std::raise(signal);
      }
    });
    EXPECT_EQ(std::raise(signal), 0);
    EXPECT_EQ(repeated_result, 0);
    EXPECT_EQ(signals, 2);
    EXPECT_EQ(std::raise(signal), 0);
    EXPECT_EQ(signals, 3);
  }
}

TEST(EntryHandlerTests, NativeCallbackBindingIsScopedAndRestoresPreviousHandler) {
  lifetime::controller_t controller;
  const auto expected = +[](int) {
    // An inert previous handler lets the test verify restoration without signal side effects.
  };
  const auto previous = std::signal(SIGINT, expected);
  ASSERT_NE(previous, SIG_ERR);
  auto restore = util::fail_guard([previous]() {
    std::signal(SIGINT, previous);
  });
  int signals = 0;
  {
    lifetime::signal_guard_t guard {controller};
    guard.on_signal(SIGINT, [&signals]() {
      ++signals;
    });
    lifetime::signal_guard_t::native_exit(11, true);
    EXPECT_EQ(controller.exit_code(), 11);
    EXPECT_EQ(signals, 1);
    EXPECT_THROW(lifetime::signal_guard_t {controller}, std::logic_error);
    EXPECT_THROW(guard.on_signal(-1, []() {
    }),
                 std::invalid_argument);
  }
  EXPECT_EQ(std::signal(SIGINT, expected), expected);
  lifetime::signal_guard_t::native_exit(13, true);
  EXPECT_EQ(signals, 1);
}

TEST(EntryHandlerTests, RequestedRestartRunsAfterDependentCleanup) {
  bool cleaned = false;
  bool restarted = false;
  {
    lifetime::controller_t controller;
    controller.restart_on_exit([&cleaned, &restarted]() {
      EXPECT_TRUE(cleaned);
      restarted = true;
    });
    auto dependent = util::fail_guard([&cleaned]() {
      cleaned = true;
    });
  }
  EXPECT_TRUE(restarted);
}

/**
 * @brief Infrastructure destruction waits for active work before releasing its dependencies.
 */
TEST(EntryHandlerTests, InfrastructureDestructionJoinsWorkBeforeDependencyCleanup) {
  auto infrastructure = std::make_unique<runtime::infrastructure_t>();
  auto *owner = infrastructure.get();
  const std::weak_ptr<safe::mail_raw_t> mailbox = owner->mailbox();
#ifdef _WIN32
  const auto *preferences = &owner->preferences();  ///< Read-only identity checked before the preferences owner is released.
#endif
  std::promise<void> started;
  std::promise<void> release;
  const auto released = release.get_future().share();
  owner->scheduler().start(1);
  auto task = owner->scheduler().push([&started, &released, &mailbox, &owner
#ifdef _WIN32
                                       ,
                                       &preferences
#endif
  ]() {
    started.set_value();
    released.wait();
    const bool dependencies_alive = !mailbox.expired() && owner->cursor();
#ifdef _WIN32
    return dependencies_alive && &owner->preferences() == preferences;
#else
    return dependencies_alive;
#endif
  });
  started.get_future().get();
  auto destruction = std::async(std::launch::async, [infrastructure = std::move(infrastructure)]() mutable {
    infrastructure.reset();
  });
  EXPECT_EQ(destruction.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
  release.set_value();
  destruction.get();
  EXPECT_TRUE(task.get());
  EXPECT_TRUE(mailbox.expired());
}

/**
 * @brief Infrastructure destruction accepts a scheduler already stopped and joined by its caller.
 */
TEST(EntryHandlerTests, InfrastructureDestructionAcceptsPreviouslyJoinedScheduler) {
  runtime::infrastructure_t infrastructure;
  infrastructure.scheduler().start(1);
  infrastructure.scheduler().stop();
  infrastructure.scheduler().join();
  infrastructure.scheduler().join();
}
