#include <bitset>

#include <NvFBC.h>
#include <ffnvcodec/dynlink_loader.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/imgutils.h>
}

#include "cuda.h"
#include "graphics.h"
#include "src/main.h"
#include "src/utility.h"
#include "src/video.h"
#include "wayland.h"

#define SUNSHINE_STRINGVIEW_HELPER(x) x##sv
#define SUNSHINE_STRINGVIEW(x) SUNSHINE_STRINGVIEW_HELPER(x)

#define CU_CHECK(x, y) \
  if(check((x), SUNSHINE_STRINGVIEW(y ": "))) return -1

#define CU_CHECK_IGNORE(x, y) \
  check((x), SUNSHINE_STRINGVIEW(y ": "))

using namespace std::literals;
namespace cuda {
constexpr auto cudaDevAttrMaxThreadsPerBlock          = (CUdevice_attribute)1;
constexpr auto cudaDevAttrMaxThreadsPerMultiProcessor = (CUdevice_attribute)39;

void pass_error(const std::string_view &sv, const char *name, const char *description) {
  BOOST_LOG(error) << sv << name << ':' << description;
}

void cff(CudaFunctions *cf) {
  cuda_free_functions(&cf);
}

using cdf_t = util::safe_ptr<CudaFunctions, cff>;

static cdf_t cdf;

inline static int check(CUresult result, const std::string_view &sv) {
  if(result != CUDA_SUCCESS) {
    const char *name;
    const char *description;

    cdf->cuGetErrorName(result, &name);
    cdf->cuGetErrorString(result, &description);

    BOOST_LOG(error) << sv << name << ':' << description;
    return -1;
  }

  return 0;
}

void freeStream(CUstream stream) {
  CU_CHECK_IGNORE(cdf->cuStreamDestroy(stream), "Couldn't destroy cuda stream");
}

class img_t : public platf::img_t {
public:
  tex_t tex;
};

int init() {
  auto status = cuda_load_functions(&cdf, nullptr);
  if(status) {
    BOOST_LOG(error) << "Couldn't load cuda: "sv << status;

    return -1;
  }

  CU_CHECK(cdf->cuInit(0), "Couldn't initialize cuda");

  return 0;
}

class cuda_t : public platf::hwdevice_t {
public:
  int init(int in_width, int in_height) {
    if(!cdf) {
      BOOST_LOG(warning) << "cuda not initialized"sv;
      return -1;
    }

    data = (void *)0x1;

    width  = in_width;
    height = in_height;

    return 0;
  }

  int set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) override {
    this->hwframe.reset(frame);
    this->frame = frame;

    auto hwframe_ctx = (AVHWFramesContext *)hw_frames_ctx->data;
    if(hwframe_ctx->sw_format != AV_PIX_FMT_NV12) {
      BOOST_LOG(error) << "cuda::cuda_t doesn't support any format other than AV_PIX_FMT_NV12"sv;
      return -1;
    }

    if(!frame->buf[0]) {
      if(av_hwframe_get_buffer(hw_frames_ctx, frame, 0)) {
        BOOST_LOG(error) << "Couldn't get hwframe for NVENC"sv;
        return -1;
      }
    }

    auto cuda_ctx = (AVCUDADeviceContext *)hwframe_ctx->device_ctx->hwctx;

    stream = make_stream();
    if(!stream) {
      return -1;
    }

    cuda_ctx->stream = stream.get();

    auto sws_opt = sws_t::make(width, height, frame->width, frame->height, width * 4);
    if(!sws_opt) {
      return -1;
    }

    sws = std::move(*sws_opt);

    linear_interpolation = width != frame->width || height != frame->height;

    return 0;
  }

  void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) override {
    sws.set_colorspace(colorspace, color_range);

    auto tex = tex_t::make(height, width * 4);
    if(!tex) {
      return;
    }

    // The default green color is ugly.
    // Update the background color
    platf::img_t img;
    img.width       = width;
    img.height      = height;
    img.pixel_pitch = 4;
    img.row_pitch   = img.width * img.pixel_pitch;

    std::vector<std::uint8_t> image_data;
    image_data.resize(img.row_pitch * img.height);

    img.data = image_data.data();

    if(sws.load_ram(img, tex->array)) {
      return;
    }

    sws.convert(frame->data[0], frame->data[1], frame->linesize[0], frame->linesize[1], tex->texture.linear, stream.get(), { frame->width, frame->height, 0, 0 });
  }

  cudaTextureObject_t tex_obj(const tex_t &tex) const {
    return linear_interpolation ? tex.texture.linear : tex.texture.point;
  }

  stream_t stream;
  frame_t hwframe;

  int width, height;

  // When heigth and width don't change, it's not necessary to use linear interpolation
  bool linear_interpolation;

  sws_t sws;
};

