/**
 * @file src/platform/linux/dynamic_library.h
 * @brief Owning handles for optional Linux capture libraries.
 */
#pragma once

// standard includes
#include <dlfcn.h>
#include <functional>
#include <memory>

// local includes
#include "misc.h"

namespace dyn {
  /**
   * @brief Symbol destinations used while constructing an immutable API table.
   */
  using symbols_t = std::vector<std::tuple<apiproc *, const char *>>;

  /**
   * @brief Loading operations supplied to optional-library factories.
   */
  struct loader_t {
    std::function<std::shared_ptr<void>(const std::vector<const char *> &)> open;  ///< Open a library owner.
    std::function<int(void *, const symbols_t &)> resolve;  ///< Resolve all required symbols into a local table.
  };

  /**
   * @brief Open an optional library and retain it until its last owner is released.
   *
   * @param names Library names to try in order.
   * @return Shared library owner, or nullptr when none of the names can be loaded.
   */
  inline std::shared_ptr<void> open(const std::vector<const char *> &names) {
    auto library = handle(names);
    if (!library) {
      return {};
    }
    return {library, [](void *value) {
              dlclose(value);
            }};
  }

  /**
   * @brief Return the platform loader used by production API owners.
   *
   * @return Operations that use the existing library-name fallback and strict symbol lookup.
   */
  inline loader_t system_loader() {
    return {
      open,
      [](void *library, const symbols_t &symbols) {
        return load(library, symbols);
      },
    };
  }
}  // namespace dyn
