/**
 * @file src/video_colorspace.cpp
 * @brief Definitions for colorspace functions.
 */
#include "video_colorspace.h"

#include "logging.h"
#include "video.h"

extern "C" {
#include <libswscale/swscale.h>
}

namespace video {

  bool
  colorspace_is_hdr(const sunshine_colorspace_t &colorspace) {
    return colorspace.colorspace == colorspace_e::bt2020;
  }

  sunshine_colorspace_t
  colorspace_from_client_config(const config_t &config, bool hdr_display) {
    sunshine_colorspace_t colorspace;

    /* See video::config_t declaration for details */

    if (config.dynamicRange > 0 && hdr_display) {
      // Rec. 2020 with ST 2084 perceptual quantizer
      colorspace.colorspace = colorspace_e::bt2020;
    }
    else {
      switch (config.encoderCscMode >> 1) {
        case 0:
          // Rec. 601
          colorspace.colorspace = colorspace_e::rec601;
          break;

        case 1:
          // Rec. 709
          colorspace.colorspace = colorspace_e::rec709;
          break;

        case 2:
          // Rec. 2020
          colorspace.colorspace = colorspace_e::bt2020sdr;
          break;

        default:
          BOOST_LOG(error) << "Unknown video colorspace in csc, falling back to Rec. 709";
          colorspace.colorspace = colorspace_e::rec709;
          break;
      }
    }

    colorspace.full_range = (config.encoderCscMode & 0x1);

    switch (config.dynamicRange) {
      case 0:
        colorspace.bit_depth = 8;
        break;

      case 1:
        colorspace.bit_depth = 10;
        break;

      default:
        BOOST_LOG(error) << "Unknown dynamicRange value, falling back to 10-bit color depth";
        colorspace.bit_depth = 10;
        break;
    }

    if (colorspace.colorspace == colorspace_e::bt2020sdr && colorspace.bit_depth != 10) {
      BOOST_LOG(error) << "BT.2020 SDR colorspace expects 10-bit color depth, falling back to Rec. 709";
      colorspace.colorspace = colorspace_e::rec709;
    }

    return colorspace;
  }

  avcodec_colorspace_t
  avcodec_colorspace_from_sunshine_colorspace(const sunshine_colorspace_t &sunshine_colorspace) {
    avcodec_colorspace_t avcodec_colorspace;

    switch (sunshine_colorspace.colorspace) {
      case colorspace_e::rec601:
        // Rec. 601
        avcodec_colorspace.primaries = AVCOL_PRI_SMPTE170M;
        avcodec_colorspace.transfer_function = AVCOL_TRC_SMPTE170M;
        avcodec_colorspace.matrix = AVCOL_SPC_SMPTE170M;
        avcodec_colorspace.software_format = SWS_CS_SMPTE170M;
        break;

      case colorspace_e::rec709:
        // Rec. 709
        avcodec_colorspace.primaries = AVCOL_PRI_BT709;
        avcodec_colorspace.transfer_function = AVCOL_TRC_BT709;
        avcodec_colorspace.matrix = AVCOL_SPC_BT709;
        avcodec_colorspace.software_format = SWS_CS_ITU709;
        break;

      case colorspace_e::bt2020sdr:
        // Rec. 2020
        avcodec_colorspace.primaries = AVCOL_PRI_BT2020;
        assert(sunshine_colorspace.bit_depth == 10);
        avcodec_colorspace.transfer_function = AVCOL_TRC_BT2020_10;
        avcodec_colorspace.matrix = AVCOL_SPC_BT2020_NCL;
        avcodec_colorspace.software_format = SWS_CS_BT2020;
        break;

      case colorspace_e::bt2020:
        // Rec. 2020 with ST 2084 perceptual quantizer
        avcodec_colorspace.primaries = AVCOL_PRI_BT2020;
        assert(sunshine_colorspace.bit_depth == 10);
        avcodec_colorspace.transfer_function = AVCOL_TRC_SMPTE2084;
        avcodec_colorspace.matrix = AVCOL_SPC_BT2020_NCL;
        avcodec_colorspace.software_format = SWS_CS_BT2020;
        break;
    }

    avcodec_colorspace.range = sunshine_colorspace.full_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

    return avcodec_colorspace;
  }

  const color_t *
  color_vectors_from_colorspace(const sunshine_colorspace_t &colorspace) {
    return color_vectors_from_colorspace(colorspace.colorspace, colorspace.full_range);
  }

  const color_t *
  color_vectors_from_colorspace(colorspace_e colorspace, bool full_range) {
    using float2 = float[2];
    auto make_color_matrix = [](float Cr, float Cb, const float2 &range_Y, const float2 &range_UV) -> color_t {
      float Cg = 1.0f - Cr - Cb;

      float Cr_i = 1.0f - Cr;
      float Cb_i = 1.0f - Cb;

      float shift_y = range_Y[0] / 255.0f;
      float shift_uv = range_UV[0] / 255.0f;

      float scale_y = (range_Y[1] - range_Y[0]) / 255.0f;
      float scale_uv = (range_UV[1] - range_UV[0]) / 255.0f;
      return {
        { Cr, Cg, Cb, 0.0f },
        { -(Cr * 0.5f / Cb_i), -(Cg * 0.5f / Cb_i), 0.5f, 0.5f },
        { 0.5f, -(Cg * 0.5f / Cr_i), -(Cb * 0.5f / Cr_i), 0.5f },
        { scale_y, shift_y },
        { scale_uv, shift_uv },
      };
    };

    static const color_t colors[] {
      make_color_matrix(0.299f, 0.114f, { 16.0f, 235.0f }, { 16.0f, 240.0f }),  // BT601 MPEG
      make_color_matrix(0.299f, 0.114f, { 0.0f, 255.0f }, { 0.0f, 255.0f }),  // BT601 JPEG
      make_color_matrix(0.2126f, 0.0722f, { 16.0f, 235.0f }, { 16.0f, 240.0f }),  // BT709 MPEG
      make_color_matrix(0.2126f, 0.0722f, { 0.0f, 255.0f }, { 0.0f, 255.0f }),  // BT709 JPEG
      make_color_matrix(0.2627f, 0.0593f, { 16.0f, 235.0f }, { 16.0f, 240.0f }),  // BT2020 MPEG
      make_color_matrix(0.2627f, 0.0593f, { 0.0f, 255.0f }, { 0.0f, 255.0f }),  // BT2020 JPEG
    };

    const color_t *result = nullptr;

    switch (colorspace) {
      case colorspace_e::rec601:
      default:
        result = &colors[0];
        break;
      case colorspace_e::rec709:
        result = &colors[2];
        break;
      case colorspace_e::bt2020:
      case colorspace_e::bt2020sdr:
        result = &colors[4];
        break;
    };

    if (full_range) {
      result++;
    }

    return result;
  }

}  // namespace video
