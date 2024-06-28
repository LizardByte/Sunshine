/**
 * @file src/nvenc/nvenc_utils.cpp
 * @brief Definitions for base NVENC utilities.
 */
#include <cassert>

#include "nvenc_utils.h"

namespace nvenc {

#ifdef _WIN32
  DXGI_FORMAT
  dxgi_format_from_nvenc_format(NV_ENC_BUFFER_FORMAT format) {
    switch (format) {
      case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
        return DXGI_FORMAT_P010;

      case NV_ENC_BUFFER_FORMAT_NV12:
        return DXGI_FORMAT_NV12;

      default:
        return DXGI_FORMAT_UNKNOWN;
    }
  }
#endif

  NV_ENC_BUFFER_FORMAT
  nvenc_format_from_sunshine_format(platf::pix_fmt_e format) {
    switch (format) {
      case platf::pix_fmt_e::nv12:
        return NV_ENC_BUFFER_FORMAT_NV12;

      case platf::pix_fmt_e::p010:
        return NV_ENC_BUFFER_FORMAT_YUV420_10BIT;

      default:
        return NV_ENC_BUFFER_FORMAT_UNDEFINED;
    }
  }

  nvenc_colorspace_t
  nvenc_colorspace_from_sunshine_colorspace(const video::sunshine_colorspace_t &sunshine_colorspace) {
    nvenc_colorspace_t colorspace;

    switch (sunshine_colorspace.colorspace) {
      case video::colorspace_e::rec601:
        // Rec. 601
        colorspace.primaries = NV_ENC_VUI_COLOR_PRIMARIES_SMPTE170M;
        colorspace.tranfer_function = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_SMPTE170M;
        colorspace.matrix = NV_ENC_VUI_MATRIX_COEFFS_SMPTE170M;
        break;

      case video::colorspace_e::rec709:
        // Rec. 709
        colorspace.primaries = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
        colorspace.tranfer_function = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
        colorspace.matrix = NV_ENC_VUI_MATRIX_COEFFS_BT709;
        break;

      case video::colorspace_e::bt2020sdr:
        // Rec. 2020
        colorspace.primaries = NV_ENC_VUI_COLOR_PRIMARIES_BT2020;
        assert(sunshine_colorspace.bit_depth == 10);
        colorspace.tranfer_function = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT2020_10;
        colorspace.matrix = NV_ENC_VUI_MATRIX_COEFFS_BT2020_NCL;
        break;

      case video::colorspace_e::bt2020:
        // Rec. 2020 with ST 2084 perceptual quantizer
        colorspace.primaries = NV_ENC_VUI_COLOR_PRIMARIES_BT2020;
        assert(sunshine_colorspace.bit_depth == 10);
        colorspace.tranfer_function = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_SMPTE2084;
        colorspace.matrix = NV_ENC_VUI_MATRIX_COEFFS_BT2020_NCL;
        break;
    }

    colorspace.full_range = sunshine_colorspace.full_range;

    return colorspace;
  }

}  // namespace nvenc
