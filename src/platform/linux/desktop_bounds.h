/**
 * @file src/platform/linux/desktop_bounds.h
 * @brief Bounding box of a Linux desktop's monitors, for absolute input.
 */
#pragma once

// standard includes
#include <algorithm>
#include <vector>

// local includes
#include "src/platform/common.h"

namespace platf {
  /**
   * @brief Origin and size of the box that holds every monitor.
   */
  struct desktop_bounds_t {
    int offset_x;  ///< Left edge of the leftmost monitor, in desktop coordinates.
    int offset_y;  ///< Top edge of the topmost monitor, in desktop coordinates.
    int width;  ///< Width in physical pixels.
    int height;  ///< Height in physical pixels.
    int logical_width;  ///< Width after display scaling.
    int logical_height;  ///< Height after display scaling.
  };

  /**
   * @brief Bounds of the monitors' viewports.
   *
   * Compositors such as Hyprland map an absolute pointer across the box that holds every monitor,
   * starting at the leftmost and topmost edges. A monitor left of or above the layout origin has a
   * negative offset, so the box starts there rather than at zero.
   *
   * @param viewports Each monitor's offset and its physical and logical size.
   * @return The bounds, or all zeros when there are no monitors.
   */
  inline desktop_bounds_t desktop_bounds(const std::vector<touch_port_t> &viewports) {
    if (viewports.empty()) {
      return {};
    }

    int left = viewports.front().offset_x;
    int top = viewports.front().offset_y;
    int right = left;
    int bottom = top;
    int logical_right = left;
    int logical_bottom = top;
    bool logical = false;
    for (const auto &viewport : viewports) {
      left = std::min(left, viewport.offset_x);
      top = std::min(top, viewport.offset_y);
      right = std::max(right, viewport.offset_x + viewport.width);
      bottom = std::max(bottom, viewport.offset_y + viewport.height);
      logical_right = std::max(logical_right, viewport.offset_x + viewport.logical_width);
      logical_bottom = std::max(logical_bottom, viewport.offset_y + viewport.logical_height);
      logical = logical || (viewport.logical_width > 0 && viewport.logical_height > 0);
    }

    // Without any logical size the environment has none either, as callers expect.
    return {
      left,
      top,
      right - left,
      bottom - top,
      logical ? logical_right - left : 0,
      logical ? logical_bottom - top : 0,
    };
  }
}  // namespace platf