class cuda_ram_t : public cuda_t {
public:
  int convert(platf::img_t &img) override {
    return sws.load_ram(img, tex.array) || sws.convert(frame->data[0], frame->data[1], frame->linesize[0], frame->linesize[1], tex_obj(tex), stream.get());
  }

  int set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) {
    if(cuda_t::set_frame(frame, hw_frames_ctx)) {
      return -1;
    }

    auto tex_opt = tex_t::make(height, width * 4);
    if(!tex_opt) {
      return -1;
    }

    tex = std::move(*tex_opt);

    return 0;
  }

  tex_t tex;
};

class cuda_vram_t : public cuda_t {
public:
  int convert(platf::img_t &img) override {
    return sws.convert(frame->data[0], frame->data[1], frame->linesize[0], frame->linesize[1], tex_obj(((img_t *)&img)->tex), stream.get());
  }
};

std::shared_ptr<platf::hwdevice_t> make_hwdevice(int width, int height, bool vram) {
  if(init()) {
    return nullptr;
  }

  std::shared_ptr<cuda_t> cuda;

  if(vram) {
    cuda = std::make_shared<cuda_vram_t>();
  }
  else {
    cuda = std::make_shared<cuda_ram_t>();
  }

  if(cuda->init(width, height)) {
    return nullptr;
  }

  return cuda;
}

namespace nvfbc {
static PNVFBCCREATEINSTANCE createInstance {};
static NVFBC_API_FUNCTION_LIST func { NVFBC_VERSION };

static constexpr inline NVFBC_BOOL nv_bool(bool b) {
  return b ? NVFBC_TRUE : NVFBC_FALSE;
}

static void *handle { nullptr };
int init() {
  static bool funcs_loaded = false;

  if(funcs_loaded) return 0;

  if(!handle) {
    handle = dyn::handle({ "libnvidia-fbc.so.1", "libnvidia-fbc.so" });
    if(!handle) {
      return -1;
    }
  }

  std::vector<std::tuple<dyn::apiproc *, const char *>> funcs {
    { (dyn::apiproc *)&createInstance, "NvFBCCreateInstance" },
  };

  if(dyn::load(handle, funcs)) {
    dlclose(handle);
    handle = nullptr;

    return -1;
  }

  auto status = cuda::nvfbc::createInstance(&cuda::nvfbc::func);
  if(status) {
    BOOST_LOG(error) << "Unable to create NvFBC instance"sv;

    dlclose(handle);
    handle = nullptr;
    return -1;
  }

  funcs_loaded = true;
  return 0;
}

class ctx_t {
public:
  ctx_t(NVFBC_SESSION_HANDLE handle) {
    NVFBC_BIND_CONTEXT_PARAMS params { NVFBC_BIND_CONTEXT_PARAMS_VER };

    if(func.nvFBCBindContext(handle, &params)) {
      BOOST_LOG(error) << "Couldn't bind NvFBC context to current thread: " << func.nvFBCGetLastErrorStr(handle);
    }

    this->handle = handle;
  }

  ~ctx_t() {
    NVFBC_RELEASE_CONTEXT_PARAMS params { NVFBC_RELEASE_CONTEXT_PARAMS_VER };
    if(func.nvFBCReleaseContext(handle, &params)) {
      BOOST_LOG(error) << "Couldn't release NvFBC context from current thread: " << func.nvFBCGetLastErrorStr(handle);
    }
  }

  NVFBC_SESSION_HANDLE handle;
};

class handle_t {
  enum flag_e {
    SESSION_HANDLE,
    SESSION_CAPTURE,
    MAX_FLAGS,
  };

public:
  handle_t() = default;
  handle_t(handle_t &&other) : handle_flags { other.handle_flags }, handle { other.handle } {
    other.handle_flags.reset();
  }

  handle_t &operator=(handle_t &&other) {
    std::swap(handle_flags, other.handle_flags);
    std::swap(handle, other.handle);

    return *this;
  }

