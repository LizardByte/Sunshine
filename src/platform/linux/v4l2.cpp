/**
 * @file src/platform/linux/v4l2.cpp
 * @brief Definitions for V4L2 M2M zero-copy encode devices.
 */

// standard includes
#include <array>
#include <drm_fourcc.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <vector>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/frame.h>
}

// local includes
#include "graphics.h"
#include "v4l2.h"
#include "v4l2_wrapper.h"

namespace v4l2 {
  /**
   * @brief Common V4L2 M2M encode device backed by EGL-imported output buffers.
   */
  class v4l2_t: public platf::avcodec_encode_device_t {
  public:
    /**
     * @brief Initialize the GBM and EGL resources used for frame conversion.
     *
     * @param in_width Input image width in pixels.
     * @param in_height Input image height in pixels.
     * @param render_device Open render-device descriptor whose ownership is transferred to this object.
     * @return 0 on success, or -1 when GBM or EGL initialization fails.
     */
    int init(int in_width, int in_height, file_t &&render_device) {
      render_device_fd = std::move(render_device);

      if (!gbm::create_device) {
        BOOST_LOG(warning) << "libgbm not initialized"sv;
        return -1;
      }

      gbm_device.reset(gbm::create_device(render_device_fd.el));
      if (!gbm_device) {
        std::array<char, 1024> error_buffer {};
        BOOST_LOG(error) << "Couldn't create GBM device: ["sv << strerror_r(errno, error_buffer.data(), error_buffer.size()) << ']';
        return -1;
      }

      display = egl::make_display(gbm_device.get());
      if (!display) {
        return -1;
      }

      auto egl_ctx_opt = egl::make_ctx(display.get());
      if (!egl_ctx_opt) {
        return -1;
      }

      egl_ctx = std::move(*egl_ctx_opt);

      width = in_width;
      height = in_height;

      // V4L2 encoder has no need to create hw frames context,
      // so we just set this to a non-null value to avoid falling back to software encoding.
      data = (void *) 0x1;

      current_output_frame_idx = -1;

      return 0;
    }

    /**
     * @brief Import the output buffers allocated by an opened FFmpeg V4L2 M2M encoder.
     *
     * @param ctx Opened FFmpeg V4L2 M2M encoder context.
     * @return 0 on success, or -1 when the context, buffer layout, export, or EGL import is invalid.
     */
    int load_opened_context(AVCodecContext *ctx) override {
      if (v4l2_wrapper_context_init(&encoder_ctx, ctx)) {
        BOOST_LOG(error) << "Failed to load V4L2 context from AVCodecContext (open failed?)"sv;
        return -1;
      }

      auto &fmt = encoder_ctx.format.fmt;
      bool is_mplane = V4L2_TYPE_IS_MULTIPLANAR(encoder_ctx.format.type);
      auto num_buffers = encoder_ctx.num_buffers;
      auto num_planes = is_mplane ? fmt.pix_mp.num_planes : 1;
      auto pixel_format = is_mplane ? fmt.pix_mp.pixelformat : fmt.pix.pixelformat;

      // Check validity first to avoid unnecessary work
      if (num_buffers <= 0) {
        BOOST_LOG(error) << "V4L2 encoder has no output buffers to bind?"sv;
        return -1;
      }
      switch (pixel_format) {
        case V4L2_PIX_FMT_NV12:
          if (num_planes != 1) {
            BOOST_LOG(error) << "Unexpected number of planes for NV12 V4L2 output: expected 1, got "sv << num_planes;
            return -1;
          }
          break;
        case V4L2_PIX_FMT_NV12M:
          if (num_planes != 2) {
            BOOST_LOG(error) << "Unexpected number of planes for NV12M V4L2 output: expected 2, got "sv << num_planes;
            return -1;
          }
          break;
        default:
          BOOST_LOG(error) << "Unsupported V4L2 output pixel format: "sv << util::hex(pixel_format).to_string_view();
          return -1;
      }

      // Export the V4L2 output buffers
      output_frames.reserve(num_buffers);
      for (int i = 0; i < num_buffers; i++) {
        auto nv12_opt = export_v4l2_buffer(encoder_ctx.fd, i, encoder_ctx.format, display.get());
        if (!nv12_opt) {
          BOOST_LOG(error) << "Failed to import V4L2 output buffer "sv << i << " into EGL"sv;
          return -1;
        }
        output_frames.push_back(std::move(*nv12_opt));
      }

      auto convertor_opt = egl::sws_t::make(width, height, encoder_ctx.width, encoder_ctx.height, (AVPixelFormat) frame->format, false);
      if (!convertor_opt) {
        return -1;
      }
      convertor = std::move(*convertor_opt);

      return 0;
    }

