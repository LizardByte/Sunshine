/**
 * @file src/platform/linux/vaapi.h
 * @brief Declarations for VA-API hardware accelerated capture.
 */
#pragma once

// local includes
#include "misc.h"
#include "src/platform/common.h"

namespace egl {
  struct surface_descriptor_t;
}

namespace va {
  /**
   * Width --> Width of the image
   * Height --> Height of the image
   * offset_x --> Horizontal offset of the image in the texture
   * offset_y --> Vertical offset of the image in the texture
   * file_t card --> The file descriptor of the render device used for encoding
   *
   * @param width Frame or display width in pixels.
   * @param height Frame or display height in pixels.
   * @param vram Whether the image should use GPU memory instead of system memory.
   * @return Constructed AVCodec encode device object.
   */
  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, bool vram);
  /**
   * @brief Create AVCodec encode device.
   *
   * @param width Frame or display width in pixels.
   * @param height Frame or display height in pixels.
   * @param offset_x Offset x.
   * @param offset_y Offset y.
   * @param vram Whether the image should use GPU memory instead of system memory.
   * @return Constructed AVCodec encode device object.
   */
  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, int offset_x, int offset_y, bool vram);
  /**
   * @brief Create AVCodec encode device.
   *
   * @param width Frame or display width in pixels.
   * @param height Frame or display height in pixels.
   * @param card Video device path or render node used for VAAPI.
   * @param offset_x Offset x.
   * @param offset_y Offset y.
   * @param vram Whether the image should use GPU memory instead of system memory.
   * @return Constructed AVCodec encode device object.
   */
  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, file_t &&card, int offset_x, int offset_y, bool vram);

  // Ensure the render device pointed to by fd is capable of encoding h264 with the hevc_mode configured
  /**
   * @brief Validate that the configured VAAPI device can be used.
   *
   * @param fd Native file descriptor to wrap or inspect.
   * @return True when the VAAPI device is usable for capture or encode.
   */
  bool validate(int fd);
}  // namespace va