  static std::optional<handle_t> make() {
    NVFBC_CREATE_HANDLE_PARAMS params { NVFBC_CREATE_HANDLE_PARAMS_VER };

    handle_t handle;
    auto status = func.nvFBCCreateHandle(&handle.handle, &params);
    if(status) {
      BOOST_LOG(error) << "Failed to create session: "sv << handle.last_error();

      return std::nullopt;
    }

    handle.handle_flags[SESSION_HANDLE] = true;

    return std::move(handle);
  }

  const char *last_error() {
    return func.nvFBCGetLastErrorStr(handle);
  }

  std::optional<NVFBC_GET_STATUS_PARAMS> status() {
    NVFBC_GET_STATUS_PARAMS params { NVFBC_GET_STATUS_PARAMS_VER };

    auto status = func.nvFBCGetStatus(handle, &params);
    if(status) {
      BOOST_LOG(error) << "Failed to get NvFBC status: "sv << last_error();

      return std::nullopt;
    }

    return params;
  }

  int capture(NVFBC_CREATE_CAPTURE_SESSION_PARAMS &capture_params) {
    if(func.nvFBCCreateCaptureSession(handle, &capture_params)) {
      BOOST_LOG(error) << "Failed to start capture session: "sv << last_error();
      return -1;
    }

    handle_flags[SESSION_CAPTURE] = true;

    NVFBC_TOCUDA_SETUP_PARAMS setup_params {
      NVFBC_TOCUDA_SETUP_PARAMS_VER,
      NVFBC_BUFFER_FORMAT_BGRA,
    };

    if(func.nvFBCToCudaSetUp(handle, &setup_params)) {
      BOOST_LOG(error) << "Failed to setup cuda interop with nvFBC: "sv << last_error();
      return -1;
    }
    return 0;
  }

  int stop() {
    if(!handle_flags[SESSION_CAPTURE]) {
      return 0;
    }

    NVFBC_DESTROY_CAPTURE_SESSION_PARAMS params { NVFBC_DESTROY_CAPTURE_SESSION_PARAMS_VER };

    if(func.nvFBCDestroyCaptureSession(handle, &params)) {
      BOOST_LOG(error) << "Couldn't destroy capture session: "sv << last_error();

      return -1;
    }

    handle_flags[SESSION_CAPTURE] = false;

    return 0;
  }

  int reset() {
    if(!handle_flags[SESSION_HANDLE]) {
      return 0;
    }

    stop();

    NVFBC_DESTROY_HANDLE_PARAMS params { NVFBC_DESTROY_HANDLE_PARAMS_VER };

    if(func.nvFBCDestroyHandle(handle, &params)) {
      BOOST_LOG(error) << "Couldn't destroy session handle: "sv << func.nvFBCGetLastErrorStr(handle);
    }

    handle_flags[SESSION_HANDLE] = false;

    return 0;
  }

  ~handle_t() {
    reset();
  }

  std::bitset<MAX_FLAGS> handle_flags;

  NVFBC_SESSION_HANDLE handle;
};

class display_t : public platf::display_t {
public:
  int init(const std::string_view &display_name, const ::video::config_t &config) {
    auto handle = handle_t::make();
    if(!handle) {
      return -1;
    }

    ctx_t ctx { handle->handle };

    auto status_params = handle->status();
    if(!status_params) {
      return -1;
    }

    int streamedMonitor = -1;
    if(!display_name.empty()) {
      if(status_params->bXRandRAvailable) {
        auto monitor_nr = util::from_view(display_name);

        if(monitor_nr < 0 || monitor_nr >= status_params->dwOutputNum) {
          BOOST_LOG(warning) << "Can't stream monitor ["sv << monitor_nr << "], it needs to be between [0] and ["sv << status_params->dwOutputNum - 1 << "], defaulting to virtual desktop"sv;
        }
        else {
          streamedMonitor = monitor_nr;
        }
      }
      else {
        BOOST_LOG(warning) << "XrandR not available, streaming entire virtual desktop"sv;
      }
    }

    delay = std::chrono::nanoseconds { 1s } / config.framerate;

    capture_params = NVFBC_CREATE_CAPTURE_SESSION_PARAMS { NVFBC_CREATE_CAPTURE_SESSION_PARAMS_VER };

    capture_params.eCaptureType                = NVFBC_CAPTURE_SHARED_CUDA;
    capture_params.bDisableAutoModesetRecovery = nv_bool(true);

    capture_params.dwSamplingRateMs = 1000 /* ms */ / config.framerate;

    if(streamedMonitor != -1) {
      auto &output = status_params->outputs[streamedMonitor];

      width    = output.trackedBox.w;
      height   = output.trackedBox.h;
      offset_x = output.trackedBox.x;
      offset_y = output.trackedBox.y;

      capture_params.eTrackingType = NVFBC_TRACKING_OUTPUT;
      capture_params.dwOutputId    = output.dwId;
    }
    else {
      capture_params.eTrackingType = NVFBC_TRACKING_SCREEN;

      width  = status_params->screenSize.w;
      height = status_params->screenSize.h;
    }

    env_width  = status_params->screenSize.w;
    env_height = status_params->screenSize.h;

    this->handle = std::move(*handle);
    return 0;
  }

