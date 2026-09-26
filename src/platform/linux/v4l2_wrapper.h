/**
 * @file src/platform/linux/v4l2_wrapper.h
 * @brief Interface to the patched FFmpeg V4L2 M2M output-buffer internals.
 *
 * FFmpeg's private V4L2 headers are implemented for C and cannot be included
 * safely from Sunshine's C++ V4L2 encode device. This C-compatible interface
 * keeps those headers behind a small wrapper and exposes only the state and
 * operations required by the zero-copy path.
 */
#pragma once

// standard includes
#include <linux/videodev2.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>

  /**
   * @brief State required to access an FFmpeg V4L2 M2M output queue.
   *
   * This structure exposes only the FFmpeg-private state needed by Sunshine's
   * zero-copy V4L2 encode device, keeping those dependencies out of C++ code.
   */
  struct v4l2_wrapper_context {
    AVCodecContext *avctx;  ///< FFmpeg codec context that owns the V4L2 M2M context.

    void *output;  ///< Opaque pointer to FFmpeg's V4L2 output-queue context.
    int fd;  ///< File descriptor of the V4L2 M2M device.
    struct v4l2_format format;  ///< Negotiated format of the V4L2 output queue.
    int width;  ///< Negotiated output width in pixels.
    int height;  ///< Negotiated output height in pixels.
    int num_buffers;  ///< Number of buffers allocated for the V4L2 output queue.
  };

  /**
   * @brief Initialize a wrapper context from an opened FFmpeg V4L2 M2M encoder.
   *
   * This also enables Sunshine's zero-copy buffer-selection path in the
   * patched FFmpeg V4L2 output queue.
   *
   * @param ctx Wrapper context to initialize.
   * @param avctx Opened FFmpeg V4L2 M2M encoder context.
   * @return 0 on success, or -1 when a required input pointer is null.
   */
  int v4l2_wrapper_context_init(struct v4l2_wrapper_context *ctx, AVCodecContext *avctx);

  /**
   * @brief Dequeue completed output buffers and find one available for reuse.
   *
   * @param ctx Initialized wrapper context whose output queue is inspected.
   * @return Zero-based buffer index on success, or -1 when none is available.
   */
  int v4l2_wrapper_getfree_v4l2buf_idx(struct v4l2_wrapper_context *ctx);

  /**
   * @brief Select the V4L2 output buffer used for the next submitted frame.
   *
   * @param ctx Initialized wrapper context whose output queue is updated.
   * @param index Zero-based index of the output buffer to select.
   */
  void v4l2_wrapper_set_current_buffer_index(struct v4l2_wrapper_context *ctx, int index);

#ifdef __cplusplus
}
#endif
