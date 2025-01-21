/**
 * @file src/nvenc/nvenc_utils.h
 * @brief Declarations for NVENC utilities.
 */
#pragma once

// plafform includes
#ifdef _WIN32
  #include <dxgiformat.h>
#endif

// lib includes
#include <ffnvcodec/nvEncodeAPI.h>

// local includes
#include "nvenc_colorspace.h"
#include "src/platform/common.h"
#include "src/video_colorspace.h"

namespace nvenc {

#ifdef _WIN32
  DXGI_FORMAT dxgi_format_from_nvenc_format(NV_ENC_BUFFER_FORMAT format);
#endif

  NV_ENC_BUFFER_FORMAT nvenc_format_from_sunshine_format(platf::pix_fmt_e format);

  nvenc_colorspace_t nvenc_colorspace_from_sunshine_colorspace(const video::sunshine_colorspace_t &sunshine_colorspace);

}  // namespace nvenc
