/**
 * @file src/platform/linux/vulkan_encode.h
 * @brief Declarations for FFmpeg Vulkan Video encoder.
 */
#pragma once

#include "src/platform/common.h"

#include <cstdint>
#include <map>
#include <vector>

extern "C" struct AVBufferRef;

namespace vk {

  /**
   * @brief Query DRM format modifiers supported by the Vulkan driver for common capture formats.
   *
   * This queries the Vulkan driver for modifiers it can import via VK_EXT_image_drm_format_modifier.
   * The returned map is keyed by DRM fourcc format code (e.g. DRM_FORMAT_ARGB8888).
   *
   * @return Map of DRM format to supported modifiers, or empty map if query fails.
   */
  std::map<std::uint32_t, std::vector<std::uint64_t>> get_supported_capture_modifiers();

  /**
   * @brief Initialize Vulkan hardware device for FFmpeg encoding.
   * @param encode_device The encode device (vk_t).
   * @param hw_device_buf Output hardware device buffer.
   * @return 0 on success, negative on error.
   */
  int vulkan_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *encode_device, AVBufferRef **hw_device_buf);

  /**
   * @brief Create a Vulkan encode device for RAM capture.
   *
   * @param width Frame or display width in pixels.
   * @param height Frame or display height in pixels.
   * @return Constructed AVCodec encode device ram object.
   */
  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device_ram(int width, int height);

  /**
   * @brief Create a Vulkan encode device for VRAM capture.
   *
   * @param width Frame or display width in pixels.
   * @param height Frame or display height in pixels.
   * @param offset_x Offset x.
   * @param offset_y Offset y.
   * @return Constructed AVCodec encode device VRAM object.
   */
  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device_vram(int width, int height, int offset_x, int offset_y);

  /**
   * @brief Check if FFmpeg Vulkan Video encoding is available.
   *
   * @return True when FFmpeg Vulkan Video encoding is available.
   */
  bool validate();

}  // namespace vk
