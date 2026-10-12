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
      [](const char *name) -> library_t {
        auto module = LoadLibraryEx(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) {
          return {};
        }
        return {module, [](HMODULE value) {
                  FreeLibrary(value);
                }};
      },
      [](HMODULE module, const char *name) {
        return adapt_query(GetProcAddress(module, name));
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

    auto query_interface = loader.query(library.get(), "nvapi_QueryInterface");
    if (!query_interface) {
      return {};
    }

    interfaces_t interfaces;
    for (const auto &item : nvapi_interface_table) {
      interfaces.emplace(item.func, query_interface(item.id));
    }
    return std::make_shared<const api_t>(std::move(library), std::move(interfaces));
  }

  api_t::query_t api_t::adapt_query(FARPROC address) {
    // NVAPI's native void pointer carries a flat Windows function address, not an object.
    static_assert(sizeof(native_query_t) == sizeof(FARPROC));
    static_assert(sizeof(FARPROC) == sizeof(void *));
    if (!address) {
      return {};
    }
    return [query = std::bit_cast<native_query_t>(address)](NvU32 id) {
      return std::bit_cast<FARPROC>(query(id));
    };
  }
}  // namespace nvprefs
