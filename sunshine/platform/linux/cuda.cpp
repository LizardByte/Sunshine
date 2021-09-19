#include <NvFBC.h>
#include <ffnvcodec/dynlink_loader.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/imgutils.h>
}

#include "cuda.h"
#include "graphics.h"
#include "sunshine/main.h"
#include "sunshine/utility.h"
#include "wayland.h"
#include "x11grab.h"

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

class ctx_t {
public:
  ctx_t(CUcontext ctx) {
    CU_CHECK_IGNORE(cdf->cuCtxPushCurrent(ctx), "Couldn't push cuda context");
  }

  ~ctx_t() {
    CUcontext dummy;

    CU_CHECK_IGNORE(cdf->cuCtxPopCurrent(&dummy), "Couldn't pop cuda context");
  }
};

void free_res(CUgraphicsResource res) {
  cdf->cuGraphicsUnregisterResource(res);
}

using res_internal_t = util::safe_ptr<CUgraphicsResource_st, free_res>;

template<std::size_t N>
class res_t {
public:
  res_t() : resources {}, mapped { false } {}
  res_t(res_t &&other) noexcept : resources { other.resources }, array_p { other.array_p }, ctx { other.ctx }, stream { other.stream } {
    other.resources = std::array<res_internal_t::pointer, N> {};
  }

  res_t &operator=(res_t &&other) {
    for(auto x = 0; x < N; ++x) {
      std::swap(resources[x], other.resources[x]);
      std::swap(array_p[x], other.array_p[x]);
    }

    std::swap(ctx, other.ctx);
    std::swap(stream, other.stream);
    std::swap(mapped, other.mapped);

    return *this;
  }

  res_t(CUcontext ctx, CUstream stream) : resources {}, ctx { ctx }, stream { stream }, mapped { false } {}

  int bind(gl::tex_t &tex) {
    ctx_t ctx { this->ctx };

    CU_CHECK(cdf->cuGraphicsGLRegisterImage(&resources[0], tex[0], GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY), "Couldn't register Y image");
    CU_CHECK(cdf->cuGraphicsGLRegisterImage(&resources[1], tex[1], GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY), "Couldn't register uv image");

    return 0;
  }

  int map() {
    ctx_t ctx { this->ctx };

    CU_CHECK(cdf->cuGraphicsMapResources(resources.size(), resources.data(), stream), "Coudn't map cuda resources");

    mapped = true;

    CU_CHECK(cdf->cuGraphicsSubResourceGetMappedArray(&array_p[0], resources[0], 0, 0), "Couldn't get mapped subresource [0]");
    CU_CHECK(cdf->cuGraphicsSubResourceGetMappedArray(&array_p[1], resources[1], 0, 0), "Couldn't get mapped subresource [1]");

    return 0;
  }

  void unmap() {
    // Either all or none are mapped
    if(mapped) {
      ctx_t ctx { this->ctx };

      CU_CHECK_IGNORE(cdf->cuGraphicsUnmapResources(resources.size(), resources.data(), stream), "Couldn't unmap cuda resources");

      mapped = false;
    }
  }

  inline CUarray &operator[](std::size_t index) {
    return array_p[index];
  }

  ~res_t() {
    unmap();
  }

  std::array<res_internal_t::pointer, N> resources;
  std::array<CUarray, N> array_p;

  CUcontext ctx;
  CUstream stream;

