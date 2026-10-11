/**
 * @file src/platform/windows/nvprefs/nvapi_opensource_wrapper.cpp
 * @brief Definitions for the instance-owned NVAPI wrapper.
 */
// local includes
#include "nvapi_opensource_wrapper.h"

#include "nvprefs_common.h"

// special nvapi header that should be the last include
#include <nvapi_interface.h>

namespace nvprefs {
  std::shared_ptr<const api_t> api_t::load() {
    const loader_t loader {
      [](const char *name) -> std::shared_ptr<void> {
        auto module = LoadLibraryEx(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) {
          return {};
        }
        return {module, [](void *value) {
                  FreeLibrary(static_cast<HMODULE>(value));
                }};
      },
      [](void *module, const char *name) {
        return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(module), name));
      },
    };
    return load(loader);
  }

  std::shared_ptr<const api_t> api_t::load(const loader_t &loader) {
#if defined(_M_ARM64) || defined(__aarch64__)
    const auto dll_name = "nvapia64.dll";
#elif defined(_WIN64)
    const auto dll_name = "nvapi64.dll";
#else
    const auto dll_name = "nvapi.dll";
#endif
    auto library = loader.open(dll_name);
    if (!library) {
      return {};
    }

    using query_interface_fn = void *(__cdecl *) (NvU32);
    auto query_interface = reinterpret_cast<query_interface_fn>(loader.symbol(library.get(), "nvapi_QueryInterface"));
    if (!query_interface) {
      return {};
    }

    interfaces_t interfaces;
    for (const auto &item : nvapi_interface_table) {
      interfaces.emplace(item.func, query_interface(item.id));
    }
    return std::make_shared<const api_t>(std::move(library), std::move(interfaces));
  }
}  // namespace nvprefs
