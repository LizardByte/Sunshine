/**
 * @file src/platform/windows/nvprefs/nvprefs_interface.h
 * @brief Declarations for nvidia preferences interface.
 */
#pragma once

// standard includes
#include <memory>

namespace nvprefs {

  /**
   * @brief High-level NVIDIA profile preferences interface with undo support.
   */
  class nvprefs_interface {
  public:
    nvprefs_interface();
    ~nvprefs_interface();

    /**
     * @brief Load persisted state from its backing store.
     *
     * @return True when the NVIDIA preferences operation succeeds.
     */
    bool load();

    /**
     * @brief Release loaded NVIDIA preference state.
     */
    void unload();

    /**
     * @brief Restore NVIDIA settings from the undo file, then remove it.
     *
     * @return True when the NVIDIA preferences operation succeeds.
     */
    bool restore_from_and_delete_undo_file_if_exists();

    /**
     * @brief Apply Sunshine-specific NVIDIA application profile changes.
     *
     * @return True when the NVIDIA preferences operation succeeds.
     */
    bool modify_application_profile();

    /**
     * @brief Apply Sunshine-specific NVIDIA global profile changes.
     *
     * @return True when the NVIDIA preferences operation succeeds.
     */
    bool modify_global_profile();

    /**
     * @brief Check whether this interface owns the active undo file.
     *
     * @return True when the NVIDIA preferences operation succeeds.
     */
    bool owning_undo_file();

    /**
     * @brief Restore NVIDIA global profile settings from saved undo data.
     *
     * @return True when the NVIDIA preferences operation succeeds.
     */
    bool restore_global_profile();

  private:
    struct impl;
    std::unique_ptr<impl> pimpl;
  };

}  // namespace nvprefs
