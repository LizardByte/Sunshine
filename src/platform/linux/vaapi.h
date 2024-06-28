/**
 * @file src/platform/linux/vaapi.h
 * @brief Declarations for VA-API hardware accelerated capture.
 */
#pragma once

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
   */
  std::unique_ptr<platf::avcodec_encode_device_t>
  make_avcodec_encode_device(int width, int height, bool vram);
  std::unique_ptr<platf::avcodec_encode_device_t>
  make_avcodec_encode_device(int width, int height, int offset_x, int offset_y, bool vram);
  std::unique_ptr<platf::avcodec_encode_device_t>
  make_avcodec_encode_device(int width, int height, file_t &&card, int offset_x, int offset_y, bool vram);

  // Ensure the render device pointed to by fd is capable of encoding h264 with the hevc_mode configured
  bool
  validate(int fd);
}  // namespace va
