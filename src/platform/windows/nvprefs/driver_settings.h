/**
 * @file src/platform/windows/nvprefs/driver_settings.h
 * @brief Declarations for nvidia driver settings.
 */
#pragma once

// local includes first so standard library headers are pulled in before nvapi's SAL macros
#include "undo_data.h"

// nvapi headers
// disable clang-format header reordering
// as <NvApiDriverSettings.h> needs types from <nvapi.h>
// clang-format off

// With GCC/MinGW, nvapi_lite_salend.h (included transitively via nvapi_lite_d3dext.h)
// undefines all SAL annotation macros (e.g. __success, __in, __out, __inout) after
// nvapi_lite_salstart.h had defined them. This leaves NVAPI_INTERFACE and other macros
// that use SAL annotations broken for the rest of nvapi.h. Defining __NVAPI_EMPTY_SAL
// makes nvapi_lite_salend.h a no-op, preserving the SAL macro definitions throughout.
// After nvapi.h, we include nvapi_lite_salend.h explicitly (without __NVAPI_EMPTY_SAL)
// to clean up the SAL macros and prevent them from polluting subsequent includes.
#if defined(__GNUC__)
  #define __NVAPI_EMPTY_SAL
#endif

#include <nvapi.h>
#include <NvApiDriverSettings.h>

#if defined(__GNUC__)
  #undef __NVAPI_EMPTY_SAL
  // Clean up SAL macros that nvapi_lite_salstart.h defined and salend.h was
  // prevented from cleaning up (due to __NVAPI_EMPTY_SAL above).
  #include <nvapi_lite_salend.h>
#endif
// clang-format on

namespace nvprefs {

  /**
   * @brief NVIDIA driver profile settings loaded for inspection or modification.
   */
  class driver_settings_t {
  public:
    ~driver_settings_t();

    /**
     * @brief Load NVIDIA profile settings for the current driver state.
     *
     * @return True when the NVIDIA driver-settings operation succeeds.
     */
    bool init();

    /**
     * @brief Destroy the native resource owned by the wrapper.
     */
    void destroy();

    /**
     * @brief Load settings data from the backing API or store.
     *
     * @return True when the NVIDIA driver-settings operation succeeds.
     */
    bool load_settings();

    /**
     * @brief Save settings data through the backing API or store.
     *
     * @return True when the NVIDIA driver-settings operation succeeds.
     */
    bool save_settings();

    /**
     * @brief Restore global NVIDIA profile settings from undo data.
     *
     * @param undo_data Driver settings captured before Sunshine modified them.
     * @return True when the NVIDIA driver-settings operation succeeds.
     */
    bool restore_global_profile_to_undo(const undo_data_t &undo_data);

    /**
     * @brief Compare and update global NVIDIA profile settings.
     *
     * @param undo_data Driver settings captured before Sunshine modified them.
     * @return True when the NVIDIA driver-settings operation succeeds.
     */
    bool check_and_modify_global_profile(std::optional<undo_data_t> &undo_data);

    /**
     * @brief Compare and update Sunshine NVIDIA application profile settings.
     *
     * @param modified Whether NVIDIA driver settings were changed and need saving.
     * @return True when the NVIDIA driver-settings operation succeeds.
     */
    bool check_and_modify_application_profile(bool &modified);

  private:
    NvDRSSessionHandle session_handle = nullptr;
  };

}  // namespace nvprefs
