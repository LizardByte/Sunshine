/**
 * @file src/video_colorspace.cpp
 * @brief Definitions for colorspace functions.
 */
// this include
#include "video_colorspace.h"

// local includes
#include "logging.h"
#include "video.h"

extern "C" {
#include <libswscale/swscale.h>
}

namespace video {

  bool colorspace_is_hdr(const sunshine_colorspace_t &colorspace) {
    return colorspace.colorspace == colorspace_e::bt2020;
  }

  sunshine_colorspace_t colorspace_from_client_config(const config_t &config, bool hdr_display) {
    sunshine_colorspace_t colorspace;

    /* See video::config_t declaration for details */

    if (config.dynamicRange > 0 && hdr_display) {
      // Rec. 2020 with ST 2084 perceptual quantizer
      colorspace.colorspace = colorspace_e::bt2020;
    } else {
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

  avcodec_colorspace_t avcodec_colorspace_from_sunshine_colorspace(const sunshine_colorspace_t &sunshine_colorspace) {
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

  const color_t *color_vectors_from_colorspace(const sunshine_colorspace_t &colorspace) {
    return color_vectors_from_colorspace(colorspace.colorspace, colorspace.full_range);
  }

  const color_t *color_vectors_from_colorspace(colorspace_e colorspace, bool full_range) {
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
        {Cr, Cg, Cb, 0.0f},
        {-(Cr * 0.5f / Cb_i), -(Cg * 0.5f / Cb_i), 0.5f, 0.5f},
        {0.5f, -(Cg * 0.5f / Cr_i), -(Cb * 0.5f / Cr_i), 0.5f},
        {scale_y, shift_y},
        {scale_uv, shift_uv},
      };
    };

    static const color_t colors[] {
      make_color_matrix(0.299f, 0.114f, {16.0f, 235.0f}, {16.0f, 240.0f}),  // BT601 MPEG
      make_color_matrix(0.299f, 0.114f, {0.0f, 255.0f}, {0.0f, 255.0f}),  // BT601 JPEG
      make_color_matrix(0.2126f, 0.0722f, {16.0f, 235.0f}, {16.0f, 240.0f}),  // BT709 MPEG
      make_color_matrix(0.2126f, 0.0722f, {0.0f, 255.0f}, {0.0f, 255.0f}),  // BT709 JPEG
      make_color_matrix(0.2627f, 0.0593f, {16.0f, 235.0f}, {16.0f, 240.0f}),  // BT2020 MPEG
      make_color_matrix(0.2627f, 0.0593f, {0.0f, 255.0f}, {0.0f, 255.0f}),  // BT2020 JPEG
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

  const color_t *new_color_vectors_from_colorspace(const sunshine_colorspace_t &colorspace) {
    constexpr auto generate_color_vectors = [](const sunshine_colorspace_t &colorspace) -> color_t {
      double Kr, Kb;
      switch (colorspace.colorspace) {
        case colorspace_e::rec601:
          Kr = 0.299;
          Kb = 0.114;
          break;
        case colorspace_e::rec709:
        default:
          Kr = 0.2126;
          Kb = 0.0722;
          break;
        case colorspace_e::bt2020:
        case colorspace_e::bt2020sdr:
          Kr = 0.2627;
          Kb = 0.0593;
          break;
      }
      double Kg = 1.0 - Kr - Kb;

      double y_mult, y_add;
      double uv_mult, uv_add;

      // "Matrix coefficients" section of ITU-T H.273
      if (colorspace.full_range) {
        y_mult = (1 << colorspace.bit_depth) - 1;
        y_add = 0;
        uv_mult = (1 << colorspace.bit_depth) - 1;
        uv_add = (1 << (colorspace.bit_depth - 1));
      } else {
        y_mult = (1 << (colorspace.bit_depth - 8)) * 219;
        y_add = (1 << (colorspace.bit_depth - 8)) * 16;
        uv_mult = (1 << (colorspace.bit_depth - 8)) * 224;
        uv_add = (1 << (colorspace.bit_depth - 8)) * 128;
      }

      // For rounding
      y_add += 0.5;
      uv_add += 0.5;

      color_t color_vectors;

      color_vectors.color_vec_y[0] = Kr * y_mult;
      color_vectors.color_vec_y[1] = Kg * y_mult;
      color_vectors.color_vec_y[2] = Kb * y_mult;
      color_vectors.color_vec_y[3] = y_add;

      color_vectors.color_vec_u[0] = -0.5 * Kr / (1.0 - Kb) * uv_mult;
      color_vectors.color_vec_u[1] = -0.5 * Kg / (1.0 - Kb) * uv_mult;
      color_vectors.color_vec_u[2] = 0.5 * uv_mult;
      color_vectors.color_vec_u[3] = uv_add;

      color_vectors.color_vec_v[0] = 0.5 * uv_mult;
      color_vectors.color_vec_v[1] = -0.5 * Kg / (1.0 - Kr) * uv_mult;
      color_vectors.color_vec_v[2] = -0.5 * Kb / (1.0 - Kr) * uv_mult;
      color_vectors.color_vec_v[3] = uv_add;

      // Unused
      color_vectors.range_y[0] = 1;
      color_vectors.range_y[1] = 0;
      color_vectors.range_uv[0] = 1;
      color_vectors.range_uv[1] = 0;

      return color_vectors;
    };

    static constexpr color_t colors[] = {
      generate_color_vectors({colorspace_e::rec601, false, 8}),
      generate_color_vectors({colorspace_e::rec601, true, 8}),
      generate_color_vectors({colorspace_e::rec601, false, 10}),
      generate_color_vectors({colorspace_e::rec601, true, 10}),
      generate_color_vectors({colorspace_e::rec709, false, 8}),
      generate_color_vectors({colorspace_e::rec709, true, 8}),
      generate_color_vectors({colorspace_e::rec709, false, 10}),
      generate_color_vectors({colorspace_e::rec709, true, 10}),
      generate_color_vectors({colorspace_e::bt2020, false, 8}),
      generate_color_vectors({colorspace_e::bt2020, true, 8}),
      generate_color_vectors({colorspace_e::bt2020, false, 10}),
      generate_color_vectors({colorspace_e::bt2020, true, 10}),
    };

    const color_t *result = nullptr;

    switch (colorspace.colorspace) {
      case colorspace_e::rec601:
        result = &colors[0];
        break;
      case colorspace_e::rec709:
      default:
        result = &colors[4];
        break;
      case colorspace_e::bt2020:
      case colorspace_e::bt2020sdr:
        result = &colors[8];
        break;
    }

    if (colorspace.bit_depth == 10) {
      result += 2;
    }
    if (colorspace.full_range) {
      result += 1;
    }

    return result;
  }
}  // namespace video
