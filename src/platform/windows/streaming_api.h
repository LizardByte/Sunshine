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
#include <functional>
#include <memory>

namespace platf {
  /**
   * @brief Injectable loader for optional Windows networking libraries.
   */
  struct networking_loader_t {
    std::function<std::shared_ptr<void>(const char *)> open;  ///< Own a loaded library.
    std::function<void *(void *, const char *)> resolve;  ///< Resolve an exported function.
  };

  /**
   * @brief Build a loader restricted to Windows system libraries.
   * @return System loader.
   */
  inline networking_loader_t system_networking_loader() {
    return {
      [](const char *name) -> std::shared_ptr<void> {
        auto library = LoadLibraryExA(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) {
          return {};
        }
        return {library, [](void *library) {
                  FreeLibrary(static_cast<HMODULE>(library));
                }};
      },
      [](void *library, const char *name) {
        return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(library), name));
      },
    };
  }

  /**
   * @brief Immutable WLAN exports retaining their library.
   */
  struct wlan_api_t {
    std::shared_ptr<void> library;  ///< Loaded WLAN library.
    decltype(WlanOpenHandle) *open {};  ///< Open a WLAN client.
    decltype(WlanCloseHandle) *close {};  ///< Close a WLAN client.
    decltype(WlanFreeMemory) *free_memory {};  ///< Free discovery results.
    decltype(WlanEnumInterfaces) *enumerate {};  ///< Enumerate network interfaces.
    decltype(WlanSetInterface) *set {};  ///< Set interface options.

    /**
     * @brief Load a complete WLAN table.
     * @param loader Library operations.
     * @return Validated API, or nullptr.
     */
    static std::shared_ptr<const wlan_api_t> load(const networking_loader_t &loader = system_networking_loader()) {
      auto api = std::make_shared<wlan_api_t>();
      api->library = loader.open("wlanapi.dll");
      if (!api->library) {
        return {};
      }
      api->open = reinterpret_cast<decltype(open)>(loader.resolve(api->library.get(), "WlanOpenHandle"));
      api->close = reinterpret_cast<decltype(close)>(loader.resolve(api->library.get(), "WlanCloseHandle"));
      api->free_memory = reinterpret_cast<decltype(free_memory)>(loader.resolve(api->library.get(), "WlanFreeMemory"));
      api->enumerate = reinterpret_cast<decltype(enumerate)>(loader.resolve(api->library.get(), "WlanEnumInterfaces"));
      api->set = reinterpret_cast<decltype(set)>(loader.resolve(api->library.get(), "WlanSetInterface"));
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
    std::shared_ptr<void> library;  ///< Loaded QWAVE library.
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
     * @param loader Library operations.
     * @return Complete owner, or nullptr.
     */
    static std::shared_ptr<const qwave_api_t> load(const networking_loader_t &loader = system_networking_loader()) {
      auto api = std::make_shared<qwave_api_t>();
      api->library = loader.open("qwave.dll");
      if (!api->library) {
        return {};
      }
      api->create = reinterpret_cast<decltype(create)>(loader.resolve(api->library.get(), "QOSCreateHandle"));
      api->close = reinterpret_cast<decltype(close)>(loader.resolve(api->library.get(), "QOSCloseHandle"));
      api->add = reinterpret_cast<decltype(add)>(loader.resolve(api->library.get(), "QOSAddSocketToFlow"));
      api->remove = reinterpret_cast<decltype(remove)>(loader.resolve(api->library.get(), "QOSRemoveSocketFromFlow"));
      if (!api->create || !api->close || !api->add || !api->remove) {
        return {};
      }
      QOS_VERSION version {1, 0};
      if (!api->create(&version, &api->handle)) {
        return {};
      }
      return api;
    }
  };
}  // namespace platf