  bool mapped;
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

class opengl_t : public platf::hwdevice_t {
public:
  int init(int in_width, int in_height, platf::x11::xdisplay_t::pointer xdisplay) {
    if(!cdf) {
      BOOST_LOG(warning) << "cuda not initialized"sv;
      return -1;
    }

    this->data = (void *)0x1;

    display = egl::make_display(xdisplay);
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

  int set_frame(AVFrame *frame) override {
    auto cuda_ctx = (AVCUDADeviceContext *)((AVHWFramesContext *)frame->hw_frames_ctx->data)->device_ctx->hwctx;

    tex = gl::tex_t::make(2);
    fb  = gl::frame_buf_t::make(2);

    gl::ctx.BindTexture(GL_TEXTURE_2D, tex[0]);
    gl::ctx.TexImage2D(GL_TEXTURE_2D, 0, GL_RED, frame->width, frame->height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    gl::ctx.BindTexture(GL_TEXTURE_2D, tex[1]);
    gl::ctx.TexImage2D(GL_TEXTURE_2D, 0, GL_RG, frame->width / 2, frame->height / 2, 0, GL_RG, GL_UNSIGNED_BYTE, nullptr);
    gl::ctx.BindTexture(GL_TEXTURE_2D, 0);

    fb.bind(std::begin(tex), std::end(tex));

    res = res_t<2> { cuda_ctx->cuda_ctx, cuda_ctx->stream };

    if(res.bind(tex)) {
      return -1;
    }

    this->hwframe.reset(frame);
    this->frame = frame;

    if(av_hwframe_get_buffer(frame->hw_frames_ctx, frame, 0)) {
      BOOST_LOG(error) << "Couldn't get hwframe for NVENC"sv;

      return -1;
    }

    auto sws_opt = egl::sws_t::make(width, height, frame->width, frame->height);
    if(!sws_opt) {
      return -1;
    }

    sws = std::move(*sws_opt);
    return sws.blank(fb, 0, 0, frame->width, frame->height);
  }

  int convert(platf::img_t &img) override {
    sws.load_ram(img);

    if(sws.convert(fb)) {
      return -1;
    }

    if(res.map()) {
      return -1;
    }

    // Push and pop cuda context
    ctx_t ctx { res.ctx };
    for(auto x = 0; x < 2; ++x) {
      CUDA_MEMCPY2D desc {};

      auto shift = x;

      desc.srcPitch     = frame->width;
      desc.dstPitch     = frame->linesize[x];
      desc.Height       = frame->height >> shift;
      desc.WidthInBytes = std::min(desc.srcPitch, desc.dstPitch);

      desc.srcMemoryType = CU_MEMORYTYPE_ARRAY;
      desc.dstMemoryType = CU_MEMORYTYPE_DEVICE;

      desc.srcArray  = res[x];
      desc.dstDevice = (CUdeviceptr)frame->data[x];

      CU_CHECK(cdf->cuMemcpy2DAsync(&desc, res.stream), "Couldn't copy from OpenGL to cuda");
    }

    res.unmap();

    return 0;
  }

  void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) override {
    sws.set_colorspace(colorspace, color_range);
  }

  frame_t hwframe;

  egl::display_t display;
  egl::ctx_t ctx;

  egl::sws_t sws;

  gl::tex_t tex;
  gl::frame_buf_t fb;

  res_t<2> res;

  int width, height;
};

class cuda_t : public platf::hwdevice_t {
public:
  ~cuda_t() override {
    // sws_t needs to be destroyed while the context is active
    if(sws) {
      ctx_t ctx { cuda_ctx };

      sws.reset();
    }
  }

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

  int set_frame(AVFrame *frame) override {
    this->hwframe.reset(frame);
    this->frame = frame;

    if(((AVHWFramesContext *)frame->hw_frames_ctx->data)->sw_format != AV_PIX_FMT_NV12) {
      BOOST_LOG(error) << "cuda::cuda_t doesn't support any format other than AV_PIX_FMT_NV12"sv;
      return -1;
    }

    if(av_hwframe_get_buffer(frame->hw_frames_ctx, frame, 0)) {
      BOOST_LOG(error) << "Couldn't get hwframe for NVENC"sv;

      return -1;
    }

    cuda_ctx = ((AVCUDADeviceContext *)((AVHWFramesContext *)frame->hw_frames_ctx->data)->device_ctx->hwctx)->cuda_ctx;

    ctx_t ctx { cuda_ctx };
    sws = sws_t::make(width, height, frame->width, frame->height, width * 4);

    if(!sws) {
      return -1;
    }

    return 0;
  }