    /**
     * @brief Attach frame resources used by the next conversion or encode operation.
     *
     * @param input_frame Video or graphics frame being processed.
     * @param hw_frames_ctx Hardware frames context buffer.
     * @return Status from updating frame.
     */
    int set_frame(AVFrame *input_frame, AVBufferRef *hw_frames_ctx) override {
      // Allocate a dummy buffer in case FFmpeg assumes this frame is invalid.
      input_frame->buf[0] = av_buffer_alloc(1);
      if (!input_frame->buf[0]) {
        av_frame_free(&input_frame);
        return -1;
      }

      this->frame_guard.reset(input_frame);
      this->frame = input_frame;

      return 0;
    }

    /**
     * @brief Apply the configured colorspace metadata to the EGL converter.
     */
    void apply_colorspace() override {
      convertor.apply_colorspace(colorspace, false);
    }

    /**
     * @brief Select the converted V4L2 output buffer and submit its metadata frame to FFmpeg.
     *
     * @param ctx Opened FFmpeg V4L2 M2M encoder context.
     * @return FFmpeg status code, or an AVERROR code when no suitable output buffer is available.
     */
    int send_frame(AVCodecContext *ctx) override {
      if (!encoder_ctx.avctx) {
        BOOST_LOG(error) << "Encoder context not set when trying to send a frame"sv;
        return AVERROR(EINVAL);
      }

      if (current_output_frame_idx < 0) {
        BOOST_LOG(error) << "No converted frame available to send to V4L2 encoder (convert() not called?)"sv;
        return AVERROR(EINVAL);
      }

      // If the output buffer selected by the most recent conversion is still in use,
      // find a free one and copy the converted frame into it.
      int real_output_frame_idx = current_output_frame_idx;
      v4l2_wrapper_sync_v4l2buf_status(&encoder_ctx);
      if (!v4l2_wrapper_v4l2buf_available(&encoder_ctx, current_output_frame_idx)) {
        real_output_frame_idx = get_free_frame(false);
        if (real_output_frame_idx < 0) {
          return AVERROR(EAGAIN);
        }
        if (convertor.copy_nv12(output_frames[real_output_frame_idx]->buf, output_frames[current_output_frame_idx]->buf)) {
          BOOST_LOG(error) << "Failed to copy converted frame to a new output buffer"sv;
          return AVERROR(EIO);
        }
      }

      v4l2_wrapper_set_current_buffer_index(&encoder_ctx, real_output_frame_idx);

      return avcodec_send_frame(ctx, frame);
    }

  protected:
    /**
     * @brief Select one available V4L2 output buffer for conversion.
     *
     * @param reclaim Whether to reclaim completed buffers before searching.
     * @return Zero-based output-buffer index, or -1 when no buffer is available.
     */
    int get_free_frame(bool reclaim = true) {
      if (!encoder_ctx.avctx) {
        BOOST_LOG(error) << "Encoder context not set when trying to get a free V4L2 output buffer"sv;
        return -1;
      }

      if (reclaim) {
        v4l2_wrapper_sync_v4l2buf_status(&encoder_ctx);
      }
      int frame_idx = v4l2_wrapper_getfree_v4l2buf_idx(&encoder_ctx);
      if (frame_idx < 0) {
        BOOST_LOG(error) << "No free V4L2 output buffer available"sv;
        return -1;
      }

      return frame_idx;
    }

    /**
     * @brief Load and convert an image into an available output buffer.
     *
     * @param load Callable that loads the source using the EGL converter and display.
     *             Returns 0 on success, or non-zero on failure.
     * @return 0 on success, or -1 if buffer selection, loading, or conversion fails.
     */
    int convert_helper(const std::function<int(egl::sws_t &, egl::display_t &)> &load) {
      const int frame_idx = get_free_frame();
      if (frame_idx < 0) {
        return -1;
      }
      if (load(convertor, display) || convertor.convert_nv12(output_frames[frame_idx]->buf)) {
        return -1;
      }
      current_output_frame_idx = frame_idx;
      return 0;
    }

