/**
 * @file src/platform/windows/nvprefs/undo_data.h
 * @brief Declarations for undoing changes to nvidia preferences.
 */
#pragma once

// standard includes
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nvprefs {

  /**
   * @brief Serializable NVIDIA profile state saved before preference changes.
   */
  class undo_data_t {
  public:
    /**
     * @brief NVIDIA profile settings captured for undo.
     */
    struct data_t {
      /**
       * @brief OpenGL swapchain setting values captured from the driver.
       */
      struct opengl_swapchain_t {
        uint32_t our_value;  ///< NVIDIA setting value applied by Sunshine.
        std::optional<uint32_t> undo_value;  ///< Previous NVIDIA setting value to restore, if present.
      };

      std::optional<opengl_swapchain_t> opengl_swapchain;  ///< Opengl swapchain.
    };

    /**
     * @brief Set opengl swapchain.
     *
     * @param our_value NVIDIA setting value applied by Sunshine.
     * @param undo_value Previous NVIDIA setting value to restore, if present.
     */
    void set_opengl_swapchain(uint32_t our_value, std::optional<uint32_t> undo_value);

    /**
     * @brief Get opengl swapchain.
     *
     * @return Stored OpenGL swapchain override and optional restore setting.
     */
    std::optional<data_t::opengl_swapchain_t> get_opengl_swapchain() const;

    /**
     * @brief Serialize undo data to its JSON representation.
     *
     * @return JSON string containing the undo data.
     */
    std::string write() const;

    /**
     * @brief Read persisted data into the current object.
     *
     * @param buffer Serialized byte buffer to read from or write to.
     */
    void read(const std::vector<char> &buffer);

    /**
     * @brief Merge newer undo data with the current data set.
     *
     * @param newer_data Newer data.
     */
    void merge(const undo_data_t &newer_data);

  private:
    data_t data;
  };

}  // namespace nvprefs
