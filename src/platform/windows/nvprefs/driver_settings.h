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

  class driver_settings_t {
  public:
    ~driver_settings_t();

    bool init();

    void destroy();

    bool load_settings();

    bool save_settings();

    bool restore_global_profile_to_undo(const undo_data_t &undo_data);

    bool check_and_modify_global_profile(std::optional<undo_data_t> &undo_data);

    bool check_and_modify_application_profile(bool &modified);

  private:
    NvDRSSessionHandle session_handle = nullptr;
  };

}  // namespace nvprefs
