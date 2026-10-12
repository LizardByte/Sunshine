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
   * @brief Opaque token identifying a native dynamic-library handle.
   *
   * The token is never dereferenced. Only the platform loader converts it to the native loader representation.
   */
  struct library_handle_t;

  /**
   * @brief Symbol destinations used while constructing an immutable API table.
   */
  using symbols_t = std::vector<std::tuple<apiproc *, const char *>>;

  /**
   * @brief Loading operations supplied to optional-library factories.
   */
  struct loader_t {
    std::function<std::shared_ptr<library_handle_t>(const std::vector<const char *> &)> open;  ///< Open a library owner.
    std::function<int(library_handle_t *, const symbols_t &)> resolve;  ///< Resolve all required symbols into a local table.
  };

  /**
   * @brief Open an optional library and retain it until its last owner is released.
   *
   * @param names Library names to try in order.
   * @return Shared library owner, or nullptr when none of the names can be loaded.
   */
  inline std::shared_ptr<library_handle_t> open(const std::vector<const char *> &names) {
    auto library = static_cast<library_handle_t *>(handle(names));
    if (!library) {
      return {};
    }
    return {library, [](library_handle_t *value) {
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
      [](library_handle_t *library, const symbols_t &symbols) {
        return load(library, symbols);
      },
    };
  }
}  // namespace dyn
