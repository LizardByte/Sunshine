/**
 * @file src/platform/windows/nvprefs/nvapi_opensource_wrapper.h
 * @brief Instance-owned NVIDIA driver API tables.
 */
#pragma once

// standard includes
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

// nvapi headers
// clang-format off
#if defined(__GNUC__)
  #define __NVAPI_EMPTY_SAL
#endif
#include <nvapi.h>
#include <NvApiDriverSettings.h>
#if defined(__GNUC__)
  #undef __NVAPI_EMPTY_SAL
  #include <nvapi_lite_salend.h>
#endif
// clang-format on

namespace nvprefs {
  /**
   * @brief Resolved NVAPI interfaces and the loaded library that implements them.
   */
  class api_t {
  public:
    /**
     * @brief Library loading operations that can be supplied by a fake driver in tests.
     */
    struct loader_t {
      std::function<std::shared_ptr<void>(const char *)> open;  ///< Load a library with its owning deleter.
      std::function<void *(void *, const char *)> symbol;  ///< Resolve the interface-query entry point.
    };

    /**
     * @brief Function table keyed by interface names with string-value comparison.
     */
    using interfaces_t = std::map<std::string, void *, std::less<>>;

    /**
     * @brief Construct an immutable table for a library or injected driver implementation.
     *
     * @param library Owner that keeps resolved interface pointers alive.
     * @param interfaces Resolved functions, including null entries for unsupported interfaces.
     */
    api_t(std::shared_ptr<void> library, interfaces_t interfaces):
        library {std::move(library)},
        interfaces {std::move(interfaces)} {
    }

    /**
     * @brief Load the system driver and resolve its NVAPI interface table.
     *
     * @return Immutable API, or nullptr when the driver library or resolver is unavailable.
     */
    static std::shared_ptr<const api_t> load();

    /**
     * @brief Resolve an API using the supplied library-loading operations.
     *
     * @param loader Driver loader used by this initialization attempt.
     * @return Immutable API, or nullptr when loading or resolver lookup fails.
     */
    static std::shared_ptr<const api_t> load(const loader_t &loader);

    /**
     * @brief Invoke a resolved interface without modifying the immutable table.
     *
     * @tparam Func NVAPI function type whose arguments are forwarded.
     * @tparam Args Forwarded argument types.
     * @param name NVAPI function name.
     * @param args Arguments passed to the loaded driver.
     * @return Driver status, or the appropriate unavailable-interface status.
     */
    template<typename Func, typename... Args>
    NvAPI_Status call_interface(std::string_view name, Args... args) const {
      const auto found = interfaces.find(name);
      if (found == interfaces.end() || !found->second) {
        return interfaces.empty() ? NVAPI_API_NOT_INITIALIZED : NVAPI_NOT_SUPPORTED;
      }
      return reinterpret_cast<Func *>(found->second)(args...);
    }

  private:
    const std::shared_ptr<void> library;  ///< Keep the driver loaded while sessions use its interfaces.
    const interfaces_t interfaces;  ///< Immutable function table resolved at initialization.
  };
}  // namespace nvprefs
