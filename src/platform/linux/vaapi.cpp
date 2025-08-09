/**
 * @file src/platform/linux/vaapi.cpp
 * @brief Definitions for VA-API hardware accelerated capture.
 */
// standard includes
#include <fcntl.h>
#include <format>
#include <sstream>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <va/va.h>
#include <va/va_drm.h>
#if !VA_CHECK_VERSION(1, 9, 0)
  // vaSyncBuffer stub allows Sunshine built against libva <2.9.0 to link against ffmpeg on libva 2.9.0 or later
  VAStatus
    vaSyncBuffer(
      VADisplay dpy,
      VABufferID buf_id,
      uint64_t timeout_ns
    ) {
    return VA_STATUS_ERROR_UNIMPLEMENTED;
  }
#endif
}

// local includes
#include "graphics.h"
#include "misc.h"
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"
#include "src/video.h"

using namespace std::literals;

extern "C" struct AVBufferRef;

namespace va {
  constexpr auto SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2 = 0x40000000;
  constexpr auto EXPORT_SURFACE_WRITE_ONLY = 0x0002;
  constexpr auto EXPORT_SURFACE_SEPARATE_LAYERS = 0x0004;

  using VADisplay = void *;
  using VAStatus = int;
  using VAGenericID = unsigned int;
  using VASurfaceID = VAGenericID;

  struct DRMPRIMESurfaceDescriptor {
    // VA Pixel format fourcc of the whole surface (VA_FOURCC_*).
    uint32_t fourcc;

    uint32_t width;
    uint32_t height;

    // Number of distinct DRM objects making up the surface.
    uint32_t num_objects;

    struct {
      // DRM PRIME file descriptor for this object.
      // Needs to be closed manually
      int fd;

      // Total size of this object (may include regions which are not part of the surface)
      uint32_t size;
      // Format modifier applied to this object, not sure what that means
      uint64_t drm_format_modifier;
    } objects[4];

    // Number of layers making up the surface.
    uint32_t num_layers;

    struct {
      // DRM format fourcc of this layer (DRM_FOURCC_*).
      uint32_t drm_format;

      // Number of planes in this layer.
      uint32_t num_planes;

      // references objects --> DRMPRIMESurfaceDescriptor.objects[object_index[0]]
      uint32_t object_index[4];

      // Offset within the object of each plane.
      uint32_t offset[4];

      // Pitch of each plane.
      uint32_t pitch[4];
    } layers[4];
  };

  using display_t = util::safe_ptr_v2<void, VAStatus, vaTerminate>;

