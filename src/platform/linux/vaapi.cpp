#include <sstream>
#include <string>

#include <fcntl.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "graphics.h"
#include "misc.h"
#include "src/config.h"
#include "src/main.h"
#include "src/platform/common.h"
#include "src/utility.h"

using namespace std::literals;

extern "C" struct AVBufferRef;

namespace va {
constexpr auto SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2 = 0x40000000;
constexpr auto EXPORT_SURFACE_WRITE_ONLY           = 0x0002;
constexpr auto EXPORT_SURFACE_COMPOSED_LAYERS      = 0x0008;

using VADisplay   = void *;
using VAStatus    = int;
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

    /*
     *  Total size of this object (may include regions which are
     *  not part of the surface).
     */
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

/** Currently defined profiles */
enum class profile_e {
  // Profile ID used for video processing.
  ProfileNone             = -1,
  MPEG2Simple             = 0,
  MPEG2Main               = 1,
  MPEG4Simple             = 2,
  MPEG4AdvancedSimple     = 3,
  MPEG4Main               = 4,
  H264Baseline            = 5,
  H264Main                = 6,
  H264High                = 7,
  VC1Simple               = 8,
  VC1Main                 = 9,
  VC1Advanced             = 10,
  H263Baseline            = 11,
  JPEGBaseline            = 12,
  H264ConstrainedBaseline = 13,
  VP8Version0_3           = 14,
  H264MultiviewHigh       = 15,
  H264StereoHigh          = 16,
  HEVCMain                = 17,
  HEVCMain10              = 18,
  VP9Profile0             = 19,
  VP9Profile1             = 20,
  VP9Profile2             = 21,
  VP9Profile3             = 22,
  HEVCMain12              = 23,
  HEVCMain422_10          = 24,
  HEVCMain422_12          = 25,
  HEVCMain444             = 26,
  HEVCMain444_10          = 27,
  HEVCMain444_12          = 28,
  HEVCSccMain             = 29,
  HEVCSccMain10           = 30,
  HEVCSccMain444          = 31,
  AV1Profile0             = 32,
  AV1Profile1             = 33,
  HEVCSccMain444_10       = 34,

