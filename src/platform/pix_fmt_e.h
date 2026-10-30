/**
 * @file src/platform/pix_fmt_e.h
 * @brief Capture pixel format enumeration shared with the CUDA translation unit.
 */
#pragma once

// standard includes
#include <cstdint>

namespace platf {
  /**
   * @brief Enumerates supported pix fmt options.
   *
   * The plain 8-bit names describe the byte order in memory, so bgr0 stores B,
   * G and R in increasing addresses. The 101010 and 2101010 names describe the
   * 32-bit word layout instead, so xbgr2101010 carries two bits of X in the
   * most significant bits of the word.
   */
  enum class pix_fmt_e : std::int32_t {
    yuv420p,  ///< YUV 4:2:0
    yuv420p10,  ///< YUV 4:2:0 10-bit
    nv12,  ///< NV12
    p010,  ///< P010
    ayuv,  ///< AYUV
    yuv444p16,  ///< Planar YUV 4:4:4, 10-bit components in 16-bit words
    yuv444p,  ///< Planar 8-bit YUV 4:4:4
    y410,  ///< Y410
    bgr0,  ///< Packed 8-bit B,G,R,0
    bgra,  ///< Packed 8-bit B,G,R,A
    rgba,  ///< Packed 8-bit R,G,B,A
    xbgr2101010,  ///< Packed 10-bit X,B,G,R
    bgra1010102,  ///< Packed 10-bit B,G,R,A
    rgba1010102,  ///< Packed 10-bit R,G,B,A
    abgr2101010,  ///< Packed 10-bit A,B,G,R
    argb2101010,  ///< Packed 10-bit A,R,G,B
    unknown  ///< Unknown
  };
}  // namespace platf
