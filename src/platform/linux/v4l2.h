/**
 * @file src/platform/linux/v4l2.h
 * @brief Declarations for V4L2 M2M zero-copy encode devices.
 */
#pragma once

// local includes
#include "misc.h"
#include "src/platform/common.h"

namespace v4l2 {
  /**
   * @brief Create a V4L2 encode device using the automatically selected render device.
   *
   * @param width Input image width in pixels.
   * @param height Input image height in pixels.
   * @param vram Whether input frames are DMA-BUF-backed VRAM images.
   * @return Initialized encode device, or nullptr when initialization fails.
   */
  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, bool vram);

  /**
   * @brief Create an offset-aware V4L2 encode device using the automatically selected render device.
   *
   * @param width Input image width in pixels.
   * @param height Input image height in pixels.
   * @param offset_x Horizontal offset of the input image within its texture.
   * @param offset_y Vertical offset of the input image within its texture.
   * @param vram Whether input frames are DMA-BUF-backed VRAM images.
   * @return Initialized encode device, or nullptr when initialization fails.
   */
  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, int offset_x, int offset_y, bool vram);

  /**
   * @brief Create a V4L2 encode device using an open render-device descriptor.
   *
   * @param width Input image width in pixels.
   * @param height Input image height in pixels.
   * @param card Render-device file descriptor whose ownership is transferred to the encode device.
   * @param offset_x Horizontal offset of the input image within its texture.
   * @param offset_y Vertical offset of the input image within its texture.
   * @param vram Whether input frames are DMA-BUF-backed VRAM images.
   * @return Initialized encode device, or nullptr when initialization fails.
   */
  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, file_t &&card, int offset_x, int offset_y, bool vram);
}  // namespace v4l2