  // Profile ID used for protected video playback.
  Protected = 35
};

enum class entry_e {
  VLD        = 1,
  IZZ        = 2,
  IDCT       = 3,
  MoComp     = 4,
  Deblocking = 5,
  EncSlice   = 6, /* slice level encode */
  EncPicture = 7, /* pictuer encode, JPEG, etc */
  /*
     * For an implementation that supports a low power/high performance variant
     * for slice level encode, it can choose to expose the
     * VAEntrypointEncSliceLP entrypoint. Certain encoding tools may not be
     * available with this entrypoint (e.g. interlace, MBAFF) and the
     * application can query the encoding configuration attributes to find
     * out more details if this entrypoint is supported.
     */
  EncSliceLP = 8,
  VideoProc  = 10, /**< Video pre/post-processing. */
  /**
     * \brief FEI
     *
     * The purpose of FEI (Flexible Encoding Infrastructure) is to allow applications to
     * have more controls and trade off quality for speed with their own IPs.
     * The application can optionally provide input to ENC for extra encode control
     * and get the output from ENC. Application can chose to modify the ENC
     * output/PAK input during encoding, but the performance impact is significant.
     *
     * On top of the existing buffers for normal encode, there will be
     * one extra input buffer (VAEncMiscParameterFEIFrameControl) and
     * three extra output buffers (VAEncFEIMVBufferType, VAEncFEIMBModeBufferType
     * and VAEncFEIDistortionBufferType) for FEI entry function.
     * If separate PAK is set, two extra input buffers
     * (VAEncFEIMVBufferType, VAEncFEIMBModeBufferType) are needed for PAK input.
     **/
  FEI = 11,
  /**
     * \brief Stats
     *
     * A pre-processing function for getting some statistics and motion vectors is added,
     * and some extra controls for Encode pipeline are provided. The application can
     * optionally call the statistics function to get motion vectors and statistics like
     * variances, distortions before calling Encode function via this entry point.
     *
     * Checking whether Statistics is supported can be performed with vaQueryConfigEntrypoints().
     * If Statistics entry point is supported, then the list of returned entry-points will
     * include #Stats. Supported pixel format, maximum resolution and statistics
     * specific attributes can be obtained via normal attribute query. One input buffer
     * (VAStatsStatisticsParameterBufferType) and one or two output buffers
     * (VAStatsStatisticsBufferType, VAStatsStatisticsBottomFieldBufferType (for interlace only)
     * and VAStatsMVBufferType) are needed for this entry point.
     **/
  Stats = 12,
  /**
     * \brief ProtectedTEEComm
     *
     * A function for communicating with TEE (Trusted Execution Environment).
     **/
  ProtectedTEEComm = 13,
  /**
     * \brief ProtectedContent
     *
     * A function for protected content to decrypt encrypted content.
     **/
  ProtectedContent = 14,
};


typedef VAStatus (*queryConfigEntrypoints_fn)(VADisplay dpy, profile_e profile, entry_e *entrypoint_list, int *num_entrypoints);
typedef int (*maxNumEntrypoints_fn)(VADisplay dpy);
typedef VADisplay (*getDisplayDRM_fn)(int fd);
typedef VAStatus (*terminate_fn)(VADisplay dpy);
typedef VAStatus (*initialize_fn)(VADisplay dpy, int *major_version, int *minor_version);
typedef const char *(*errorStr_fn)(VAStatus error_status);
typedef void (*VAMessageCallback)(void *user_context, const char *message);
typedef VAMessageCallback (*setErrorCallback_fn)(VADisplay dpy, VAMessageCallback callback, void *user_context);
typedef VAMessageCallback (*setInfoCallback_fn)(VADisplay dpy, VAMessageCallback callback, void *user_context);
typedef const char *(*queryVendorString_fn)(VADisplay dpy);
typedef VAStatus (*exportSurfaceHandle_fn)(
  VADisplay dpy, VASurfaceID surface_id,
  uint32_t mem_type, uint32_t flags,
  void *descriptor);

static maxNumEntrypoints_fn maxNumEntrypoints;
static queryConfigEntrypoints_fn queryConfigEntrypoints;
static getDisplayDRM_fn getDisplayDRM;
static terminate_fn terminate;
static initialize_fn initialize;
static errorStr_fn errorStr;
static setErrorCallback_fn setErrorCallback;
static setInfoCallback_fn setInfoCallback;
static queryVendorString_fn queryVendorString;
static exportSurfaceHandle_fn exportSurfaceHandle;

using display_t = util::dyn_safe_ptr_v2<void, VAStatus, &terminate>;

int init_main_va() {
  static void *handle { nullptr };
  static bool funcs_loaded = false;

  if(funcs_loaded) return 0;

  if(!handle) {
    handle = dyn::handle({ "libva.so.2", "libva.so" });
    if(!handle) {
      return -1;
    }
  }

  std::vector<std::tuple<dyn::apiproc *, const char *>> funcs {
    { (dyn::apiproc *)&maxNumEntrypoints, "vaMaxNumEntrypoints" },
    { (dyn::apiproc *)&queryConfigEntrypoints, "vaQueryConfigEntrypoints" },
    { (dyn::apiproc *)&terminate, "vaTerminate" },
    { (dyn::apiproc *)&initialize, "vaInitialize" },
    { (dyn::apiproc *)&errorStr, "vaErrorStr" },
    { (dyn::apiproc *)&setErrorCallback, "vaSetErrorCallback" },
    { (dyn::apiproc *)&setInfoCallback, "vaSetInfoCallback" },
    { (dyn::apiproc *)&queryVendorString, "vaQueryVendorString" },
    { (dyn::apiproc *)&exportSurfaceHandle, "vaExportSurfaceHandle" },
  };

  if(dyn::load(handle, funcs)) {
    return -1;
  }

  funcs_loaded = true;
  return 0;
}

int init() {
  if(init_main_va()) {
    return -1;
  }

  static void *handle { nullptr };
  static bool funcs_loaded = false;

  if(funcs_loaded) return 0;

  if(!handle) {
    handle = dyn::handle({ "libva-drm.so.2", "libva-drm.so" });
    if(!handle) {
      return -1;
    }
  }

  std::vector<std::tuple<dyn::apiproc *, const char *>> funcs {
    { (dyn::apiproc *)&getDisplayDRM, "vaGetDisplayDRM" },
  };

  if(dyn::load(handle, funcs)) {
    return -1;
  }

  funcs_loaded = true;
  return 0;
}

int vaapi_make_hwdevice_ctx(platf::hwdevice_t *base, AVBufferRef **hw_device_buf);

class va_t : public platf::hwdevice_t {
public:
  int init(int in_width, int in_height, file_t &&render_device) {
    file = std::move(render_device);

    if(!va::initialize || !gbm::create_device) {
      if(!va::initialize) BOOST_LOG(warning) << "libva not initialized"sv;
      if(!gbm::create_device) BOOST_LOG(warning) << "libgbm not initialized"sv;
      return -1;
    }

    this->data = (void *)vaapi_make_hwdevice_ctx;

    gbm.reset(gbm::create_device(file.el));
    if(!gbm) {
      char string[1024];
      BOOST_LOG(error) << "Couldn't create GBM device: ["sv << strerror_r(errno, string, sizeof(string)) << ']';
      return -1;
    }

    display = egl::make_display(gbm.get());
    if(!display) {
      return -1;
    }

    auto ctx_opt = egl::make_ctx(display.get());
    if(!ctx_opt) {
      return -1;
    }

    ctx = std::move(*ctx_opt);

    width  = in_width;
    height = in_height;

    return 0;
  }

