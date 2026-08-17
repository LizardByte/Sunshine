/**
 * @file src/nvenc/nvenc_shared_dll.h
 * @brief Windows module lifetime helpers shared by NVENC factories and encoders.
 */
#pragma once
#ifdef _WIN32

  // standard includes
  #include <memory>
  #include <type_traits>

  // platform includes
  #include <Windows.h>

namespace nvenc {

  /**
   * @brief Shared ownership wrapper for a loaded Windows module.
   */
  using shared_dll = std::shared_ptr<std::remove_pointer_t<HMODULE>>;

  /**
   * @brief Release a Windows module when its final shared owner is destroyed.
   */
  struct shared_dll_deleter {
    /**
     * @brief Release the module.
     *
     * @param dll Module handle to release.
     */
    void operator()(HMODULE dll) const {
      if (dll) {
        FreeLibrary(dll);
      }
    }
  };

  /**
   * @brief Wrap a Windows module handle in shared ownership.
   *
   * @param dll Module handle returned by `LoadLibraryEx()`.
   * @return Shared module handle.
   */
  inline shared_dll make_shared_dll(HMODULE dll) {
    return shared_dll(dll, shared_dll_deleter {});
  }

}  // namespace nvenc
#endif