  private:
    /**
     * @brief Select one available V4L2 output buffer and import it into EGL.
     *
     * @param v4l2_fd Opened V4L2 M2M device descriptor.
     * @param plane_index Zero-based index of the output buffer to export.
     * @param format Negotiated format of the V4L2 output queue.
     * @param display EGL display used to create the images.
     * @return Imported NV12 image, or empty when export or import fails.
     */
    static std::optional<egl::nv12_t> export_v4l2_buffer(int v4l2_fd, int plane_index, const v4l2_format &format, egl::display_t::pointer display) {
      bool is_mplane = V4L2_TYPE_IS_MULTIPLANAR(format.type);
      auto num_planes = is_mplane ? format.fmt.pix_mp.num_planes : 1;
      auto pixel_format = is_mplane ? format.fmt.pix_mp.pixelformat : format.fmt.pix.pixelformat;

      v4l2_exportbuffer export_req = {0};
      export_req.type = format.type;
      export_req.index = plane_index;
      export_req.flags = O_CLOEXEC;

      std::array<file_t, egl::nv12_img_t::num_fds> dmabuf_fds;
      for (int plane = 0; plane < num_planes; plane++) {
        export_req.plane = plane;
        if (ioctl(v4l2_fd, VIDIOC_EXPBUF, &export_req) < 0) {
          std::array<char, 1024> error_buffer {};
          BOOST_LOG(error) << "Couldn't export V4L2 buffer "sv << plane_index << ": " << strerror_r(errno, error_buffer.data(), error_buffer.size());
          return std::nullopt;
        }
        dmabuf_fds[plane] = export_req.fd;
      }

      std::array<egl::surface_descriptor_t, 2> sds;
      for (auto &sd : sds) {
        std::fill_n(sd.fds.begin(), 4, -1);
      }
      switch (pixel_format) {
        case V4L2_PIX_FMT_NV12:
          {
            auto buffer_width = is_mplane ? format.fmt.pix_mp.width : format.fmt.pix.width;
            auto buffer_height = is_mplane ? format.fmt.pix_mp.height : format.fmt.pix.height;
            auto bytesperline = is_mplane ? format.fmt.pix_mp.plane_fmt[0].bytesperline : format.fmt.pix.bytesperline;

            sds[0].width = buffer_width;
            sds[0].height = buffer_height;
            sds[0].fds[0] = dmabuf_fds[0].el;
            sds[0].fourcc = DRM_FORMAT_R8;
            sds[0].modifier = DRM_FORMAT_MOD_LINEAR;
            sds[0].pitches[0] = bytesperline;
            sds[0].offsets[0] = 0;

            sds[1].width = buffer_width / 2;  // UV plane is subsampled
            sds[1].height = buffer_height / 2;
            sds[1].fds[0] = dmabuf_fds[0].el;
            sds[1].fourcc = DRM_FORMAT_GR88;
            sds[1].modifier = DRM_FORMAT_MOD_LINEAR;
            sds[1].pitches[0] = bytesperline;
            sds[1].offsets[0] = bytesperline * buffer_height;

            break;
          }
        case V4L2_PIX_FMT_NV12M:
          {
            sds[0].width = format.fmt.pix_mp.width;
            sds[0].height = format.fmt.pix_mp.height;
            sds[0].fds[0] = dmabuf_fds[0].el;
            sds[0].fourcc = DRM_FORMAT_R8;
            sds[0].modifier = DRM_FORMAT_MOD_LINEAR;
            sds[0].pitches[0] = format.fmt.pix_mp.plane_fmt[0].bytesperline;
            sds[0].offsets[0] = 0;

            sds[1].width = format.fmt.pix_mp.width / 2;  // UV plane is subsampled
            sds[1].height = format.fmt.pix_mp.height / 2;
            sds[1].fds[0] = dmabuf_fds[1].el;
            sds[1].fourcc = DRM_FORMAT_GR88;
            sds[1].modifier = DRM_FORMAT_MOD_LINEAR;
            sds[1].pitches[0] = format.fmt.pix_mp.plane_fmt[1].bytesperline;
            sds[1].offsets[0] = 0;

            break;
          }
        default:
          // Unreachable
          return std::nullopt;
      }

      return egl::import_target(display, std::move(dmabuf_fds), sds[0], sds[1]);
    }

    frame_t frame_guard;  ///< Metadata frame owner; pixel buffers are managed by the V4L2 kernel driver.

    file_t render_device_fd;  ///< Render-device descriptor used to create the GBM device.
    gbm::gbm_t gbm_device;  ///< GBM device associated with the selected render node.
    egl::display_t display;  ///< EGL display used to import and convert DMA-BUFs.
    egl::ctx_t egl_ctx;  ///< EGL context current while conversion resources are used.