  int set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) override {
    this->hwframe.reset(frame);
    this->frame = frame;

    if(!frame->buf[0]) {
      if(av_hwframe_get_buffer(hw_frames_ctx, frame, 0)) {
        BOOST_LOG(error) << "Couldn't get hwframe for VAAPI"sv;
        return -1;
      }
    }

    va::DRMPRIMESurfaceDescriptor prime;
    va::VASurfaceID surface = (std::uintptr_t)frame->data[3];

    auto status = va::exportSurfaceHandle(
      this->va_display,
      surface,
      va::SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
      va::EXPORT_SURFACE_WRITE_ONLY | va::EXPORT_SURFACE_COMPOSED_LAYERS,
      &prime);
    if(status) {

      BOOST_LOG(error) << "Couldn't export va surface handle: ["sv << (int)surface << "]: "sv << va::errorStr(status);

      return -1;
    }

    // Keep track of file descriptors
    std::array<file_t, egl::nv12_img_t::num_fds> fds;
    for(int x = 0; x < prime.num_objects; ++x) {
      fds[x] = prime.objects[x].fd;
    }

    auto nv12_opt = egl::import_target(
      display.get(),
      std::move(fds),
      { (int)prime.width,
        (int)prime.height,
        { prime.objects[prime.layers[0].object_index[0]].fd, -1, -1, -1 },
        0,
        0,
        { prime.layers[0].pitch[0] },
        { prime.layers[0].offset[0] } },
      { (int)prime.width / 2,
        (int)prime.height / 2,
        { prime.objects[prime.layers[0].object_index[1]].fd, -1, -1, -1 },
        0,
        0,
        { prime.layers[0].pitch[1] },
        { prime.layers[0].offset[1] } });

    if(!nv12_opt) {
      return -1;
    }

    auto sws_opt = egl::sws_t::make(width, height, frame->width, frame->height);
    if(!sws_opt) {
      return -1;
    }

    this->sws  = std::move(*sws_opt);
    this->nv12 = std::move(*nv12_opt);

    return 0;
  }

