/**
 * @file src/display_device.h
 * @brief Declarations for display device handling.
 */
#pragma once

// lib includes
#include <memory>

// forward declarations
namespace platf {
  class deinit_t;
}  // namespace platf

namespace display_device {
  /**
   * @brief Initialize the implementation and perform the initial state recovery (if needed).
   * @returns A deinit_t instance that performs cleanup when destroyed.
   *
   * @examples
   * const auto init_guard { display_device::init() };
   * @examples_end
   */
  std::unique_ptr<platf::deinit_t>
  init();

  /**
   * @brief Map the output name to a specific display.
   * @param output_name The user-configurable output name.
   * @returns Mapped display name or empty string if the output name could not be mapped.
   *
   * @examples
   * const auto mapped_name_config { map_output_name(config::video.output_name) };
   * const auto mapped_name_custom { map_output_name("{some-device-id}") };
   * @examples_end
   */
  std::string
  map_output_name(const std::string &output_name);
}  // namespace display_device
