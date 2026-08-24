/**
 * @file src/platform/windows/nvprefs/undo_file.h
 * @brief Declarations for the nvidia undo file.
 */
#pragma once

// standard includes
#include <filesystem>

// local includes
#include "nvprefs_common.h"
#include "undo_data.h"

namespace nvprefs {

  /**
   * @brief File-backed storage for NVIDIA preference undo data.
   */
  class undo_file_t {
  public:
    /**
     * @brief Open existing file.
     *
     * @param file_path File path.
     * @param access_denied Access denied.
     * @return Open undo file wrapper, or std::nullopt when the file cannot be opened.
     */
    static std::optional<undo_file_t> open_existing_file(std::filesystem::path file_path, bool &access_denied);

    /**
     * @brief Create new file.
     *
     * @param file_path File path.
     * @return Created new file object or status.
     */
    static std::optional<undo_file_t> create_new_file(std::filesystem::path file_path);

    /**
     * @brief Delete the persisted NVIDIA settings undo file.
     *
     * @return True when the undo-file operation succeeds.
     */
    bool delete_file();

    /**
     * @brief Write undo data.
     *
     * @param undo_data Driver settings to persist for a later restore.
     * @return True when the undo-file operation succeeds.
     */
    bool write_undo_data(const undo_data_t &undo_data);

    /**
     * @brief Read undo data.
     *
     * @return Parsed undo data, or std::nullopt when the file is empty or invalid.
     */
    std::optional<undo_data_t> read_undo_data();

  private:
    undo_file_t() = default;
    safe_handle file_handle;
  };

}  // namespace nvprefs
