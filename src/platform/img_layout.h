/**
 * @file src/platform/img_layout.h
 * @brief Field layout of captured images, shared between host and CUDA code.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstdint>
#include <optional>

// local includes
#include "src/platform/pix_fmt_e.h"

namespace platf {
  /**
   * @brief Field block shared by every captured image representation.
   *
   * Both the host `img_t` and its CUDA mirror derive from this struct, so the
   * field layout is enforced by construction instead of by comment.
   */
  struct img_layout_t {
    std::uint8_t *data {};  ///< Pointer to the captured image buffer.
    std::int32_t width {};  ///< Image width in pixels.
    std::int32_t height {};  ///< Image height in pixels.
    std::int32_t pixel_pitch {};  ///< Bytes per pixel in the image buffer.
    std::int32_t row_pitch {};  ///< Bytes between consecutive image rows.
    pix_fmt_e pixel_format {pix_fmt_e::unknown};  ///< Declared pixel format of the captured image.

    std::optional<std::chrono::steady_clock::time_point> frame_timestamp;  ///< Capture timestamp associated with the frame.
  };
}  // namespace platf