  int convert(platf::img_t &img) override {
    ctx_t ctx { cuda_ctx };

    return sws->load_ram(img) || sws->convert(frame->data[0], frame->data[1], frame->linesize[0], frame->linesize[1]);
  }

  void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) override {
    ctx_t ctx { cuda_ctx };
    sws->set_colorspace(colorspace, color_range);


    // The default green color is ugly.
    // Update the background color
    platf::img_t img;
    img.width = frame->width;
    img.height = frame->height;
    img.pixel_pitch = 4;
    img.row_pitch = img.width * img.pixel_pitch;

    std::vector<std::uint8_t> image_data;
    image_data.resize(img.row_pitch * img.height);

    img.data = image_data.data();

    if(sws->load_ram(img)) {
      return;
    }

    sws->convert(frame->data[0], frame->data[1], frame->linesize[0], frame->linesize[1], {
      frame->width, frame->height, 0, 0
    });
  }

  frame_t hwframe;

  std::unique_ptr<sws_t> sws;

  int width, height;

  CUcontext cuda_ctx;
};

std::shared_ptr<platf::hwdevice_t> make_hwdevice(int width, int height, platf::x11::xdisplay_t::pointer xdisplay) {
  if(init()) {
    return nullptr;
  }

  auto cuda = std::make_shared<cuda_t>();
  if(cuda->init(width, height)) {
    return nullptr;
  }

  return cuda;
}
} // namespace cuda

namespace platf::nvfbc {
static PNVFBCCREATEINSTANCE createInstance {};
static NVFBC_API_FUNCTION_LIST func { NVFBC_VERSION };

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

  funcs_loaded = true;
  return 0;
}

class handle_t {
  KITTY_USING_MOVE_T(session_t, NVFBC_SESSION_HANDLE, std::numeric_limits<std::uint64_t>::max(), {
    if(el == std::numeric_limits<std::uint64_t>::max()) {
      return;
    }
    NVFBC_DESTROY_HANDLE_PARAMS params { NVFBC_DESTROY_HANDLE_PARAMS_VER };

    auto status = func.nvFBCDestroyHandle(el, &params);
    if(status) {
      BOOST_LOG(error) << "Failed to destroy nvfbc handle: "sv << func.nvFBCGetLastErrorStr(el);
    }
  });

public:
  static std::optional<handle_t> make() {
    NVFBC_CREATE_HANDLE_PARAMS params { NVFBC_CREATE_HANDLE_PARAMS_VER };
    session_t session;

    auto status = func.nvFBCCreateHandle(&session.el, &params);
    if(status) {
      BOOST_LOG(error) << "Failed to create session: "sv << func.nvFBCGetLastErrorStr(session.el);
      session.release();

      return std::nullopt;
    }

    return handle_t { std::move(session) };
  }

  const char *last_error() {
    return func.nvFBCGetLastErrorStr(session.el);
  }

  std::optional<NVFBC_GET_STATUS_PARAMS> status() {
    NVFBC_GET_STATUS_PARAMS params { NVFBC_GET_STATUS_PARAMS_VER };

    auto status = func.nvFBCGetStatus(session.el, &params);
    if(status) {
      BOOST_LOG(error) << "Failed to create session: "sv << last_error();

      return std::nullopt;
    }

    return params;
  }

  session_t session;
};

std::vector<std::string> nvfbc_display_names() {
  if(init()) {
    return {};
  }

  std::vector<std::string> display_names;

  auto status = createInstance(&func);
  if(status) {
    BOOST_LOG(error) << "Unable to create NvFBC instance"sv;
    return {};
  }

  auto handle = handle_t::make();
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

  return display_names;
}
} // namespace platf::nvfbc