  int vaapi_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *encode_device, AVBufferRef **hw_device_buf);

  class va_t: public platf::avcodec_encode_device_t {
  public:
    int init(int in_width, int in_height, file_t &&render_device) {
      file = std::move(render_device);

      if (!gbm::create_device) {
        BOOST_LOG(warning) << "libgbm not initialized"sv;
        return -1;
      }

      this->data = (void *) vaapi_init_avcodec_hardware_input_buffer;

      gbm.reset(gbm::create_device(file.el));
      if (!gbm) {
        char string[1024];
        BOOST_LOG(error) << "Couldn't create GBM device: ["sv << strerror_r(errno, string, sizeof(string)) << ']';
        return -1;
      }

      display = egl::make_display(gbm.get());
      if (!display) {
        return -1;
      }

      auto ctx_opt = egl::make_ctx(display.get());
      if (!ctx_opt) {
        return -1;
      }

      ctx = std::move(*ctx_opt);

      width = in_width;
      height = in_height;

      return 0;
    }

    /**
     * @brief Finds a supported VA entrypoint for the given VA profile.
     * @param profile The profile to match.
     * @return A valid encoding entrypoint or 0 on failure.
     */
    VAEntrypoint select_va_entrypoint(VAProfile profile) {
      std::vector<VAEntrypoint> entrypoints(vaMaxNumEntrypoints(va_display));
      int num_eps;
      auto status = vaQueryConfigEntrypoints(va_display, profile, entrypoints.data(), &num_eps);
      if (status != VA_STATUS_SUCCESS) {
        BOOST_LOG(error) << "Failed to query VA entrypoints: "sv << vaErrorStr(status);
        return (VAEntrypoint) 0;
      }
      entrypoints.resize(num_eps);

      // Sorted in order of descending preference
      VAEntrypoint ep_preferences[] = {
        VAEntrypointEncSliceLP,
        VAEntrypointEncSlice,
        VAEntrypointEncPicture
      };
      for (auto ep_pref : ep_preferences) {
        if (std::find(entrypoints.begin(), entrypoints.end(), ep_pref) != entrypoints.end()) {
          return ep_pref;
        }
      }

      return (VAEntrypoint) 0;
    }

    /**
     * @brief Determines if a given VA profile is supported.
     * @param profile The profile to match.
     * @return Boolean value indicating if the profile is supported.
     */
    bool is_va_profile_supported(VAProfile profile) {
      std::vector<VAProfile> profiles(vaMaxNumProfiles(va_display));
      int num_profs;
      auto status = vaQueryConfigProfiles(va_display, profiles.data(), &num_profs);
      if (status != VA_STATUS_SUCCESS) {
        BOOST_LOG(error) << "Failed to query VA profiles: "sv << vaErrorStr(status);
        return false;
      }
      profiles.resize(num_profs);

      return std::find(profiles.begin(), profiles.end(), profile) != profiles.end();
    }

    /**
     * @brief Determines the matching VA profile for the codec configuration.
     * @param ctx The FFmpeg codec context.
     * @return The matching VA profile or `VAProfileNone` on failure.
     */
    VAProfile get_va_profile(AVCodecContext *ctx) {
      if (ctx->codec_id == AV_CODEC_ID_H264) {
        // There's no VAAPI profile for H.264 4:4:4
        return VAProfileH264High;
      } else if (ctx->codec_id == AV_CODEC_ID_HEVC) {
        switch (ctx->profile) {
          case AV_PROFILE_HEVC_REXT:
            switch (av_pix_fmt_desc_get(ctx->sw_pix_fmt)->comp[0].depth) {
              case 10:
                return VAProfileHEVCMain444_10;
              case 8:
                return VAProfileHEVCMain444;
            }
            break;
          case AV_PROFILE_HEVC_MAIN_10:
            return VAProfileHEVCMain10;
          case AV_PROFILE_HEVC_MAIN:
            return VAProfileHEVCMain;
        }
      } else if (ctx->codec_id == AV_CODEC_ID_AV1) {
        switch (ctx->profile) {
          case AV_PROFILE_AV1_HIGH:
            return VAProfileAV1Profile1;
          case AV_PROFILE_AV1_MAIN:
            return VAProfileAV1Profile0;
        }
      }

      BOOST_LOG(error) << "Unknown encoder profile: "sv << ctx->profile;
      return VAProfileNone;
    }

    void init_codec_options(AVCodecContext *ctx, AVDictionary **options) override {
      auto va_profile = get_va_profile(ctx);
      if (va_profile == VAProfileNone || !is_va_profile_supported(va_profile)) {
        // Don't bother doing anything if the profile isn't supported
        return;
      }

      auto va_entrypoint = select_va_entrypoint(va_profile);
      if (va_entrypoint == 0) {
        // It's possible that only decoding is supported for this profile
        return;
      }

      auto vendor = vaQueryVendorString(va_display);

      if (va_entrypoint == VAEntrypointEncSliceLP) {
        BOOST_LOG(info) << "Using LP encoding mode"sv;
        av_dict_set_int(options, "low_power", 1, 0);
      } else {
        BOOST_LOG(info) << "Using normal encoding mode"sv;
      }

      VAConfigAttrib rc_attr = {VAConfigAttribRateControl};
      auto status = vaGetConfigAttributes(va_display, va_profile, va_entrypoint, &rc_attr, 1);
      if (status != VA_STATUS_SUCCESS) {
        // Stick to the default rate control (CQP)
        rc_attr.value = 0;
      }

      VAConfigAttrib slice_attr = {VAConfigAttribEncMaxSlices};
      status = vaGetConfigAttributes(va_display, va_profile, va_entrypoint, &slice_attr, 1);
      if (status != VA_STATUS_SUCCESS) {
        // Assume only a single slice is supported
        slice_attr.value = 1;
      }
      if (ctx->slices > slice_attr.value) {
        BOOST_LOG(info) << "Limiting slice count to encoder maximum: "sv << slice_attr.value;
        ctx->slices = slice_attr.value;
      }

      // Use VBR with a single frame VBV when the user forces it and for known good cases:
      // - Intel GPUs
      // - AV1
      //
      // VBR ensures the bitstream isn't full of filler data for bitrate undershoots and
      // single frame VBV ensures that we don't have large bitrate overshoots (at least
      // as much as they can be avoided without pre-analysis).
      //
      // When we have to resort to the default 1 second VBV for encoding quality reasons,
      // we stick to CBR in order to avoid encoding huge frames after bitrate undershoots
      // leave headroom available in the RC window.
      if (config::video.vaapi.strict_rc_buffer ||
          (vendor && strstr(vendor, "Intel")) ||
          ctx->codec_id == AV_CODEC_ID_AV1) {
        ctx->rc_buffer_size = ctx->bit_rate * ctx->framerate.den / ctx->framerate.num;

        if (rc_attr.value & VA_RC_VBR) {
          BOOST_LOG(info) << "Using VBR with single frame VBV size"sv;
          av_dict_set(options, "rc_mode", "VBR", 0);
        } else if (rc_attr.value & VA_RC_CBR) {
          BOOST_LOG(info) << "Using CBR with single frame VBV size"sv;
          av_dict_set(options, "rc_mode", "CBR", 0);
        } else {
          BOOST_LOG(warning) << "Using CQP with single frame VBV size"sv;
          av_dict_set_int(options, "qp", config::video.qp, 0);
        }
      } else if (!(rc_attr.value & (VA_RC_CBR | VA_RC_VBR))) {
        BOOST_LOG(warning) << "Using CQP rate control"sv;
        av_dict_set_int(options, "qp", config::video.qp, 0);
      } else {
        BOOST_LOG(info) << "Using default rate control"sv;
      }
    }

    int set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx_buf) override {
      this->hwframe.reset(frame);
      this->frame = frame;

      if (!frame->buf[0]) {
        if (av_hwframe_get_buffer(hw_frames_ctx_buf, frame, 0)) {
          BOOST_LOG(error) << "Couldn't get hwframe for VAAPI"sv;
          return -1;
        }
      }

      va::DRMPRIMESurfaceDescriptor prime;
      va::VASurfaceID surface = (std::uintptr_t) frame->data[3];
      auto hw_frames_ctx = (AVHWFramesContext *) hw_frames_ctx_buf->data;

      auto status = vaExportSurfaceHandle(
        this->va_display,
        surface,
        va::SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
        va::EXPORT_SURFACE_WRITE_ONLY | va::EXPORT_SURFACE_SEPARATE_LAYERS,
        &prime
      );
      if (status) {
        BOOST_LOG(error) << "Couldn't export va surface handle: ["sv << (int) surface << "]: "sv << vaErrorStr(status);

        return -1;
      }

      // Keep track of file descriptors
      std::array<file_t, egl::nv12_img_t::num_fds> fds;
      for (int x = 0; x < prime.num_objects; ++x) {
        fds[x] = prime.objects[x].fd;
      }

      if (prime.num_layers != 2) {
        BOOST_LOG(error) << "Invalid layer count for VA surface: expected 2, got "sv << prime.num_layers;
        return -1;
      }

      egl::surface_descriptor_t sds[2] = {};
      for (int plane = 0; plane < 2; ++plane) {
        auto &sd = sds[plane];
        auto &layer = prime.layers[plane];

        sd.fourcc = layer.drm_format;

        // UV plane is subsampled
        sd.width = prime.width / (plane == 0 ? 1 : 2);
        sd.height = prime.height / (plane == 0 ? 1 : 2);

        // The modifier must be the same for all planes
        sd.modifier = prime.objects[layer.object_index[0]].drm_format_modifier;

        std::fill_n(sd.fds, 4, -1);
        for (int x = 0; x < layer.num_planes; ++x) {
          sd.fds[x] = prime.objects[layer.object_index[x]].fd;
          sd.pitches[x] = layer.pitch[x];
          sd.offsets[x] = layer.offset[x];
        }
      }

      auto nv12_opt = egl::import_target(display.get(), std::move(fds), sds[0], sds[1]);
      if (!nv12_opt) {
        return -1;
      }

      auto sws_opt = egl::sws_t::make(width, height, frame->width, frame->height, hw_frames_ctx->sw_format);
      if (!sws_opt) {
        return -1;
      }

      this->sws = std::move(*sws_opt);
      this->nv12 = std::move(*nv12_opt);

      return 0;
    }

    void apply_colorspace() override {
      sws.apply_colorspace(colorspace);
    }

    va::display_t::pointer va_display;
    file_t file;

    gbm::gbm_t gbm;
    egl::display_t display;
    egl::ctx_t ctx;

    // This must be destroyed before display_t to ensure the GPU
    // driver is still loaded when vaDestroySurfaces() is called.
    frame_t hwframe;

    egl::sws_t sws;
    egl::nv12_t nv12;

    int width, height;
  };

  class va_ram_t: public va_t {
  public:
    int convert(platf::img_t &img) override {
      sws.load_ram(img);

      sws.convert(nv12->buf);
      return 0;
    }
  };

  class va_vram_t: public va_t {
  public:
    int convert(platf::img_t &img) override {
      auto &descriptor = (egl::img_descriptor_t &) img;

      if (descriptor.sequence == 0) {
        // For dummy images, use a blank RGB texture instead of importing a DMA-BUF
        rgb = egl::create_blank(img);
      } else if (descriptor.sequence > sequence) {
        sequence = descriptor.sequence;

        rgb = egl::rgb_t {};

        auto rgb_opt = egl::import_source(display.get(), descriptor.sd);

        if (!rgb_opt) {
          return -1;
        }

        rgb = std::move(*rgb_opt);
      }

      sws.load_vram(descriptor, offset_x, offset_y, rgb->tex[0]);

      sws.convert(nv12->buf);
      return 0;
    }

    int init(int in_width, int in_height, file_t &&render_device, int offset_x, int offset_y) {
      if (va_t::init(in_width, in_height, std::move(render_device))) {
        return -1;
      }

      sequence = 0;

      this->offset_x = offset_x;
      this->offset_y = offset_y;

      return 0;
    }

    std::uint64_t sequence;
    egl::rgb_t rgb;

    int offset_x, offset_y;
  };

  /**
   * This is a private structure of FFmpeg, I need this to manually create
   * a VAAPI hardware context
   *
   * xdisplay will not be used internally by FFmpeg
   */
  typedef struct VAAPIDevicePriv {
    union {
      void *xdisplay;
      int fd;
    } drm;

    int drm_fd;
  } VAAPIDevicePriv;

  /**
   * VAAPI connection details.
   *
   * Allocated as AVHWDeviceContext.hwctx
   */
  typedef struct AVVAAPIDeviceContext {
    /**
     * The VADisplay handle, to be filled by the user.
     */
    va::VADisplay display;
    /**
     * Driver quirks to apply - this is filled by av_hwdevice_ctx_init(),
     * with reference to a table of known drivers, unless the
     * AV_VAAPI_DRIVER_QUIRK_USER_SET bit is already present.  The user
     * may need to refer to this field when performing any later
     * operations using VAAPI with the same VADisplay.
     */
    unsigned int driver_quirks;
  } AVVAAPIDeviceContext;

  static void __log(void *level, const char *msg) {
    BOOST_LOG(*(boost::log::sources::severity_logger<int> *) level) << msg;
  }

  static void vaapi_hwdevice_ctx_free(AVHWDeviceContext *ctx) {
    auto hwctx = (AVVAAPIDeviceContext *) ctx->hwctx;
    auto priv = (VAAPIDevicePriv *) ctx->user_opaque;

    vaTerminate(hwctx->display);
    close(priv->drm_fd);
    av_freep(&priv);
  }

  int vaapi_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *base, AVBufferRef **hw_device_buf) {
    auto va = (va::va_t *) base;
    auto fd = dup(va->file.el);

    auto *priv = (VAAPIDevicePriv *) av_mallocz(sizeof(VAAPIDevicePriv));
    priv->drm_fd = fd;

    auto fg = util::fail_guard([fd, priv]() {
      close(fd);
      av_free(priv);
    });

    va::display_t display {vaGetDisplayDRM(fd)};
    if (!display) {
      auto render_device = config::video.adapter_name.empty() ? "/dev/dri/renderD128" : config::video.adapter_name.c_str();

      BOOST_LOG(error) << "Couldn't open a va display from DRM with device: "sv << render_device;
      return -1;
    }

    va->va_display = display.get();

    vaSetErrorCallback(display.get(), __log, &error);
    vaSetErrorCallback(display.get(), __log, &info);

    int major, minor;
    auto status = vaInitialize(display.get(), &major, &minor);
    if (status) {
      BOOST_LOG(error) << "Couldn't initialize va display: "sv << vaErrorStr(status);
      return -1;
    }

    BOOST_LOG(info) << "vaapi vendor: "sv << vaQueryVendorString(display.get());

    *hw_device_buf = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
    auto ctx = (AVHWDeviceContext *) (*hw_device_buf)->data;
    auto hwctx = (AVVAAPIDeviceContext *) ctx->hwctx;

    // Ownership of the VADisplay and DRM fd is now ours to manage via the free() function
    hwctx->display = display.release();
    ctx->user_opaque = priv;
    ctx->free = vaapi_hwdevice_ctx_free;
    fg.disable();

    auto err = av_hwdevice_ctx_init(*hw_device_buf);
    if (err) {
      char err_str[AV_ERROR_MAX_STRING_SIZE] {0};
      BOOST_LOG(error) << "Failed to create FFMpeg hardware device context: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, err);

      return err;
    }

    return 0;
  }

  static bool query(display_t::pointer display, VAProfile profile) {
    std::vector<VAEntrypoint> entrypoints;
    entrypoints.resize(vaMaxNumEntrypoints(display));

    int count;
    auto status = vaQueryConfigEntrypoints(display, profile, entrypoints.data(), &count);
    if (status) {
      BOOST_LOG(error) << "Couldn't query entrypoints: "sv << vaErrorStr(status);
      return false;
    }
    entrypoints.resize(count);

    for (auto entrypoint : entrypoints) {
      if (entrypoint == VAEntrypointEncSlice || entrypoint == VAEntrypointEncSliceLP) {
        return true;
      }
    }

    return false;
  }

  bool validate(int fd) {
    va::display_t display {vaGetDisplayDRM(fd)};
    if (!display) {
      char string[1024];

      auto bytes = readlink(std::format("/proc/self/fd/{}", fd).c_str(), string, sizeof(string));

      std::string_view render_device {string, (std::size_t) bytes};

      BOOST_LOG(error) << "Couldn't open a va display from DRM with device: "sv << render_device;
      return false;
    }

    int major, minor;
    auto status = vaInitialize(display.get(), &major, &minor);
    if (status) {
      BOOST_LOG(error) << "Couldn't initialize va display: "sv << vaErrorStr(status);
      return false;
    }

    if (!query(display.get(), VAProfileH264Main)) {
      return false;
    }

    if (video::active_hevc_mode > 1 && !query(display.get(), VAProfileHEVCMain)) {
      return false;
    }

    if (video::active_hevc_mode > 2 && !query(display.get(), VAProfileHEVCMain10)) {
      return false;
    }

    return true;
  }

  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, file_t &&card, int offset_x, int offset_y, bool vram) {
    if (vram) {
      auto egl = std::make_unique<va::va_vram_t>();
      if (egl->init(width, height, std::move(card), offset_x, offset_y)) {
        return nullptr;
      }

      return egl;
    }

    else {
      auto egl = std::make_unique<va::va_ram_t>();
      if (egl->init(width, height, std::move(card))) {
        return nullptr;
      }

      return egl;
    }
  }

  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, int offset_x, int offset_y, bool vram) {
    auto render_device = config::video.adapter_name.empty() ? "/dev/dri/renderD128" : config::video.adapter_name.c_str();

    file_t file = open(render_device, O_RDWR);
    if (file.el < 0) {
      char string[1024];
      BOOST_LOG(error) << "Couldn't open "sv << render_device << ": " << strerror_r(errno, string, sizeof(string));

      return nullptr;
    }

    return make_avcodec_encode_device(width, height, std::move(file), offset_x, offset_y, vram);
  }

  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, bool vram) {
    return make_avcodec_encode_device(width, height, 0, 0, vram);
  }
}  // namespace va
