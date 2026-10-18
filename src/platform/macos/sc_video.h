/**
 * @file src/platform/macos/sc_video.h
 * @brief Declarations for ScreenCaptureKit video capture on macOS.
 */
#pragma once

// local includes
#import "av_video.h"

// platform includes
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#ifdef __cplusplus
extern "C" {
#endif

  /**
   * @brief Luminance in nits that SDR (EDR 1.0) white is streamed at in HDR, per ITU-R BT.2408.
   *
   * ScreenCaptureKit's HDR modes place SDR white at 100 nits, which looks grey next to an HDR client's
   * SDR content, so HDR frames are scaled by `sc_hdr_sdr_white_nits / 100` in linear light.
   */
  extern const float sc_hdr_sdr_white_nits;

  /**
   * @brief Copy a captured frame into a buffer from `pool`, keeping its color attachments and timing.
   *
   * PQ-tagged 10-bit 4:2:0 frames are scaled by `pq_gain` in linear light on the GPU; other frames,
   * or a gain of 1, are copied as is.
   *
   * @param source Sample buffer holding the captured frame.
   * @param pool Pool for the copies; (re)created when the frame format or size changes. Release with CVPixelBufferPoolRelease().
   * @param pq_gain Linear-light gain for PQ frames.
   * @return Retained copy, or NULL on failure.
   */
  CMSampleBufferRef sc_copy_sample_buffer(CMSampleBufferRef source, CVPixelBufferPoolRef *pool, float pq_gain) CF_RETURNS_RETAINED;

#ifdef __cplusplus
}
#endif

/**
 * @brief ScreenCaptureKit capture backend with HDR (BT.2020 PQ) support.
 *
 * Drop-in replacement for AVVideo: same properties, same capture: contract.
 * When `hdr` is set and the pixel format is 10-bit, frames are captured in
 * HDR (canonical display tone mapping) as ITU-R BT.2100 PQ.
 */
API_AVAILABLE(macos(15.0))
@interface SCVideo: AVVideo <SCStreamOutput, SCStreamDelegate>

/**
 * @brief Capture in HDR (BT.2020 PQ) when the pixel format is 10-bit.
 */
@property (nonatomic, assign) BOOL hdr;

@end
