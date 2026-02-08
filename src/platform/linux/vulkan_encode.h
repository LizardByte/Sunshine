/**
 * @file src/platform/linux/vulkan_encode.h
 * @brief Declarations for FFmpeg Vulkan Video encoder.
 */
#pragma once

#include "src/platform/common.h"

extern "C" struct AVBufferRef;

namespace vk {

  /**
   * @brief Initialize Vulkan hardware device for FFmpeg encoding.
   * @param encode_device The encode device (vk_t).
   * @param hw_device_buf Output hardware device buffer.
   * @return 0 on success, negative on error.
   */
  int vulkan_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *encode_device, AVBufferRef **hw_device_buf);

  /**
   * @brief Create a Vulkan encode device for RAM capture.
   */
  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device_ram(int width, int height);

  /**
   * @brief Create a Vulkan encode device for VRAM capture.
   */
  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device_vram(int width, int height, int offset_x, int offset_y);

  /**
   * @brief Check if FFmpeg Vulkan Video encoding is available.
   */
  bool validate();

}  // namespace vk
