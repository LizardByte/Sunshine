/**
 * @file src/platform/linux/v4l2_wrapper.c
 * @brief Accessors for the patched FFmpeg V4L2 M2M output-buffer internals.
 *
 * FFmpeg's private V4L2 headers are intended for C and cause compatibility
 * problems when included directly from C++. This translation unit includes
 * those headers as C and presents the narrow C-compatible interface consumed
 * by Sunshine's C++ zero-copy V4L2 encode device.
 */

// standard includes
#include <libavcodec/avcodec.h>
#include <libavcodec/v4l2_m2m.h>

// local includes
#include "v4l2_wrapper.h"

/** @copydoc v4l2_wrapper_context_init */
int v4l2_wrapper_context_init(struct v4l2_wrapper_context *ctx, AVCodecContext *avctx) {
  if (!ctx || !avctx || !avctx->priv_data) {
    return -1;
  }

  V4L2m2mContext *v4l2_m2m_ctx = ((V4L2m2mPriv *) avctx->priv_data)->context;

  ctx->avctx = avctx;
  ctx->output = &v4l2_m2m_ctx->output;
  ctx->fd = v4l2_m2m_ctx->fd;
  ctx->format = v4l2_m2m_ctx->output.format;
  ctx->width = v4l2_m2m_ctx->output.width;
  ctx->height = v4l2_m2m_ctx->output.height;
  ctx->num_buffers = v4l2_m2m_ctx->output.num_buffers;

  v4l2_m2m_ctx->output.sunshine_zero_copy = 1;
  v4l2_m2m_ctx->output.current_buffer_index = -1;

  return 0;
}

/** @copydoc v4l2_wrapper_getfree_v4l2buf_idx */
int v4l2_wrapper_getfree_v4l2buf_idx(struct v4l2_wrapper_context *ctx) {
  struct V4L2Context *output = (struct V4L2Context *) ctx->output;

  // get back as many output buffers as possible
  while (v4l2_dequeue_v4l2buf(output, 0));

  for (int i = 0; i < ctx->num_buffers; i++) {
    if (output->buffers[i].status == V4L2BUF_AVAILABLE) {
      return i;
    }
  }

  return -1;
}

/** @copydoc v4l2_wrapper_set_current_buffer_index */
void v4l2_wrapper_set_current_buffer_index(struct v4l2_wrapper_context *ctx, int index) {
  struct V4L2Context *output = (struct V4L2Context *) ctx->output;

  output->current_buffer_index = index;
}
