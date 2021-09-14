#include "cuda.h"
#include "graphics.h"
#include "sunshine/main.h"
#include "sunshine/utility.h"
#include "wayland.h"
#include "x11grab.h"
#include <ffnvcodec/dynlink_loader.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/imgutils.h>
}

#define SUNSHINE_STRINGVIEW_HELPER(x) x##sv
#define SUNSHINE_STRINGVIEW(x) SUNSHINE_STRINGVIEW_HELPER(x)

#define CU_CHECK(x, y) \
  if(check((x), SUNSHINE_STRINGVIEW(y ": "))) return -1

#define CU_CHECK_IGNORE(x, y) \
  check((x), SUNSHINE_STRINGVIEW(y ": "))

using namespace std::literals;
namespace cuda {
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

class cuda_t : public platf::hwdevice_t {
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

std::shared_ptr<platf::hwdevice_t> make_hwdevice(int width, int height, platf::x11::xdisplay_t::pointer xdisplay) {
  if(init()) {
    return nullptr;
  }

  auto cuda = std::make_shared<cuda_t>();
  if(cuda->init(width, height, xdisplay)) {
    return nullptr;
  }

  return cuda;
}
} // namespace cuda
