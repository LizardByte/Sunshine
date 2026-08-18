/**
 * @file tests/unit/platform/test_ib_input_simulator.cpp
 * @brief Tests for the optional IbInputSimulator C ABI wrapper.
 */

#ifdef _WIN32

  // standard includes
  #include <array>
  #include <cstdint>
  #include <filesystem>
  #include <string>
  #include <string_view>
  #include <vector>

  // local includes
  #include "../../tests_common.h"
  #include "src/platform/windows/ib_input_simulator.h"

namespace {
  std::uint32_t destroy_calls;  ///< Number of mock destroy calls.
  UINT submitted_count;  ///< Number of records observed by the mock API.
  bool accept_all;  ///< Whether the mock API accepts every record.

  /**
   * @brief Mock IbInputSimulator initializer.
   *
   * @return Success.
   */
  std::uint32_t __stdcall mock_init(std::uint32_t, std::uint32_t, void *) {
    return 0;
  }

  /**
   * @brief Record mock IbInputSimulator destruction.
   */
  void __stdcall mock_destroy() {
    ++destroy_calls;
  }

  /**
   * @brief Record mock input submission.
   *
   * @param count Number of input records.
   * @return All records when enabled, otherwise one fewer record.
   */
  UINT WINAPI mock_send_input(UINT count, LPINPUT, int) {
    submitted_count += count;
    return accept_all || count == 0 ? count : count - 1;
  }

  /**
   * @brief Reset mock state before each runtime test.
   */
  void reset_mocks() {
    destroy_calls = 0;
    submitted_count = 0;
    accept_all = true;
  }

  /**
   * @brief Build a complete mock IbInputSimulator API.
   *
   * @return Mock API table.
   */
  platf::ib_input_simulator::api_t mock_api() {
    return {
      .init = mock_init,
      .destroy = mock_destroy,
      .send_input = mock_send_input,
    };
  }
}  // namespace

TEST(IbInputSimulatorRuntimeTest, RejectsIncompleteMockApi) {
  EXPECT_EQ(platf::ib_input_simulator::runtime_t::create_for_testing({}), nullptr);
}

TEST(IbInputSimulatorRuntimeTest, ForwardsCompleteInputBatchAndDestroysState) {
  reset_mocks();
  {
    auto runtime = platf::ib_input_simulator::runtime_t::create_for_testing(mock_api());
    ASSERT_NE(runtime, nullptr);

    const std::array inputs {
      INPUT {.type = INPUT_KEYBOARD},
      INPUT {.type = INPUT_MOUSE},
    };
    EXPECT_TRUE(runtime->send(inputs));
    EXPECT_EQ(submitted_count, inputs.size());

    EXPECT_TRUE(runtime->send(std::span<const INPUT> {}));
    EXPECT_EQ(submitted_count, inputs.size());
  }
  EXPECT_EQ(destroy_calls, 1);
}

TEST(IbInputSimulatorRuntimeTest, ReportsPartialSubmissionFailure) {
  reset_mocks();
  accept_all = false;
  auto runtime = platf::ib_input_simulator::runtime_t::create_for_testing(mock_api());
  ASSERT_NE(runtime, nullptr);

  const INPUT input {.type = INPUT_KEYBOARD};
  EXPECT_FALSE(runtime->send(input));
  EXPECT_EQ(submitted_count, 1);
}

TEST(IbInputSimulatorRuntimeTest, LoadsConfiguredRuntime) {
  std::vector<wchar_t> buffer(32768);
  const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) {
    GTEST_SKIP() << "Unable to locate the test executable";
  }

  const auto dll = std::filesystem::path {std::wstring_view {buffer.data(), length}}.parent_path() / L"IbInputSimulator.dll";
  if (!std::filesystem::exists(dll)) {
    GTEST_SKIP() << "Optional IbInputSimulator runtime is not configured for this build";
  }

  auto runtime = platf::ib_input_simulator::runtime_t::create();
  EXPECT_NE(runtime, nullptr) << "The configured IbInputSimulator runtime must initialize on this host";
}

TEST(IbInputSimulatorRuntimeTest, MissingRuntimeReturnsNullptr) {
  platf::ib_input_simulator::runtime_t::set_dll_path_override_for_testing("C:\\nonexistent\\IbInputSimulator.dll");
  auto runtime = platf::ib_input_simulator::runtime_t::create();
  EXPECT_EQ(runtime, nullptr);
  platf::ib_input_simulator::runtime_t::clear_dll_path_override_for_testing();
}

TEST(IbInputSimulatorRuntimeTest, BackendTypeValuesMatchIbInputSimulator) {
  using platf::ib_input_simulator::backend;
  EXPECT_EQ(static_cast<std::uint32_t>(backend::logitech_ghub), 6);
  EXPECT_EQ(static_cast<std::uint32_t>(backend::razer), 3);
}

TEST(IbInputSimulatorRuntimeTest, BackendForValueParsesSettings) {
  using platf::ib_input_simulator::backend;
  EXPECT_EQ(platf::ib_input_simulator::backend_for_value("virtualhid"), std::nullopt);
  EXPECT_EQ(platf::ib_input_simulator::backend_for_value("logitech_ghub"), backend::logitech_ghub);
  EXPECT_EQ(platf::ib_input_simulator::backend_for_value("razer"), backend::razer);
  EXPECT_EQ(platf::ib_input_simulator::backend_for_value("unknown"), std::nullopt);
}

TEST(IbInputSimulatorRuntimeTest, LoadsConfiguredRazerRuntime) {
  std::vector<wchar_t> buffer(32768);
  const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) {
    GTEST_SKIP() << "Unable to locate the test executable";
  }

  const auto dll = std::filesystem::path {std::wstring_view {buffer.data(), length}}.parent_path() / L"IbInputSimulator.dll";
  if (!std::filesystem::exists(dll)) {
    GTEST_SKIP() << "Optional IbInputSimulator runtime is not configured for this build";
  }

  auto runtime = platf::ib_input_simulator::runtime_t::create(platf::ib_input_simulator::backend::razer);
  if (!runtime) {
    GTEST_SKIP() << "Razer driver is not available on this host";
  }
  EXPECT_NE(runtime, nullptr);
}

#endif
