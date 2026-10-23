/**
 * @file src/platform/macos/sck_capture.h
 * @brief ScreenCaptureKit window and application capture for macOS.
 */
#pragma once

// standard includes
#include <memory>
#include <string>

// local includes
#include "src/platform/common.h"

namespace platf {
  /**
   * @brief Create a ScreenCaptureKit source for the active window or application target.
   *
   * @param hwdevice_type Encoder memory type. System memory uses BGRA; VideoToolbox uses bi-planar YUV.
   * @param display_name Configured display id. Application capture prefers this display when the app has a window there.
   * @param width Stream width requested by the client, in pixels.
   * @param height Stream height requested by the client, in pixels.
   * @param framerate Requested frame rate.
   * @param ten_bit Use 10-bit bi-planar output when the encoder memory is VideoToolbox.
   * @return Capture backend, or nullptr when screen recording is not allowed or the target cannot be described.
   */
  std::shared_ptr<display_t> sck_display(mem_type_e hwdevice_type, const std::string &display_name, int width, int height, int framerate, bool ten_bit);
}  // namespace platf
