/**
 * @file src/platform/linux/capture_geometry.h
 * @brief Desktop bounds computed from a capture resource's discovered outputs.
 */
#pragma once
#include "src/platform/common.h"

#include <algorithm>

namespace platf {
  /**
   * @brief Physical and logical desktop extents relative to the desktop origin.
   */
  struct capture_geometry_t {
    int width {};  ///< Physical right edge.
    int height {};  ///< Physical bottom edge.
    int logical_width {};  ///< Logical right edge.
    int logical_height {};  ///< Logical bottom edge.

    /**
     * @brief Extend this snapshot with one discovered output.
     * @param viewport Output position and physical/logical sizes.
     */
    void include(const touch_port_t &viewport) {
      width = std::max(width, viewport.offset_x + viewport.width);
      height = std::max(height, viewport.offset_y + viewport.height);
      logical_width = std::max(logical_width, viewport.offset_x + viewport.logical_width);
      logical_height = std::max(logical_height, viewport.offset_y + viewport.logical_height);
    }
  };
}  // namespace platf
