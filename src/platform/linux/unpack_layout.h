/**
 * @file src/platform/linux/unpack_layout.h
 * @brief Source row layout classification for packed 4-byte image uploads.
 */
#pragma once

// standard includes
#include <cstdint>
#include <optional>

// local includes
#include "src/platform/common.h"

namespace egl {
  /**
   * @brief Classify how the source rows of a packed 4-byte image must be uploaded.
   *
   * GL_UNPACK_ROW_LENGTH is expressed in pixels, so only strides that are a
   * multiple of the 4-byte pixel size can be described directly. Strides
   * smaller than one row or not aligned to the pixel size cannot describe a
   * complete frame and are rejected instead of being sampled at the wrong
   * offsets.
   *
   * @note The Linux capture path uses a desktop OpenGL context, where
   * GL_UNPACK_ROW_LENGTH is core.
   *
   * @param width Image width in pixels.
   * @param row_pitch Bytes between consecutive image rows.
   * @return GL_UNPACK_ROW_LENGTH value in pixels, or empty when the stride cannot describe a complete packed row.
   */
  [[nodiscard]] inline std::optional<int> classify_unpack_layout(std::int32_t width, std::int32_t row_pitch) {
    if (!platf::packed_4byte_row_layout_valid(width, row_pitch)) {
      return std::nullopt;
    }

    return row_pitch == static_cast<std::int64_t>(width) * 4 ? 0 : row_pitch / 4;
  }

  /**
   * @brief Validation result for a RAM capture upload.
   */
  enum class ram_upload_e {
    direct,  ///< The image can be uploaded directly.
    unsupported_format,  ///< The image is not packed 8-bit BGR.
    unsupported_stride  ///< The row pitch cannot describe a complete packed row.
  };

  /**
   * @brief Validation of a capture image for the RAM upload path.
   */
  struct ram_upload_t {
    ram_upload_e kind;  ///< How the image may be uploaded.
    int row_length {};  ///< GL_UNPACK_ROW_LENGTH value in pixels.
  };

  /**
   * @brief Validate a capture image for the RAM upload path.
   *
   * @param img Capture image to validate.
   * @return Classification of the image for the RAM upload path.
   */
  [[nodiscard]] inline ram_upload_t classify_ram_upload(const platf::img_t &img) {
    using enum ram_upload_e;

    // Every consumer reads 4 bytes per pixel, so the declared or derived size
    // must say 4 as well instead of trusting the format name alone.
    if (!platf::is_bgr_capture_image(img) || img.bytes_per_pixel() != 4) {
      return {unsupported_format};
    }
    const auto row_length = classify_unpack_layout(img.width, img.row_pitch);
    if (!row_length.has_value()) {
      return {unsupported_stride};
    }
    return {direct, *row_length};
  }
}  // namespace egl
