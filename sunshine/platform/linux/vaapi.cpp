#include <sstream>
#include <string>

#include <fcntl.h>

#include <glad/egl.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "graphics.h"
#include "misc.h"
#include "sunshine/config.h"
#include "sunshine/main.h"
#include "sunshine/platform/common.h"
#include "sunshine/utility.h"

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

getDisplayDRM_fn getDisplayDRM;
terminate_fn terminate;
initialize_fn initialize;
errorStr_fn errorStr;
setErrorCallback_fn setErrorCallback;
setInfoCallback_fn setInfoCallback;
queryVendorString_fn queryVendorString;
exportSurfaceHandle_fn exportSurfaceHandle;

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

  int _set_frame(AVFrame *frame) {
    this->hwframe.reset(frame);
    this->frame = frame;

    if(av_hwframe_get_buffer(frame->hw_frames_ctx, frame, 0)) {
      BOOST_LOG(error) << "Couldn't get hwframe for VAAPI"sv;

      return -1;
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
      {
        prime.objects[prime.layers[0].object_index[0]].fd,
        (int)prime.width,
        (int)prime.height,
        (int)prime.layers[0].offset[0],
        (int)prime.layers[0].pitch[0],
      },
      {
        prime.objects[prime.layers[0].object_index[1]].fd,
        (int)prime.width / 2,
        (int)prime.height / 2,
        (int)prime.layers[0].offset[1],
        (int)prime.layers[0].pitch[1],
      });

    if(!nv12_opt) {
      return -1;
    }

    this->nv12 = std::move(*nv12_opt);

    return 0;
  }

  void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) override {
    sws.set_colorspace(colorspace, color_range);
  }

  va::display_t::pointer va_display;
  file_t file;

  frame_t hwframe;

  gbm::gbm_t gbm;
  egl::display_t display;
  egl::ctx_t ctx;

  egl::sws_t sws;
  egl::nv12_t nv12;

  int width, height;
};

class va_ram_t : public va_t {
public:
  int convert(platf::img_t &img) override {
    sws.load_ram(img);

    sws.convert(nv12);
    return 0;
  }

  int set_frame(AVFrame *frame) override {
    if(_set_frame(frame)) {
      return -1;
    }

    auto sws_opt = egl::sws_t::make(width, height, frame->width, frame->height);
    if(!sws_opt) {
      return -1;
    }

    this->sws = std::move(*sws_opt);

    return 0;
  }
};

class va_vram_t : public va_t {
public:
  int convert(platf::img_t &img) override {
    sws.load_vram(img, offset_x, offset_y, framebuffer[0]);

    sws.convert(nv12);
    return 0;
  }

  int init(int in_width, int in_height, file_t &&render_device, int offset_x, int offset_y, const egl::surface_descriptor_t &sd) {
    if(va_t::init(in_width, in_height, std::move(render_device))) {
      return -1;
    }

    auto rgb_opt = egl::import_source(display.get(), sd);
    if(!rgb_opt) {
      return -1;
    }

    rgb = std::move(*rgb_opt);

    framebuffer = gl::frame_buf_t::make(1);
    framebuffer.bind(std::begin(rgb->tex), std::end(rgb->tex));

    this->offset_x = offset_x;
    this->offset_y = offset_y;

    return 0;
  }

  int set_frame(AVFrame *frame) override {
    if(_set_frame(frame)) {
      return -1;
    }

    auto sws_opt = egl::sws_t::make(width, height, frame->width, frame->height);
    if(!sws_opt) {
      return -1;
    }

    this->sws = std::move(*sws_opt);

    return 0;
  }

  file_t fb_fd;

  egl::rgb_t rgb;
  gl::frame_buf_t framebuffer;

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

std::shared_ptr<platf::hwdevice_t> make_hwdevice(int width, int height) {
  auto render_device = config::video.adapter_name.empty() ? "/dev/dri/renderD128" : config::video.adapter_name.c_str();

  file_t file = open(render_device, O_RDWR);
  if(file.el < 0) {
    char string[1024];
    BOOST_LOG(error) << "Couldn't open "sv << render_device << ": " << strerror_r(errno, string, sizeof(string));

    return nullptr;
  }

  auto egl = std::make_shared<va::va_ram_t>();
  if(egl->init(width, height, std::move(file))) {
    return nullptr;
  }

  return egl;
}

std::shared_ptr<platf::hwdevice_t> make_hwdevice(int width, int height, file_t &&card, int offset_x, int offset_y, const egl::surface_descriptor_t &sd) {
  auto egl = std::make_shared<va::va_vram_t>();
  if(egl->init(width, height, std::move(card), offset_x, offset_y, sd)) {
    return nullptr;
  }

  return egl;
}
} // namespace va