  void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) override {
    sws.set_colorspace(colorspace, color_range);
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

class va_ram_t : public va_t {
public:
  int convert(platf::img_t &img) override {
    sws.load_ram(img);

    sws.convert(nv12->buf);
    return 0;
  }
};

class va_vram_t : public va_t {
public:
  int convert(platf::img_t &img) override {
    auto &descriptor = (egl::img_descriptor_t &)img;

    if(descriptor.sequence > sequence) {
      sequence = descriptor.sequence;

      rgb = egl::rgb_t {};

      auto rgb_opt = egl::import_source(display.get(), descriptor.sd);

      if(!rgb_opt) {
        return -1;
      }

      rgb = std::move(*rgb_opt);
    }

    sws.load_vram(descriptor, offset_x, offset_y, rgb->tex[0]);

    sws.convert(nv12->buf);
    return 0;
  }

  int init(int in_width, int in_height, file_t &&render_device, int offset_x, int offset_y) {
    if(va_t::init(in_width, in_height, std::move(render_device))) {
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
  BOOST_LOG(*(boost::log::sources::severity_logger<int> *)level) << msg;
}

int vaapi_make_hwdevice_ctx(platf::hwdevice_t *base, AVBufferRef **hw_device_buf) {
  if(!va::initialize) {
    BOOST_LOG(warning) << "libva not loaded"sv;
    return -1;
  }

  if(!va::getDisplayDRM) {
    BOOST_LOG(warning) << "libva-drm not loaded"sv;
    return -1;
  }

  auto va = (va::va_t *)base;
  auto fd = dup(va->file.el);

  auto *priv   = (VAAPIDevicePriv *)av_mallocz(sizeof(VAAPIDevicePriv));
  priv->drm_fd = fd;
  priv->drm.fd = fd;

  auto fg = util::fail_guard([fd, priv]() {
    close(fd);
    av_free(priv);
  });

  va::display_t display { va::getDisplayDRM(fd) };
  if(!display) {
    auto render_device = config::video.adapter_name.empty() ? "/dev/dri/renderD128" : config::video.adapter_name.c_str();

    BOOST_LOG(error) << "Couldn't open a va display from DRM with device: "sv << render_device;
    return -1;
  }

  va->va_display = display.get();

  va::setErrorCallback(display.get(), __log, &error);
  va::setErrorCallback(display.get(), __log, &info);

  int major, minor;
  auto status = va::initialize(display.get(), &major, &minor);
  if(status) {
    BOOST_LOG(error) << "Couldn't initialize va display: "sv << va::errorStr(status);
    return -1;
  }

  BOOST_LOG(debug) << "vaapi vendor: "sv << va::queryVendorString(display.get());

  *hw_device_buf = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
  auto ctx       = (AVVAAPIDeviceContext *)((AVHWDeviceContext *)(*hw_device_buf)->data)->hwctx;
  ctx->display   = display.release();

  fg.disable();

  auto err = av_hwdevice_ctx_init(*hw_device_buf);
  if(err) {
    char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
    BOOST_LOG(error) << "Failed to create FFMpeg hardware device context: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, err);

    return err;
  }

  return 0;
}

static bool query(display_t::pointer display, profile_e profile) {
  std::vector<entry_e> entrypoints;
  entrypoints.resize(maxNumEntrypoints(display));

  int count;
  auto status = queryConfigEntrypoints(display, profile, entrypoints.data(), &count);
  if(status) {
    BOOST_LOG(error) << "Couldn't query entrypoints: "sv << va::errorStr(status);
    return false;
  }
  entrypoints.resize(count);

  for(auto entrypoint : entrypoints) {
    if(entrypoint == entry_e::EncSlice || entrypoint == entry_e::EncSliceLP) {
      return true;
    }
  }

  return false;
}

bool validate(int fd) {
  if(init()) {
    return false;
  }

  va::display_t display { va::getDisplayDRM(fd) };
  if(!display) {
    char string[1024];

    auto bytes = readlink(("/proc/self/fd/" + std::to_string(fd)).c_str(), string, sizeof(string));

    std::string_view render_device { string, (std::size_t)bytes };

    BOOST_LOG(error) << "Couldn't open a va display from DRM with device: "sv << render_device;
    return false;
  }

  int major, minor;
  auto status = initialize(display.get(), &major, &minor);
  if(status) {
    BOOST_LOG(error) << "Couldn't initialize va display: "sv << va::errorStr(status);
    return false;
  }

  if(!query(display.get(), profile_e::H264Main)) {
    return false;
  }

  if(config::video.hevc_mode > 1 && !query(display.get(), profile_e::HEVCMain)) {
    return false;
  }

  if(config::video.hevc_mode > 2 && !query(display.get(), profile_e::HEVCMain10)) {
    return false;
  }

  return true;
}

std::shared_ptr<platf::hwdevice_t> make_hwdevice(int width, int height, file_t &&card, int offset_x, int offset_y, bool vram) {
  if(vram) {
    auto egl = std::make_shared<va::va_vram_t>();
    if(egl->init(width, height, std::move(card), offset_x, offset_y)) {
      return nullptr;
    }

    return egl;
  }

  else {
    auto egl = std::make_shared<va::va_ram_t>();
    if(egl->init(width, height, std::move(card))) {
      return nullptr;
    }

    return egl;
  }
}

std::shared_ptr<platf::hwdevice_t> make_hwdevice(int width, int height, int offset_x, int offset_y, bool vram) {
  auto render_device = config::video.adapter_name.empty() ? "/dev/dri/renderD128" : config::video.adapter_name.c_str();

  file_t file = open(render_device, O_RDWR);
  if(file.el < 0) {
    char string[1024];
    BOOST_LOG(error) << "Couldn't open "sv << render_device << ": " << strerror_r(errno, string, sizeof(string));

    return nullptr;
  }

  return make_hwdevice(width, height, std::move(file), offset_x, offset_y, vram);
}

std::shared_ptr<platf::hwdevice_t> make_hwdevice(int width, int height, bool vram) {
  return make_hwdevice(width, height, 0, 0, vram);
}
} // namespace va