    v4l2_wrapper_context encoder_ctx;  ///< Wrapper around the opened FFmpeg V4L2 context.
    egl::sws_t convertor;  ///< EGL color converter targeting the encoder's NV12 buffers.
    std::vector<egl::nv12_t> output_frames;  ///< EGL imports of all V4L2 output buffers.

    int width;  ///< Input image width in pixels.
    int height;  ///< Input image height in pixels.

    int current_output_frame_idx;  ///< Output-buffer index populated by the most recent conversion.
  };

  /**
   * @brief V4L2 M2M encode device that uploads system-memory capture frames through EGL.
   */
  class v4l2_ram_t: public v4l2_t {
  public:
    /**
     * @brief Upload and convert a system-memory image into a free V4L2 output buffer.
     *
     * @param img Captured system-memory image to convert.
     * @return 0 on success, or -1 when no output buffer is available.
     */
    int convert(platf::img_t &img) override {
      return convert_helper([&img](egl::sws_t &convertor, egl::display_t &display) {
        convertor.load_ram(img);
        return 0;
      });
    }
  };

  /**
   * @brief V4L2 M2M encode device that imports and converts DMA-BUF capture frames.
   */
  class v4l2_vram_t: public v4l2_t {
  public:
    /**
     * @brief Import and convert a DMA-BUF image into a free V4L2 output buffer.
     *
     * @param img Captured DMA-BUF image descriptor to convert.
     * @return 0 on success, or -1 when buffer selection or DMA-BUF import fails.
     */
    int convert(platf::img_t &img) override {
      return convert_helper([&img, this](egl::sws_t &convertor, egl::display_t &display) {
        auto &descriptor = (egl::img_descriptor_t &) img;

        if (descriptor.sequence == 0) {
          // For dummy images, use a blank RGB texture instead of importing a DMA-BUF
          input_frame = egl::create_blank(img);
        } else if (descriptor.sequence > sequence) {
          sequence = descriptor.sequence;

          auto rgb_opt = egl::import_source(display.get(), descriptor.sd);
          if (!rgb_opt) {
            return -1;
          }

          input_frame = std::move(*rgb_opt);
        }

        convertor.load_vram(descriptor, offset_x, offset_y, (int) input_frame->tex[0], false);
        return 0;
      });
    }

    /**
     * @brief Initialize a DMA-BUF-backed V4L2 encode device and its capture offset.
     *
     * @param in_width Input image width in pixels.
     * @param in_height Input image height in pixels.
     * @param render_device Open render-device descriptor whose ownership is transferred to this object.
     * @param capture_offset_x Horizontal offset of the input image within its texture.
     * @param capture_offset_y Vertical offset of the input image within its texture.
     * @return 0 on success, or -1 when the common V4L2/EGL initialization fails.
     */
    int init(int in_width, int in_height, file_t &&render_device, int capture_offset_x, int capture_offset_y) {
      if (v4l2_t::init(in_width, in_height, std::move(render_device))) {
        return -1;
      }

      sequence = 0;

      this->offset_x = capture_offset_x;
      this->offset_y = capture_offset_y;

      return 0;
    }

  private:
    egl::rgb_t input_frame;  ///< EGL import of the current DMA-BUF capture frame.
    std::uint64_t sequence;  ///< Sequence number of the capture frame currently imported into EGL.
    int offset_x;  ///< Horizontal capture offset within the imported texture.
    int offset_y;  ///< Vertical capture offset within the imported texture.
  };

  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, file_t &&card, int offset_x, int offset_y, bool vram) {
    if (vram) {
      auto egl = std::make_unique<v4l2::v4l2_vram_t>();
      if (egl->init(width, height, std::move(card), offset_x, offset_y)) {
        return nullptr;
      }

      return egl;
    }

    else {
      auto egl = std::make_unique<v4l2::v4l2_ram_t>();
      if (egl->init(width, height, std::move(card))) {
        return nullptr;
      }

      return egl;
    }
  }

  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, int offset_x, int offset_y, bool vram) {
    auto render_device = platf::resolve_render_device();

    file_t file = ::open(render_device.c_str(), O_RDWR);  // NOSONAR(cpp:S1874): `_sopen_s` not available
    if (file.el < 0) {
      std::array<char, 1024> error_buffer {};
      BOOST_LOG(error) << "Couldn't open "sv << render_device << ": " << strerror_r(errno, error_buffer.data(), error_buffer.size());

      return nullptr;
    }

    return make_avcodec_encode_device(width, height, std::move(file), offset_x, offset_y, vram);
  }

  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, bool vram) {
    return make_avcodec_encode_device(width, height, 0, 0, vram);
  }

}  // namespace v4l2
