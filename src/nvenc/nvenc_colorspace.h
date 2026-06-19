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
    NV_ENC_VUI_COLOR_PRIMARIES primaries;
    NV_ENC_VUI_TRANSFER_CHARACTERISTIC tranfer_function;
    NV_ENC_VUI_MATRIX_COEFFS matrix;
    bool full_range;
  };

}  // namespace nvenc
