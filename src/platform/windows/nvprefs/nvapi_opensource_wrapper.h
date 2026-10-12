/**
 * @file src/platform/windows/nvprefs/nvapi_opensource_wrapper.h
 * @brief Instance-owned NVIDIA driver API tables.
 */
#pragma once

// standard includes
#include <bit>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

// platform includes
#include <Windows.h>

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
     * @brief Shared ownership of the actual Windows module handle type.
     */
    using library_t = std::shared_ptr<std::remove_pointer_t<HMODULE>>;

    /**
     * @brief Typed query that returns an NVAPI function address for an interface ID.
     */
    using query_t = std::function<FARPROC(NvU32)>;

    /**
     * @brief Native NVAPI resolver ABI used only when calling the driver export.
     */
    using native_query_t = void *(__cdecl *) (NvU32);

    /**
     * @brief Library loading operations that can be supplied by a fake driver in tests.
     */
    struct loader_t {
      std::function<library_t(const char *)> open;  ///< Load a native module with its owning deleter.
      std::function<query_t(HMODULE, const char *)> query;  ///< Resolve and adapt the interface-query entry point.
    };

    /**
     * @brief Function table keyed by interface names with string-value comparison.
     */
    using interfaces_t = std::map<std::string, FARPROC, std::less<>>;

    /**
     * @brief Construct an immutable table for a library or injected driver implementation.
     *
     * @param library Owner that keeps resolved interface pointers alive.
     * @param interfaces Resolved functions, including null entries for unsupported interfaces.
     */
    api_t(library_t library, interfaces_t interfaces):
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
     * @brief Bind a Windows export to the native resolver ABI and typed query results.
     *
     * The returned callable borrows the export; retain its module while calling it.
     *
     * @param address Resolver address returned by GetProcAddress, or nullptr when unavailable.
     * @return Typed interface-query callable, or an empty callable for a null export.
     */
    static query_t adapt_query(FARPROC address);

    /**
     * @brief Invoke a resolved interface without modifying the immutable table.
     * @details Windows represents these NVAPI exports as flat code addresses. Copying the
     *          address representation preserves the native function type and calling convention.
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
      static_assert(std::is_function_v<Func>);
      static_assert(sizeof(Func *) == sizeof(FARPROC));
      return std::bit_cast<Func *>(found->second)(args...);
    }

  private:
    const library_t library;  ///< Keep the driver module loaded while sessions use its interfaces.
    const interfaces_t interfaces;  ///< Immutable function table resolved at initialization.
  };
}  // namespace nvprefs
