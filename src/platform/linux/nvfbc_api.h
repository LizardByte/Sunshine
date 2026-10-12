/**
 * @file src/platform/linux/nvfbc_api.h
 * @brief Immutable NvFBC API ownership for capture sessions.
 */
#pragma once

// local includes
#include "dynamic_library.h"
#include "src/logging.h"
#ifndef DOXYGEN
  #include "third-party/nvfbc/NvFBC.h"
#endif

namespace cuda::nvfbc {
  /**
   * @brief Immutable NvFBC function table that keeps its implementing library loaded.
   */
  class api_t {
  public:
    /**
     * @brief Load and validate the API for a new capture or enumeration owner.
     *
     * @return Immutable API, or nullptr when the library or API is unavailable.
     */
    static std::shared_ptr<const api_t> load() {
      return load(dyn::system_loader());
    }

    /**
     * @brief Load an NvFBC table through injected library-loading operations.
     *
     * @param loader Loading operations for this initialization attempt.
     * @return Immutable API, or nullptr when loading or function-table creation fails.
     */
    static std::shared_ptr<const api_t> load(const dyn::loader_t &loader) {
      auto library = loader.open({"libnvidia-fbc.so.1", "libnvidia-fbc.so"});
      if (!library) {
        return {};
      }

      PNVFBCCREATEINSTANCE create_instance {};
      if (const std::vector<std::tuple<dyn::apiproc *, const char *>> symbols {
            {(dyn::apiproc *) &create_instance, "NvFBCCreateInstance"},
          };
          loader.resolve(library.get(), symbols) || !create_instance) {
        return {};
      }

      NVFBC_API_FUNCTION_LIST functions {.dwVersion = NVFBC_VERSION};
      if (create_instance(&functions)) {
        logging::error("Unable to create NvFBC instance");
        return {};
      }

      if (!functions.nvFBCCreateHandle || !functions.nvFBCDestroyHandle || !functions.nvFBCGetStatus || !functions.nvFBCGetLastErrorStr || !functions.nvFBCBindContext || !functions.nvFBCReleaseContext || !functions.nvFBCCreateCaptureSession || !functions.nvFBCDestroyCaptureSession || !functions.nvFBCToCudaSetUp || !functions.nvFBCToCudaGrabFrame) {
        logging::error("NvFBC function table is incomplete");
        return {};
      }
      return std::shared_ptr<const api_t>(new api_t(std::move(library), functions));
    }

    const NVFBC_API_FUNCTION_LIST functions;  ///< Validated functions used by NvFBC sessions.

  private:
    /**
     * @brief Publish a fully initialized function table with its library owner.
     *
     * @param library Library implementing the table.
     * @param functions Functions returned by NvFBCCreateInstance.
     */
    api_t(std::shared_ptr<dyn::library_handle_t> library, const NVFBC_API_FUNCTION_LIST &functions):
        functions {functions},
        library {std::move(library)} {
    }

    const std::shared_ptr<dyn::library_handle_t> library;  ///< Keep the API loaded until all sessions are destroyed.
  };

}  // namespace cuda::nvfbc
