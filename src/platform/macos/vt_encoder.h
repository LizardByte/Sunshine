/**
 * @file src/platform/macos/vt_encoder.h
 * @brief Declarations for the native VideoToolbox encoder on macOS.
 */
#pragma once

// standard includes
#include <functional>
#include <memory>

// local includes
#include "src/platform/common.h"

namespace platf::vt {
  /**
   * @brief Callback that resizes the capture output to the encoder resolution.
   */
  using resolution_fn_t = std::function<void(int width, int height)>;

  /**
   * @brief Callback that selects the capture pixel format.
   */
  using pixel_format_fn_t = std::function<void(int pixel_format)>;

  /**
   * @brief Create a native VideoToolbox encode device bound to the capture display.
   *
   * @param pix_fmt Sunshine pixel format to capture and encode.
   * @param resolution_fn Callback that resizes the capture output.
   * @param pixel_format_fn Callback that selects the capture pixel format; invoked before capture starts.
   * @return Constructed encode device object, or null on failure.
   */
  std::unique_ptr<vt_encode_device_t> make_encode_device(pix_fmt_e pix_fmt, resolution_fn_t resolution_fn, const pixel_format_fn_t &pixel_format_fn);
}  // namespace platf::vt
