/**
 * @file src/platform/windows/ib_input_simulator.h
 * @brief Secure dynamic access to the optional IbInputSimulator runtime.
 */
#pragma once

// platform includes
#include <Windows.h>

// standard includes
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>

namespace platf::ib_input_simulator {
  /**
   * @brief IbInputSimulator send types supported by Sunshine.
   */
  enum class backend : std::uint32_t {
    logitech_ghub = 6,  ///< `LogitechGHubNew`.
    razer = 3,  ///< `Razer`.
  };

  /**
   * @brief Map a configured backend name to an IbInputSimulator send type.
   *
   * @param value Configured `keyboard_backend` or `mouse_backend` value.
   * @return Matching backend, or `std::nullopt` for `virtualhid`/unknown values.
   */
  std::optional<backend> backend_for_value(std::string_view value);

  /**
   * @brief C ABI functions exported by IbInputSimulator.
   */
  struct api_t {
    using init_t = std::uint32_t(__stdcall *)(std::uint32_t, std::uint32_t, void *);  ///< `IbSendInit` signature.
    using destroy_t = void(__stdcall *)();  ///< `IbSendDestroy` signature.
    using send_input_t = UINT(WINAPI *)(UINT, LPINPUT, int);  ///< `IbSendInput` signature.

    init_t init = nullptr;  ///< Initialize the selected simulator backend.
    destroy_t destroy = nullptr;  ///< Release the simulator backend.
    send_input_t send_input = nullptr;  ///< Submit Windows input records.
  };

  /**
   * @brief Loaded and initialized IbInputSimulator backend.
   */
  class runtime_t {
  public:
    runtime_t(const runtime_t &) = delete;
    runtime_t &operator=(const runtime_t &) = delete;
    runtime_t(runtime_t &&) = delete;
    runtime_t &operator=(runtime_t &&) = delete;

    /**
     * @brief Load `IbInputSimulator.dll` and initialize the selected backend.
     *
     * @param selected Backend to initialize.
     *
     * @return Initialized runtime, or `nullptr` when loading or initialization fails.
     */
    static std::unique_ptr<runtime_t> create(backend selected = backend::logitech_ghub);

    /**
     * @brief Release simulator state and unload the optional DLL.
     */
    ~runtime_t();

    /**
     * @brief Submit one or more Windows input records.
     *
     * @param inputs Input records to submit.
     * @return True when the simulator accepted every record.
     */
    bool send(std::span<const INPUT> inputs);

    /**
     * @brief Submit one Windows input record.
     *
     * @param input Input record to submit.
     * @return True when the simulator accepted the record.
     */
    bool send(const INPUT &input);

#ifdef SUNSHINE_TESTS
    /**
     * @brief Construct an initialized runtime around mock C ABI functions.
     *
     * @param api Mock API functions.
     * @return Runtime that does not own a DLL module.
     */
    static std::unique_ptr<runtime_t> create_for_testing(api_t api);

    /**
     * @brief Override the DLL path used by `create` for fallback tests.
     *
     * @param path Absolute DLL path to load, or an empty path to clear.
     */
    static void set_dll_path_override_for_testing(std::filesystem::path path);

    /**
     * @brief Restore automatic DLL path resolution.
     */
    static void clear_dll_path_override_for_testing();
#endif

  private:
    /**
     * @brief Store a resolved API and its optional owning module.
     *
     * @param module Loaded DLL module, or `nullptr` for a test runtime.
     * @param api Resolved C ABI functions.
     */
    runtime_t(HMODULE module, api_t api);

    HMODULE module_ = nullptr;  ///< Owned IbInputSimulator module.
    api_t api_;  ///< Resolved IbInputSimulator entry points.
    std::mutex mutex_;  ///< Serializes input submission and destruction.
    bool initialized_ = true;  ///< Whether the runtime still owns initialized simulator state.

#ifdef SUNSHINE_TESTS
    /**
     * @brief Shared DLL path override used by fallback tests.
     *
     * @return Mutable path override, empty when automatic resolution is active.
     */
    static std::filesystem::path &dll_path_override();
#endif
  };
}  // namespace platf::ib_input_simulator
