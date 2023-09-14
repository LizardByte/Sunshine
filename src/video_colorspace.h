#pragma once

extern "C" {
#include <libavutil/pixfmt.h>
}

namespace video {

  enum class colorspace_e {
    rec601,
    rec709,
    bt2020sdr,
    bt2020,
  };

  struct sunshine_colorspace_t {
    colorspace_e colorspace;
    bool full_range;
    unsigned bit_depth;
  };

  bool
  colorspace_is_hdr(const sunshine_colorspace_t &colorspace);

  // Declared in video.h
  struct config_t;

  sunshine_colorspace_t
  colorspace_from_client_config(const config_t &config, bool hdr_display);

  struct avcodec_colorspace_t {
    AVColorPrimaries primaries;
    AVColorTransferCharacteristic transfer_function;
    AVColorSpace matrix;
    AVColorRange range;
    int software_format;
  };

  avcodec_colorspace_t
  avcodec_colorspace_from_sunshine_colorspace(const sunshine_colorspace_t &sunshine_colorspace);

  struct alignas(16) color_t {
    float color_vec_y[4];
    float color_vec_u[4];
    float color_vec_v[4];
    float range_y[2];
    float range_uv[2];
  };

  const color_t *
  color_vectors_from_colorspace(const sunshine_colorspace_t &colorspace);

  const color_t *
  color_vectors_from_colorspace(colorspace_e colorspace, bool full_range);

}  // namespace video
