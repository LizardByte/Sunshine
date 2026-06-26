/**
 * @file src/nvenc/nvenc_colorspace.h
 * @brief Declarations for NVENC YUV colorspace.
 */
#pragma once

// lib includes
#include <ffnvcodec/nvEncodeAPI.h>

namespace nvenc {

  /**
   * @brief YUV colorspace and color range.
   */
  struct nvenc_colorspace_t {
    NV_ENC_VUI_COLOR_PRIMARIES primaries;  ///< Color primaries written into NVENC VUI metadata.
    NV_ENC_VUI_TRANSFER_CHARACTERISTIC tranfer_function;  ///< Transfer function written into NVENC VUI metadata.
    NV_ENC_VUI_MATRIX_COEFFS matrix;  ///< YUV matrix coefficients written into NVENC VUI metadata.
    bool full_range;  ///< Whether the video range is full-range instead of limited.
  };

}  // namespace nvenc
