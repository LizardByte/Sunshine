/**
 * @file src/platform/windows/nvprefs/driver_settings.h
 * @brief Declarations for nvidia driver settings.
 */
#pragma once

// local includes first so standard library headers are pulled in before nvapi's SAL macros
#include "nvapi_opensource_wrapper.h"
#include "undo_data.h"

namespace nvprefs {

  /**
   * @brief NVIDIA driver profile settings loaded for inspection or modification.
   */
  class driver_settings_t {
  public:
    /**
     * @brief Construct an empty driver-settings owner.
     */
    driver_settings_t() = default;
    driver_settings_t(const driver_settings_t &) = delete;
    driver_settings_t &operator=(const driver_settings_t &) = delete;

    /**
     * @brief Destroy the session before releasing the driver library.
     */
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
    std::shared_ptr<const api_t> api;  ///< API owner retained until the DRS session is destroyed.
    NvDRSSessionHandle session_handle = nullptr;  ///< Native driver-settings session owned by this instance.
  };

}  // namespace nvprefs
