/**
 * @file src/video_colorspace.h
 * @brief Declarations for colorspace functions.
 */
#pragma once

extern "C" {
#include <libavutil/pixfmt.h>
}

namespace video {

  /**
   * @brief Enumerates supported colorspace options.
   */
  enum class colorspace_e {
    rec601,  ///< Rec. 601
    rec709,  ///< Rec. 709
    bt2020sdr,  ///< Rec. 2020 SDR
    bt2020,  ///< Rec. 2020 HDR
  };

  /**
   * @brief Sunshine colorimetry values derived from stream configuration.
   */
  struct sunshine_colorspace_t {
    colorspace_e colorspace;  ///< Colorspace.
    bool full_range;  ///< Whether the video range is full-range instead of limited.
    unsigned bit_depth;  ///< Bit depth.
  };

  /**
   * @brief Check whether a Sunshine colorspace represents HDR video.
   *
   * @param colorspace Colorimetry information used for conversion or encoding.
   * @return True when the colorspace values describe the same metadata.
   */
  bool colorspace_is_hdr(const sunshine_colorspace_t &colorspace);

  // Declared in video.h
  struct config_t;

  /**
   * @brief Derive Sunshine colorspace metadata from the negotiated client config.
   *
   * @param config Configuration values to apply.
   * @param hdr_display HDR display.
   * @return Colorspace, range, transfer, and HDR flags used for encoding.
   */
  sunshine_colorspace_t colorspace_from_client_config(const config_t &config, bool hdr_display);

  /**
   * @brief FFmpeg colorimetry values used by AVCodec encoders.
   */
  struct avcodec_colorspace_t {
    AVColorPrimaries primaries;  ///< FFmpeg color primaries selected for the encoded stream.
    AVColorTransferCharacteristic transfer_function;  ///< FFmpeg transfer function selected for the encoded stream.
    AVColorSpace matrix;  ///< FFmpeg YUV matrix coefficients selected for the encoded stream.
    AVColorRange range;  ///< FFmpeg full-range or limited-range flag selected for the encoded stream.
    int software_format;  ///< FFmpeg software pixel format used when hardware frames are not supplied.
  };

  /**
   * @brief Convert Sunshine colorspace metadata to FFmpeg AVCodec fields.
   *
   * @param sunshine_colorspace Sunshine colorspace.
   * @return FFmpeg color range, primaries, transfer, and software format values.
   */
  avcodec_colorspace_t avcodec_colorspace_from_sunshine_colorspace(const sunshine_colorspace_t &sunshine_colorspace);

  /**
   * @brief Pair of Sunshine and AVCodec colorimetry descriptions.
   */
  struct alignas(16) color_t {
    float color_vec_y[4];  ///< Color vec y.
    float color_vec_u[4];  ///< Color vec u.
    float color_vec_v[4];  ///< Color vec v.
    float range_y[2];  ///< Range y.
    float range_uv[2];  ///< Range uv.
  };

  /**
   * @brief Get static RGB->YUV color conversion matrix.
   *        This matrix expects RGB input in UNORM (0.0 to 1.0) range and doesn't perform any
   *        gamut mapping or gamma correction.
   * @param colorspace Targeted YUV colorspace.
   * @param unorm_output Whether the matrix should produce output in UNORM or UINT range.
   * @return `const color_t*` that contains RGB->YUV transformation vectors.
   *         Components `range_y` and `range_uv` are there for backwards compatibility
   *         and can be ignored in the computation.
   */
  const color_t *color_vectors_from_colorspace(const sunshine_colorspace_t &colorspace, bool unorm_output);
}  // namespace video
