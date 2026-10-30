/**
 * @file src/platform/linux/x11_layout.h
 * @brief X11 visual and pixmap format classification for capture.
 */
#pragma once

// standard includes
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

// local includes
#include "src/platform/pix_fmt_e.h"

namespace platf::x11 {
  /**
   * @brief X11 visual properties needed to declare a capture pixel format.
   */
  struct x11_visual_t {
    int depth {};  ///< Visual depth in bits.
    int bits_per_pixel {};  ///< Bits used to store one pixel.
    bool little_endian {};  ///< True when the server byte order is LSBFirst.
    bool true_color {};  ///< True when the visual class is TrueColor; DirectColor and pseudo-color visuals expose the same masks but interpret pixels as colormap indices.
    std::uint64_t red_mask {};  ///< Visual red channel mask.
    std::uint64_t green_mask {};  ///< Visual green channel mask.
    std::uint64_t blue_mask {};  ///< Visual blue channel mask.
  };

  /**
   * @brief Map an X11 TrueColor layout to a declared capture pixel format.
   *
   * The pixel size alone does not identify the layout: a 30-bit visual also
   * occupies 4 bytes per pixel without being packed BGR0. The canonical 24-bit
   * BGRX layout in LSBFirst byte order is supported, and so is the 32-bit
   * variant whose fourth byte carries an alpha channel that packed BGR0
   * ignores. Every other layout (16-bit, 30-bit, swapped channels, MSBFirst or
   * non-TrueColor) is rejected so the capture backend fails at init instead of
   * decoding the buffer with a wrong format.
   *
   * @param visual Visual properties to classify.
   * @return Declared capture pixel format, or empty when the layout is unsupported.
   */
  [[nodiscard]] inline std::optional<platf::pix_fmt_e> layout_to_pix_fmt(const x11_visual_t &visual) {
    if (!visual.true_color || !visual.little_endian || visual.bits_per_pixel != 32) {
      return std::nullopt;
    }

    if ((visual.depth == 24 || visual.depth == 32) && visual.red_mask == 0xFF0000 && visual.green_mask == 0xFF00 && visual.blue_mask == 0xFF) {
      return platf::pix_fmt_e::bgr0;
    }

    return std::nullopt;
  }

  /**
   * @brief Select the bits per pixel used by the server for a visual depth.
   *
   * The visual depth and the storage size per pixel are independent
   * properties, so the storage size must be looked up in the server's pixmap
   * formats instead of being assumed. When the server lists several formats
   * for the same depth, the first entry is used.
   *
   * @param formats Supported pixmap formats as (depth, bits per pixel) pairs.
   * @param depth Visual depth to look up.
   * @return Bits per pixel, or empty when the depth is not supported.
   */
  [[nodiscard]] inline std::optional<int> pixmap_format_bits_per_pixel(std::span<const std::pair<int, int>> formats, int depth) {
    for (const auto &[format_depth, bits_per_pixel] : formats) {
      if (format_depth == depth) {
        return bits_per_pixel;
      }
    }

    return std::nullopt;
  }
}  // namespace platf::x11
