/**
 * @file src/platform/windows/streaming_api.h
 * @brief Owned optional Windows APIs used by streaming resources.
 */
#pragma once
// clang-format off
#include <WinSock2.h>
#include <Windows.h>
#include <qos2.h>
#include <wlanapi.h>
// clang-format on
#include <bit>
#include <functional>
#include <memory>
#include <type_traits>

namespace platf {
  /**
   * @brief Injectable loader for optional Windows networking libraries.
   */
  struct networking_loader_t {
    /**
     * @brief Shared ownership of the actual Windows module handle type.
     */
    using library_t = std::shared_ptr<std::remove_pointer_t<HMODULE>>;
    std::function<library_t(const char *)> open;  ///< Own a loaded library.
    std::function<FARPROC(HMODULE, const char *)> resolve;  ///< Resolve an exported function.
  };

  /**
   * @brief Build a loader restricted to Windows system libraries.
   * @return System loader.
   */
  inline networking_loader_t system_networking_loader() {
    return {
      [](const char *name) -> networking_loader_t::library_t {
        auto library = LoadLibraryExA(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) {
          return {};
        }
        return {library, [](HMODULE library) {
                  FreeLibrary(library);
                }};
      },
      [](HMODULE library, const char *name) {
        return GetProcAddress(library, name);
      },
    };
  }

  /**
   * @brief Immutable WLAN exports retaining their library.
   */
  struct wlan_api_t {
    networking_loader_t::library_t library;  ///< Loaded WLAN library.
    decltype(WlanOpenHandle) *open {};  ///< Open a WLAN client.
    decltype(WlanCloseHandle) *close {};  ///< Close a WLAN client.
    decltype(WlanFreeMemory) *free_memory {};  ///< Free discovery results.
    decltype(WlanEnumInterfaces) *enumerate {};  ///< Enumerate network interfaces.
    decltype(WlanSetInterface) *set {};  ///< Set interface options.

    /**
     * @brief Load a complete WLAN table.
     * @note Windows AMD64 and ARM64 represent these SDK function pointers as export addresses. The bit copies preserve that ABI; they do not provide a portable function-pointer conversion.
     * @param loader Library operations.
     * @return Validated API, or nullptr.
     */
    static std::shared_ptr<const wlan_api_t> load(const networking_loader_t &loader = system_networking_loader()) {
      auto api = std::make_shared<wlan_api_t>();
      api->library = loader.open("wlanapi.dll");
      if (!api->library) {
        return {};
      }
      api->open = std::bit_cast<decltype(open)>(loader.resolve(api->library.get(), "WlanOpenHandle"));
      api->close = std::bit_cast<decltype(close)>(loader.resolve(api->library.get(), "WlanCloseHandle"));
      api->free_memory = std::bit_cast<decltype(free_memory)>(loader.resolve(api->library.get(), "WlanFreeMemory"));
      api->enumerate = std::bit_cast<decltype(enumerate)>(loader.resolve(api->library.get(), "WlanEnumInterfaces"));
      api->set = std::bit_cast<decltype(set)>(loader.resolve(api->library.get(), "WlanSetInterface"));
      if (!api->open || !api->close || !api->free_memory || !api->enumerate || !api->set) {
        return {};
      }
      return api;
    }
  };

  /**
   * @brief QWAVE handle and validated exports retained by every active flow.
   */
  struct qwave_api_t {
    /**
     * @brief Construct an empty client owner before export validation.
     */
    qwave_api_t() = default;
    /**
     * @brief Native client owners cannot be copied.
     */
    qwave_api_t(const qwave_api_t &) = delete;
    /**
     * @brief Native client owners cannot be assigned.
     */
    qwave_api_t &operator=(const qwave_api_t &) = delete;
    networking_loader_t::library_t library;  ///< Loaded QWAVE library.
    HANDLE handle {};  ///< QWAVE client handle.
    decltype(QOSCreateHandle) *create {};  ///< Create a client handle.
    decltype(QOSCloseHandle) *close {};  ///< Release a client handle.
    decltype(QOSAddSocketToFlow) *add {};  ///< Register socket traffic.
    decltype(QOSRemoveSocketFromFlow) *remove {};  ///< Remove registered traffic.

    /**
     * @brief Release the client before unloading its library.
     */
    ~qwave_api_t() {
      if (handle) {
        close(handle);
      }
    }

    /**
     * @brief Load QWAVE and create a scoped client.
     * @note Windows AMD64 and ARM64 represent these SDK function pointers as export addresses. The bit copies preserve that ABI; they do not provide a portable function-pointer conversion.
     * @param loader Library operations.
     * @return Complete owner, or nullptr.
     */
    static std::shared_ptr<const qwave_api_t> load(const networking_loader_t &loader = system_networking_loader()) {
      auto api = std::make_shared<qwave_api_t>();
      api->library = loader.open("qwave.dll");
      if (!api->library) {
        return {};
      }
      api->create = std::bit_cast<decltype(create)>(loader.resolve(api->library.get(), "QOSCreateHandle"));
      api->close = std::bit_cast<decltype(close)>(loader.resolve(api->library.get(), "QOSCloseHandle"));
      api->add = std::bit_cast<decltype(add)>(loader.resolve(api->library.get(), "QOSAddSocketToFlow"));
      api->remove = std::bit_cast<decltype(remove)>(loader.resolve(api->library.get(), "QOSRemoveSocketFromFlow"));
      if (!api->create || !api->close || !api->add || !api->remove) {
        return {};
      }
      if (QOS_VERSION version {1, 0}; !api->create(&version, &api->handle)) {
        return {};
      }
      return api;
    }
  };
}  // namespace platf