  platf::capture_e capture(snapshot_cb_t &&snapshot_cb, std::shared_ptr<platf::img_t> img, bool *cursor) override {
    auto next_frame = std::chrono::steady_clock::now();

    // Force display_t::capture to initialize handle_t::capture
    cursor_visible = !*cursor;

    ctx_t ctx { handle.handle };
    auto fg = util::fail_guard([&]() {
      handle.reset();
    });

    while(img) {
      auto now = std::chrono::steady_clock::now();
      if(next_frame > now) {
        std::this_thread::sleep_for((next_frame - now) / 3 * 2);
      }
      while(next_frame > now) {
        std::this_thread::sleep_for(1ns);
        now = std::chrono::steady_clock::now();
      }
      next_frame = now + delay;

      auto status = snapshot(img.get(), 150ms, *cursor);
      switch(status) {
      case platf::capture_e::reinit:
      case platf::capture_e::error:
        return status;
      case platf::capture_e::timeout:
        img = snapshot_cb(img, false);
        break;
      case platf::capture_e::ok:
        img = snapshot_cb(img, true);
        break;
      default:
        BOOST_LOG(error) << "Unrecognized capture status ["sv << (int)status << ']';
        return status;
      }
    }

    return platf::capture_e::ok;
  }

  // Reinitialize the capture session.
  platf::capture_e reinit(bool cursor) {
    if(handle.stop()) {
      return platf::capture_e::error;
    }

    cursor_visible = cursor;
    if(cursor) {
      capture_params.bPushModel          = nv_bool(false);
      capture_params.bWithCursor         = nv_bool(true);
      capture_params.bAllowDirectCapture = nv_bool(false);
    }
    else {
      capture_params.bPushModel          = nv_bool(true);
      capture_params.bWithCursor         = nv_bool(false);
      capture_params.bAllowDirectCapture = nv_bool(true);
    }

    if(handle.capture(capture_params)) {
      return platf::capture_e::error;
    }

    // If trying to capture directly, test if it actually does.
    if(capture_params.bAllowDirectCapture) {
      CUdeviceptr device_ptr;
      NVFBC_FRAME_GRAB_INFO info;

      NVFBC_TOCUDA_GRAB_FRAME_PARAMS grab {
        NVFBC_TOCUDA_GRAB_FRAME_PARAMS_VER,
        NVFBC_TOCUDA_GRAB_FLAGS_NOWAIT,
        &device_ptr,
        &info,
        0,
      };

      // Direct Capture may fail the first few times, even if it's possible
      for(int x = 0; x < 3; ++x) {
        if(auto status = func.nvFBCToCudaGrabFrame(handle.handle, &grab)) {
          if(status == NVFBC_ERR_MUST_RECREATE) {
            return platf::capture_e::reinit;
          }

          BOOST_LOG(error) << "Couldn't capture nvFramebuffer: "sv << handle.last_error();

          return platf::capture_e::error;
        }

        if(info.bDirectCapture) {
          break;
        }

        BOOST_LOG(debug) << "Direct capture failed attempt ["sv << x << ']';
      }

      if(!info.bDirectCapture) {
        BOOST_LOG(debug) << "Direct capture failed, trying the extra copy method"sv;
        // Direct capture failed
        capture_params.bPushModel          = nv_bool(false);
        capture_params.bWithCursor         = nv_bool(false);
        capture_params.bAllowDirectCapture = nv_bool(false);

        if(handle.stop() || handle.capture(capture_params)) {
          return platf::capture_e::error;
        }
      }
    }

    return platf::capture_e::ok;
  }

