/**
 * @file tests/unit/test_entry_handler.cpp
 * @brief Test src/entry_handler.*.
 */
#include "../tests_common.h"
#include "../tests_log_checker.h"

#ifdef _WIN32
  #include <bit>
  #include <cstdint>
  #include <lizardbyte/common/env.h>
  #include <string>
  #include <Windows.h>
#endif

#include <src/entry_handler.h>

#ifdef _WIN32
namespace {
  constexpr auto SERVICE_READY_EVENT_ENV = "SUNSHINE_SERVICE_READY_EVENT";

  class ServiceReadySignalTest: public testing::Test {
  protected:
    void SetUp() override {
      static_cast<void>(lizardbyte::common::unset_env(SERVICE_READY_EVENT_ENV));
    }

    void TearDown() override {
      static_cast<void>(lizardbyte::common::unset_env(SERVICE_READY_EVENT_ENV));
    }
  };

  std::string handle_to_string(HANDLE handle) {
    return std::to_string(std::bit_cast<std::uintptr_t>(handle));
  }

  bool is_handle_open(HANDLE handle) {
    DWORD flags {};
    return GetHandleInformation(handle, &flags) != FALSE;
  }
}  // namespace
#endif

TEST(EntryHandlerTests, LogPublisherDataTest) {
  // call log_publisher_data
  log_publisher_data();

  // check if specific log messages exist
  ASSERT_TRUE(log_checker::line_starts_with("test_sunshine.log", "Info: Package Publisher: "));
  ASSERT_TRUE(log_checker::line_starts_with("test_sunshine.log", "Info: Publisher Website: "));
  ASSERT_TRUE(log_checker::line_starts_with("test_sunshine.log", "Info: Get support: "));
}

#ifdef _WIN32
TEST_F(ServiceReadySignalTest, SignalsAndClosesEventHandle) {
  const auto source_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ASSERT_NE(source_event, nullptr);

  HANDLE inherited_event {};
  ASSERT_TRUE(DuplicateHandle(GetCurrentProcess(), source_event, GetCurrentProcess(), &inherited_event, 0, TRUE, DUPLICATE_SAME_ACCESS));
  ASSERT_EQ(lizardbyte::common::set_env(SERVICE_READY_EVENT_ENV, handle_to_string(inherited_event)), 0);

  service_ctrl::signal_ready();

  EXPECT_EQ(WaitForSingleObject(source_event, 0), WAIT_OBJECT_0);
  const auto inherited_event_is_open = is_handle_open(inherited_event);
  EXPECT_FALSE(inherited_event_is_open);
  if (inherited_event_is_open) {
    CloseHandle(inherited_event);
  }

  std::string env_value;
  EXPECT_FALSE(lizardbyte::common::get_env(SERVICE_READY_EVENT_ENV, env_value));
  EXPECT_TRUE(CloseHandle(source_event));
}

TEST_F(ServiceReadySignalTest, ClearsInvalidHandleText) {
  ASSERT_EQ(lizardbyte::common::set_env(SERVICE_READY_EVENT_ENV, "not-a-handle"), 0);

  service_ctrl::signal_ready();

  std::string env_value;
  EXPECT_FALSE(lizardbyte::common::get_env(SERVICE_READY_EVENT_ENV, env_value));
}

TEST_F(ServiceReadySignalTest, ClosesHandleWhenSetEventFails) {
  const auto mutex = CreateMutexW(nullptr, FALSE, nullptr);
  ASSERT_NE(mutex, nullptr);
  ASSERT_EQ(lizardbyte::common::set_env(SERVICE_READY_EVENT_ENV, handle_to_string(mutex)), 0);

  service_ctrl::signal_ready();

  const auto mutex_is_open = is_handle_open(mutex);
  EXPECT_FALSE(mutex_is_open);
  if (mutex_is_open) {
    CloseHandle(mutex);
  }

  std::string env_value;
  EXPECT_FALSE(lizardbyte::common::get_env(SERVICE_READY_EVENT_ENV, env_value));
}
#endif
