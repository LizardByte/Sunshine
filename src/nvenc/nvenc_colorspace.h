/**
 * @file src/nvenc/nvenc_colorspace.h
 * @brief Declarations for base NVENC colorspace.
 */
#pragma once

#include <ffnvcodec/nvEncodeAPI.h>

namespace nvenc {
  struct nvenc_colorspace_t {
    NV_ENC_VUI_COLOR_PRIMARIES primaries;
    NV_ENC_VUI_TRANSFER_CHARACTERISTIC tranfer_function;
    NV_ENC_VUI_MATRIX_COEFFS matrix;
    bool full_range;
  };
}  // namespace nvenc