  platf::capture_e snapshot(platf::img_t *img, std::chrono::milliseconds timeout, bool cursor) {
    if(cursor != cursor_visible) {
      auto status = reinit(cursor);
      if(status != platf::capture_e::ok) {
        return status;
      }
    }

    CUdeviceptr device_ptr;
    NVFBC_FRAME_GRAB_INFO info;

    NVFBC_TOCUDA_GRAB_FRAME_PARAMS grab {
      NVFBC_TOCUDA_GRAB_FRAME_PARAMS_VER,
      NVFBC_TOCUDA_GRAB_FLAGS_NOWAIT,
      &device_ptr,
      &info,
      (std::uint32_t)timeout.count(),
    };

    if(auto status = func.nvFBCToCudaGrabFrame(handle.handle, &grab)) {
      if(status == NVFBC_ERR_MUST_RECREATE) {
        return platf::capture_e::reinit;
      }

      BOOST_LOG(error) << "Couldn't capture nvFramebuffer: "sv << handle.last_error();
      return platf::capture_e::error;
    }

    if(((img_t *)img)->tex.copy((std::uint8_t *)device_ptr, img->height, img->row_pitch)) {
      return platf::capture_e::error;
    }

    return platf::capture_e::ok;
  }

  std::shared_ptr<platf::hwdevice_t> make_hwdevice(platf::pix_fmt_e pix_fmt) override {
    return ::cuda::make_hwdevice(width, height, true);
  }

  std::shared_ptr<platf::img_t> alloc_img() override {
    auto img = std::make_shared<cuda::img_t>();

    img->data        = nullptr;
    img->width       = width;
    img->height      = height;
    img->pixel_pitch = 4;
    img->row_pitch   = img->width * img->pixel_pitch;

    auto tex_opt = tex_t::make(height, width * img->pixel_pitch);
    if(!tex_opt) {
      return nullptr;
    }

    img->tex = std::move(*tex_opt);

    return img;
  };

  int dummy_img(platf::img_t *) override {
    return 0;
  }

  std::chrono::nanoseconds delay;

  bool cursor_visible;
  handle_t handle;

  NVFBC_CREATE_CAPTURE_SESSION_PARAMS capture_params;
};
} // namespace nvfbc
} // namespace cuda

namespace platf {
std::shared_ptr<display_t> nvfbc_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
  if(hwdevice_type != mem_type_e::cuda) {
    BOOST_LOG(error) << "Could not initialize nvfbc display with the given hw device type"sv;
    return nullptr;
  }

  auto display = std::make_shared<cuda::nvfbc::display_t>();

  if(display->init(display_name, config)) {
    return nullptr;
  }

  return display;
}

std::vector<std::string> nvfbc_display_names() {
  if(cuda::init() || cuda::nvfbc::init()) {
    return {};
  }

  std::vector<std::string> display_names;

  auto handle = cuda::nvfbc::handle_t::make();
  if(!handle) {
    return {};
  }

  auto status_params = handle->status();
  if(!status_params) {
    return {};
  }

  if(!status_params->bIsCapturePossible) {
    BOOST_LOG(error) << "NVidia driver doesn't support NvFBC screencasting"sv;
  }

  BOOST_LOG(info) << "Found ["sv << status_params->dwOutputNum << "] outputs"sv;
  BOOST_LOG(info) << "Virtual Desktop: "sv << status_params->screenSize.w << 'x' << status_params->screenSize.h;
  BOOST_LOG(info) << "XrandR: "sv << (status_params->bXRandRAvailable ? "available"sv : "unavailable"sv);

  for(auto x = 0; x < status_params->dwOutputNum; ++x) {
    auto &output = status_params->outputs[x];
    BOOST_LOG(info) << "-- Output --"sv;
    BOOST_LOG(debug) << "  ID: "sv << output.dwId;
    BOOST_LOG(debug) << "  Name: "sv << output.name;
    BOOST_LOG(info) << "  Resolution: "sv << output.trackedBox.w << 'x' << output.trackedBox.h;
    BOOST_LOG(info) << "  Offset: "sv << output.trackedBox.x << 'x' << output.trackedBox.y;
    display_names.emplace_back(std::to_string(x));
  }

  return display_names;
}
} // namespace platf