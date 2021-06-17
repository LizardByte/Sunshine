#include <sstream>
#include <string>

#include <glad/egl.h>
#include <glad/gl.h>

#include <dlfcn.h>
#include <fcntl.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "sunshine/config.h"
#include "sunshine/main.h"
#include "sunshine/platform/common.h"
#include "sunshine/utility.h"
#include "sunshine/video.h"

// I want to have as little build dependencies as possible
// There aren't that many DRM_FORMAT I need to use, so define them here
//
// They aren't likely to change any time soon.
#define fourcc_code(a, b, c, d) ((std::uint32_t)(a) | ((std::uint32_t)(b) << 8) | \
                                 ((std::uint32_t)(c) << 16) | ((std::uint32_t)(d) << 24))
#define DRM_FORMAT_R8 fourcc_code('R', '8', ' ', ' ')   /* [7:0] R */
#define DRM_FORMAT_GR88 fourcc_code('G', 'R', '8', '8') /* [15:0] G:R 8:8 little endian */


#define SUNSHINE_SHADERS_DIR SUNSHINE_ASSETS_DIR "/shaders/opengl"

#define STRINGIFY(x) #x
#define gl_drain_errors_helper(x) gl::drain_errors("line " STRINGIFY(x))
#define gl_drain_errors gl_drain_errors_helper(__LINE__)

using namespace std::literals;

static void free_frame(AVFrame *frame) {
  av_frame_free(&frame);
}

using frame_t = util::safe_ptr<AVFrame, free_frame>;

namespace dyn {
void *handle(const std::vector<const char *> &libs) {
  void *handle;

  for(auto lib : libs) {
    handle = dlopen(lib, RTLD_LAZY | RTLD_LOCAL);
    if(handle) {
      return handle;
    }
  }

  std::stringstream ss;
  ss << "Couldn't find any of the following libraries: ["sv << libs.front();
  std::for_each(std::begin(libs) + 1, std::end(libs), [&](auto lib) {
    ss << ", "sv << lib;
  });

  ss << ']';

  BOOST_LOG(error) << ss.str();

  return nullptr;
}

int load(void *handle, std::vector<std::tuple<GLADapiproc *, const char *>> &funcs, bool strict = true) {
  for(auto &func : funcs) {
    TUPLE_2D_REF(fn, name, func);

    *fn = GLAD_GNUC_EXTENSION(GLADapiproc) dlsym(handle, name);

    if(!*fn && strict) {
      BOOST_LOG(error) << "Couldn't find function: "sv << name;

      return -1;
    }
  }

  return 0;
}
} // namespace dyn


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

int init() {
  static void *handle { nullptr };
  static bool funcs_loaded = false;

  if(funcs_loaded) return 0;

  if(!handle) {
    handle = dyn::handle({ "libva.so.2", "libva.so" });
    if(!handle) {
      return -1;
    }
  }

  std::vector<std::tuple<GLADapiproc *, const char *>> funcs {
    { (GLADapiproc *)&terminate, "vaTerminate" },
    { (GLADapiproc *)&initialize, "vaInitialize" },
    { (GLADapiproc *)&errorStr, "vaErrorStr" },
    { (GLADapiproc *)&setErrorCallback, "vaSetErrorCallback" },
    { (GLADapiproc *)&setInfoCallback, "vaSetInfoCallback" },
    { (GLADapiproc *)&queryVendorString, "vaQueryVendorString" },
    { (GLADapiproc *)&exportSurfaceHandle, "vaExportSurfaceHandle" },
  };

  if(dyn::load(handle, funcs)) {
    return -1;
  }

  funcs_loaded = true;
  return 0;
}

int init_drm() {
  if(init()) {
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

  std::vector<std::tuple<GLADapiproc *, const char *>> funcs {
    { (GLADapiproc *)&getDisplayDRM, "vaGetDisplayDRM" },
  };

  if(dyn::load(handle, funcs)) {
    return -1;
  }

  funcs_loaded = true;
  return 0;
}
} // namespace va

namespace gbm {
struct device;
typedef void (*device_destroy_fn)(device *gbm);
typedef device *(*create_device_fn)(int fd);

device_destroy_fn device_destroy;
create_device_fn create_device;

int init() {
  static void *handle { nullptr };
  static bool funcs_loaded = false;

  if(funcs_loaded) return 0;

  if(!handle) {
    handle = dyn::handle({ "libgbm.so.1", "libgbm.so" });
    if(!handle) {
      return -1;
    }
  }

  std::vector<std::tuple<GLADapiproc *, const char *>> funcs {
    { (GLADapiproc *)&device_destroy, "gbm_device_destroy" },
    { (GLADapiproc *)&create_device, "gbm_create_device" },
  };

  if(dyn::load(handle, funcs)) {
    return -1;
  }

  funcs_loaded = true;
  return 0;
}

} // namespace gbm

namespace gl {
static GladGLContext ctx;

void drain_errors(const std::string_view &prefix) {
  GLenum err;
  while((err = ctx.GetError()) != GL_NO_ERROR) {
    BOOST_LOG(error) << "GL: "sv << prefix << ": ["sv << util::hex(err).to_string_view() << ']';
  }
}

class tex_t : public util::buffer_t<GLuint> {
  using util::buffer_t<GLuint>::buffer_t;

public:
  tex_t(tex_t &&) = default;
  tex_t &operator=(tex_t &&) = default;

  ~tex_t() {
    if(!size() == 0) {
      ctx.DeleteTextures(size(), begin());
    }
  }

  static tex_t make(std::size_t count) {
    tex_t textures { count };

    ctx.GenTextures(textures.size(), textures.begin());

    float color[] = { 0.0f, 0.0f, 0.0f, 1.0f };

    for(auto tex : textures) {
      gl::ctx.BindTexture(GL_TEXTURE_2D, tex);
      gl::ctx.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // x
      gl::ctx.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // y
      gl::ctx.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      gl::ctx.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      gl::ctx.TexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, color);
    }

    return textures;
  }
};

class frame_buf_t : public util::buffer_t<GLuint> {
  using util::buffer_t<GLuint>::buffer_t;

public:
  frame_buf_t(frame_buf_t &&) = default;
  frame_buf_t &operator=(frame_buf_t &&) = default;

  ~frame_buf_t() {
    if(begin()) {
      ctx.DeleteFramebuffers(size(), begin());
    }
  }

  static frame_buf_t make(std::size_t count) {
    frame_buf_t frame_buf { count };

    ctx.GenFramebuffers(frame_buf.size(), frame_buf.begin());

    return frame_buf;
  }

  template<class It>
  void bind(It it_begin, It it_end) {
    if(std::distance(it_begin, it_end) > size()) {
      BOOST_LOG(warning) << "To many elements to bind"sv;
      return;
    }

    int x = 0;
    std::for_each(it_begin, it_end, [&](auto tex) {
      ctx.BindFramebuffer(GL_FRAMEBUFFER, (*this)[x]);
      ctx.BindTexture(GL_TEXTURE_2D, tex);

      ctx.FramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + x, tex, 0);

      ++x;
    });
  }
};

class shader_t {
  KITTY_USING_MOVE_T(shader_internal_t, GLuint, std::numeric_limits<GLuint>::max(), {
    if(el != std::numeric_limits<GLuint>::max()) {
      ctx.DeleteShader(el);
    }
  });

public:
  std::string err_str() {
    int length;
    ctx.GetShaderiv(handle(), GL_INFO_LOG_LENGTH, &length);

    std::string string;
    string.resize(length);

    ctx.GetShaderInfoLog(handle(), length, &length, string.data());

    string.resize(length - 1);

    return string;
  }

  static util::Either<shader_t, std::string> compile(const std::string_view &source, GLenum type) {
    shader_t shader;

    auto data    = source.data();
    GLint length = source.length();

    shader._shader.el = ctx.CreateShader(type);
    ctx.ShaderSource(shader.handle(), 1, &data, &length);
    ctx.CompileShader(shader.handle());

    int status = 0;
    ctx.GetShaderiv(shader.handle(), GL_COMPILE_STATUS, &status);

    if(!status) {
      return shader.err_str();
    }

    return shader;
  }

  GLuint handle() const {
    return _shader.el;
  }

private:
  shader_internal_t _shader;
};

class buffer_t {
  KITTY_USING_MOVE_T(buffer_internal_t, GLuint, std::numeric_limits<GLuint>::max(), {
    if(el != std::numeric_limits<GLuint>::max()) {
      ctx.DeleteBuffers(1, &el);
    }
  });

public:
  static buffer_t make(util::buffer_t<GLint> &&offsets, const char *block, const std::string_view &data) {
    buffer_t buffer;
    buffer._block   = block;
    buffer._size    = data.size();
    buffer._offsets = std::move(offsets);

    ctx.GenBuffers(1, &buffer._buffer.el);
    ctx.BindBuffer(GL_UNIFORM_BUFFER, buffer.handle());
    ctx.BufferData(GL_UNIFORM_BUFFER, data.size(), (const std::uint8_t *)data.data(), GL_DYNAMIC_DRAW);

    return buffer;
  }

  GLuint handle() const {
    return _buffer.el;
  }

  const char *block() const {
    return _block;
  }

  void update(const std::string_view &view, std::size_t offset = 0) {
    ctx.BindBuffer(GL_UNIFORM_BUFFER, handle());
    ctx.BufferSubData(GL_UNIFORM_BUFFER, offset, view.size(), (const void *)view.data());
  }

  void update(std::string_view *members, std::size_t count, std::size_t offset = 0) {
    util::buffer_t<std::uint8_t> buffer { _size };

    for(int x = 0; x < count; ++x) {
      auto val = members[x];

      std::copy_n((const std::uint8_t *)val.data(), val.size(), &buffer[_offsets[x]]);
    }

    update(util::view(buffer.begin(), buffer.end()), offset);
  }

private:
  const char *_block;

  std::size_t _size;

  util::buffer_t<GLint> _offsets;

  buffer_internal_t _buffer;
};

class program_t {
  KITTY_USING_MOVE_T(program_internal_t, GLuint, std::numeric_limits<GLuint>::max(), {
    if(el != std::numeric_limits<GLuint>::max()) {
      ctx.DeleteProgram(el);
    }
  });

public:
  std::string err_str() {
    int length;
    ctx.GetProgramiv(handle(), GL_INFO_LOG_LENGTH, &length);

    std::string string;
    string.resize(length);

    ctx.GetShaderInfoLog(handle(), length, &length, string.data());

    string.resize(length - 1);

    return string;
  }

  static util::Either<program_t, std::string> link(const shader_t &vert, const shader_t &frag) {
    program_t program;

    program._program.el = ctx.CreateProgram();

    ctx.AttachShader(program.handle(), vert.handle());
    ctx.AttachShader(program.handle(), frag.handle());

    // p_handle stores a copy of the program handle, since program will be moved before
    // the fail guard funcion is called.
    auto fg = util::fail_guard([p_handle = program.handle(), &vert, &frag]() {
      ctx.DetachShader(p_handle, vert.handle());
      ctx.DetachShader(p_handle, frag.handle());
    });

    ctx.LinkProgram(program.handle());

    int status = 0;
    ctx.GetProgramiv(program.handle(), GL_LINK_STATUS, &status);

    if(!status) {
      return program.err_str();
    }

    return program;
  }

  void bind(const buffer_t &buffer) {
    ctx.UseProgram(handle());
    auto i = ctx.GetUniformBlockIndex(handle(), buffer.block());

    ctx.BindBufferBase(GL_UNIFORM_BUFFER, i, buffer.handle());
  }

  std::optional<buffer_t> uniform(const char *block, std::pair<const char *, std::string_view> *members, std::size_t count) {
    auto i = ctx.GetUniformBlockIndex(handle(), block);
    if(i == GL_INVALID_INDEX) {
      BOOST_LOG(error) << "Couldn't find index of ["sv << block << ']';
      return std::nullopt;
    }

    int size;
    ctx.GetActiveUniformBlockiv(handle(), i, GL_UNIFORM_BLOCK_DATA_SIZE, &size);

    bool error_flag = false;

    util::buffer_t<GLint> offsets { count };
    auto indices = (std::uint32_t *)alloca(count * sizeof(std::uint32_t));
    auto names   = (const char **)alloca(count * sizeof(const char *));
    auto names_p = names;

    std::for_each_n(members, count, [names_p](auto &member) mutable {
      *names_p++ = std::get<0>(member);
    });

    std::fill_n(indices, count, GL_INVALID_INDEX);
    ctx.GetUniformIndices(handle(), count, names, indices);

    for(int x = 0; x < count; ++x) {
      if(indices[x] == GL_INVALID_INDEX) {
        error_flag = true;

        BOOST_LOG(error) << "Couldn't find ["sv << block << '.' << members[x].first << ']';
      }
    }

    if(error_flag) {
      return std::nullopt;
    }

    ctx.GetActiveUniformsiv(handle(), count, indices, GL_UNIFORM_OFFSET, offsets.begin());
    util::buffer_t<std::uint8_t> buffer { (std::size_t)size };

    for(int x = 0; x < count; ++x) {
      auto val = std::get<1>(members[x]);

      std::copy_n((const std::uint8_t *)val.data(), val.size(), &buffer[offsets[x]]);
    }

    return buffer_t::make(std::move(offsets), block, std::string_view { (char *)buffer.begin(), buffer.size() });
  }

  GLuint handle() const {
    return _program.el;
  }

private:
  program_internal_t _program;
};
} // namespace gl

namespace platf {
namespace egl {

constexpr auto EGL_LINUX_DMA_BUF_EXT         = 0x3270;
constexpr auto EGL_LINUX_DRM_FOURCC_EXT      = 0x3271;
constexpr auto EGL_DMA_BUF_PLANE0_FD_EXT     = 0x3272;
constexpr auto EGL_DMA_BUF_PLANE0_OFFSET_EXT = 0x3273;
constexpr auto EGL_DMA_BUF_PLANE0_PITCH_EXT  = 0x3274;

using display_t = util::dyn_safe_ptr_v2<void, EGLBoolean, &eglTerminate>;
using gbm_t     = util::dyn_safe_ptr<gbm::device, &gbm::device_destroy>;

int vaapi_make_hwdevice_ctx(platf::hwdevice_t *base, AVBufferRef **hw_device_buf);

KITTY_USING_MOVE_T(file_t, int, -1, {
  if(el >= 0) {
    close(el);
  }
});

struct nv12_img_t {
  display_t::pointer display;
  EGLImage r8;
  EGLImage bg88;

  gl::tex_t tex;
  gl::frame_buf_t buf;

  static constexpr std::size_t num_fds =
    sizeof(va::DRMPRIMESurfaceDescriptor::objects) / sizeof(va::DRMPRIMESurfaceDescriptor::objects[0]);

  std::array<file_t, num_fds> fds;
};

KITTY_USING_MOVE_T(nv12_t, nv12_img_t, , {
  if(el.r8) {
    eglDestroyImageKHR(el.display, el.r8);
  }

  if(el.bg88) {
    eglDestroyImageKHR(el.display, el.bg88);
  }
});

KITTY_USING_MOVE_T(ctx_t, (std::tuple<display_t::pointer, EGLContext>), , {
  TUPLE_2D_REF(disp, ctx, el);
  if(ctx) {
    if(ctx == eglGetCurrentContext()) {
      eglMakeCurrent(disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    eglDestroyContext(disp, ctx);
  }
});

bool fail() {
  return eglGetError() != EGL_SUCCESS;
}

class egl_t : public platf::hwdevice_t {
public:
  std::optional<nv12_t> import(va::VASurfaceID surface) {
    // No deallocation necessary
    va::DRMPRIMESurfaceDescriptor prime;

    auto status = va::exportSurfaceHandle(
      va_display,
      surface,
      va::SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
      va::EXPORT_SURFACE_WRITE_ONLY | va::EXPORT_SURFACE_COMPOSED_LAYERS,
      &prime);
    if(status) {

      BOOST_LOG(error) << "Couldn't export va surface handle: ["sv << (int)surface << "]: "sv << va::errorStr(status);

      return std::nullopt;
    }

    // Keep track of file descriptors
    std::array<file_t, nv12_img_t::num_fds> fds;
    for(int x = 0; x < prime.num_objects; ++x) {
      fds[x] = prime.objects[x].fd;
    }

    int img_attr_planes[2][13] {
      { EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_R8,
        EGL_WIDTH, (int)prime.width,
        EGL_HEIGHT, (int)prime.height,
        EGL_DMA_BUF_PLANE0_FD_EXT, prime.objects[prime.layers[0].object_index[0]].fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, (int)prime.layers[0].offset[0],
        EGL_DMA_BUF_PLANE0_PITCH_EXT, (int)prime.layers[0].pitch[0],
        EGL_NONE },

      { EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_GR88,
        EGL_WIDTH, (int)prime.width / 2,
        EGL_HEIGHT, (int)prime.height / 2,
        EGL_DMA_BUF_PLANE0_FD_EXT, prime.objects[prime.layers[0].object_index[1]].fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, (int)prime.layers[0].offset[1],
        EGL_DMA_BUF_PLANE0_PITCH_EXT, (int)prime.layers[0].pitch[1],
        EGL_NONE },
    };

    nv12_t nv12 {
      display.get(),
      eglCreateImageKHR(display.get(), EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, img_attr_planes[0]),
      eglCreateImageKHR(display.get(), EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, img_attr_planes[1]),
      gl::tex_t::make(2),
      gl::frame_buf_t::make(2),
      std::move(fds)
    };

    if(!nv12->r8 || !nv12->bg88) {
      BOOST_LOG(error) << "Couldn't create KHR Image"sv;

      return std::nullopt;
    }

    gl::ctx.BindTexture(GL_TEXTURE_2D, nv12->tex[0]);
    gl::ctx.EGLImageTargetTexture2DOES(GL_TEXTURE_2D, nv12->r8);

    gl::ctx.BindTexture(GL_TEXTURE_2D, nv12->tex[1]);
    gl::ctx.EGLImageTargetTexture2DOES(GL_TEXTURE_2D, nv12->bg88);

    nv12->buf.bind(std::begin(nv12->tex), std::end(nv12->tex));

    gl_drain_errors;

    return nv12;
  }

  void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) override {
    video::color_t *color_p;
    switch(colorspace) {
    case 5: // SWS_CS_SMPTE170M
      color_p = &video::colors[0];
      break;
    case 1: // SWS_CS_ITU709
      color_p = &video::colors[2];
      break;
    case 9: // SWS_CS_BT2020
    default:
      BOOST_LOG(warning) << "Colorspace: ["sv << colorspace << "] not yet supported: switching to default"sv;
      color_p = &video::colors[0];
    };

    if(color_range > 1) {
      // Full range
      ++color_p;
    }

    std::string_view members[] {
      util::view(color_p->color_vec_y),
      util::view(color_p->color_vec_u),
      util::view(color_p->color_vec_v),
      util::view(color_p->range_y),
      util::view(color_p->range_uv),
    };

    color_matrix.update(members, sizeof(members) / sizeof(decltype(members[0])));
  }

  int init(int in_width, int in_height, const char *render_device) {
    if(!va::initialize || !gbm::create_device) {
      if(!va::initialize) BOOST_LOG(warning) << "libva not initialized"sv;
      if(!gbm::create_device) BOOST_LOG(warning) << "libgbm not initialized"sv;
      return -1;
    }

    file.el = open(render_device, O_RDWR);

    if(file.el < 0) {
      char error_buf[1024];
      BOOST_LOG(error) << "Couldn't open ["sv << render_device << "]: "sv << strerror_r(errno, error_buf, sizeof(error_buf));
      return -1;
    }

    gbm.reset(gbm::create_device(file.el));
    if(!gbm) {
      BOOST_LOG(error) << "Couldn't create GBM device: ["sv << util::hex(eglGetError()).to_string_view() << ']';
      return -1;
    }

    constexpr auto EGL_PLATFORM_GBM_MESA = 0x31D7;

    display.reset(eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA, gbm.get(), nullptr));
    if(fail()) {
      BOOST_LOG(error) << "Couldn't open EGL display: ["sv << util::hex(eglGetError()).to_string_view() << ']';
      return -1;
    }

    int major, minor;
    if(!eglInitialize(display.get(), &major, &minor)) {
      BOOST_LOG(error) << "Couldn't initialize EGL display: ["sv << util::hex(eglGetError()).to_string_view() << ']';
      return -1;
    }

    const char *extension_st = eglQueryString(display.get(), EGL_EXTENSIONS);
    const char *version      = eglQueryString(display.get(), EGL_VERSION);
    const char *vendor       = eglQueryString(display.get(), EGL_VENDOR);
    const char *apis         = eglQueryString(display.get(), EGL_CLIENT_APIS);

    BOOST_LOG(debug) << "EGL: ["sv << vendor << "]: version ["sv << version << ']';
    BOOST_LOG(debug) << "API's supported: ["sv << apis << ']';

    const char *extensions[] {
      "EGL_KHR_create_context",
      "EGL_KHR_surfaceless_context",
      "EGL_EXT_image_dma_buf_import",
      "EGL_KHR_image_pixmap"
    };

    for(auto ext : extensions) {
      if(!std::strstr(extension_st, ext)) {
        BOOST_LOG(error) << "Missing extension: ["sv << ext << ']';
        return -1;
      }
    }

    constexpr int conf_attr[] {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE
    };

    int count;
    EGLConfig conf;
    if(!eglChooseConfig(display.get(), conf_attr, &conf, 1, &count)) {
      BOOST_LOG(error) << "Couldn't set config attributes: ["sv << util::hex(eglGetError()).to_string_view() << ']';
      return -1;
    }

    if(!eglBindAPI(EGL_OPENGL_API)) {
      BOOST_LOG(error) << "Couldn't bind API: ["sv << util::hex(eglGetError()).to_string_view() << ']';
      return -1;
    }

    constexpr int attr[] {
      EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE
    };

    ctx.el = { display.get(), eglCreateContext(display.get(), conf, EGL_NO_CONTEXT, attr) };
    if(fail()) {
      BOOST_LOG(error) << "Couldn't create EGL context: ["sv << util::hex(eglGetError()).to_string_view() << ']';
      return -1;
    }

    TUPLE_EL_REF(ctx_p, 1, ctx.el);
    if(!eglMakeCurrent(display.get(), EGL_NO_SURFACE, EGL_NO_SURFACE, ctx_p)) {
      BOOST_LOG(error) << "Couldn't make current display"sv;
      return -1;
    }

    if(!gladLoadGLContext(&gl::ctx, eglGetProcAddress)) {
      BOOST_LOG(error) << "Couldn't load OpenGL library"sv;
      return -1;
    }

    BOOST_LOG(debug) << "GL: vendor: "sv << gl::ctx.GetString(GL_VENDOR);
    BOOST_LOG(debug) << "GL: renderer: "sv << gl::ctx.GetString(GL_RENDERER);
    BOOST_LOG(debug) << "GL: version: "sv << gl::ctx.GetString(GL_VERSION);
    BOOST_LOG(debug) << "GL: shader: "sv << gl::ctx.GetString(GL_SHADING_LANGUAGE_VERSION);

    gl::ctx.PixelStorei(GL_UNPACK_ALIGNMENT, 1);

    {
      const char *sources[] {
        SUNSHINE_SHADERS_DIR "/ConvertUV.frag",
        SUNSHINE_SHADERS_DIR "/ConvertUV.vert",
        SUNSHINE_SHADERS_DIR "/ConvertY.frag",
        SUNSHINE_SHADERS_DIR "/Scene.vert",
        SUNSHINE_SHADERS_DIR "/Scene.frag",
      };

      GLenum shader_type[2] {
        GL_FRAGMENT_SHADER,
        GL_VERTEX_SHADER,
      };

      constexpr auto count = sizeof(sources) / sizeof(const char *);

      util::Either<gl::shader_t, std::string> compiled_sources[count];

      bool error_flag = false;
      for(int x = 0; x < count; ++x) {
        auto &compiled_source = compiled_sources[x];

        compiled_source = gl::shader_t::compile(read_file(sources[x]), shader_type[x % 2]);
        gl_drain_errors;

        if(compiled_source.has_right()) {
          BOOST_LOG(error) << sources[x] << ": "sv << compiled_source.right();
          error_flag = true;
        }
      }

      if(error_flag) {
        return -1;
      }

      auto program = gl::program_t::link(compiled_sources[1].left(), compiled_sources[0].left());
      if(program.has_right()) {
        BOOST_LOG(error) << "GL linker: "sv << program.right();
        return -1;
      }

      // UV - shader
      this->program[1] = std::move(program.left());

      program = gl::program_t::link(compiled_sources[3].left(), compiled_sources[2].left());
      if(program.has_right()) {
        BOOST_LOG(error) << "GL linker: "sv << program.right();
        return -1;
      }

      // Y - shader
      this->program[0] = std::move(program.left());
    }

    auto color_p = &video::colors[0];
    std::pair<const char *, std::string_view> members[] {
      std::make_pair("color_vec_y", util::view(color_p->color_vec_y)),
      std::make_pair("color_vec_u", util::view(color_p->color_vec_u)),
      std::make_pair("color_vec_v", util::view(color_p->color_vec_v)),
      std::make_pair("range_y", util::view(color_p->range_y)),
      std::make_pair("range_uv", util::view(color_p->range_uv)),
    };

    auto color_matrix = program[0].uniform("ColorMatrix", members, sizeof(members) / sizeof(decltype(members[0])));
    if(!color_matrix) {
      return -1;
    }

    this->color_matrix = std::move(*color_matrix);

    tex_in = gl::tex_t::make(1);

    this->in_width  = in_width;
    this->in_height = in_height;

    data = (void *)vaapi_make_hwdevice_ctx;
    gl_drain_errors;
    return 0;
  }

  int convert(platf::img_t &img) override {
    auto tex = tex_in[0];

    gl::ctx.BindTexture(GL_TEXTURE_2D, tex);
    gl::ctx.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, in_width, in_height, GL_BGRA, GL_UNSIGNED_BYTE, img.data);

    GLenum attachments[] {
      GL_COLOR_ATTACHMENT0,
      GL_COLOR_ATTACHMENT1
    };

    for(int x = 0; x < sizeof(attachments) / sizeof(decltype(attachments[0])); ++x) {
      gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, nv12->buf[x]);
      gl::ctx.DrawBuffers(1, &attachments[x]);

      auto status = gl::ctx.CheckFramebufferStatus(GL_FRAMEBUFFER);
      if(status != GL_FRAMEBUFFER_COMPLETE) {
        BOOST_LOG(error) << "Pass "sv << x << ": CheckFramebufferStatus() --> [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }

      gl::ctx.BindTexture(GL_TEXTURE_2D, tex);

      gl::ctx.UseProgram(program[x].handle());
      program[x].bind(color_matrix);

      gl::ctx.Viewport(offsetX / (x + 1), offsetY / (x + 1), out_width / (x + 1), out_height / (x + 1));
      gl::ctx.DrawArrays(GL_TRIANGLES, 0, 3);
    }

    return 0;
  }

  int set_frame(AVFrame *frame) override {
    this->hwframe.reset(frame);
    this->frame = frame;

    if(av_hwframe_get_buffer(frame->hw_frames_ctx, frame, 0)) {
      BOOST_LOG(error) << "Couldn't get hwframe for VAAPI"sv;

      return -1;
    }

    va::VASurfaceID surface = (std::uintptr_t)frame->data[3];

    auto nv12_opt = import(surface);
    if(!nv12_opt) {
      return -1;
    }

    nv12 = std::move(*nv12_opt);

    // // Ensure aspect ratio is maintained
    auto scalar       = std::fminf(frame->width / (float)in_width, frame->height / (float)in_height);
    auto out_width_f  = in_width * scalar;
    auto out_height_f = in_height * scalar;

    // result is always positive
    auto offsetX_f = (frame->width - out_width_f) / 2;
    auto offsetY_f = (frame->height - out_height_f) / 2;

    out_width  = out_width_f;
    out_height = out_height_f;

    offsetX = offsetX_f;
    offsetY = offsetY_f;

    auto tex = tex_in[0];

    gl::ctx.BindTexture(GL_TEXTURE_2D, tex);
    gl::ctx.TexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, in_width, in_height);

    auto loc_width_i = gl::ctx.GetUniformLocation(program[1].handle(), "width_i");
    if(loc_width_i < 0) {
      BOOST_LOG(error) << "Couldn't find uniform [width_i]"sv;
      return -1;
    }

    auto width_i = 1.0f / out_width;
    gl::ctx.UseProgram(program[1].handle());
    gl::ctx.Uniform1fv(loc_width_i, 1, &width_i);

    gl_drain_errors;
    return 0;
  }

  ~egl_t() override {
    if(gl::ctx.GetError) {
      gl_drain_errors;
    }
  }

  int in_width, in_height;
  int out_width, out_height;
  int offsetX, offsetY;

  frame_t hwframe;

  va::display_t::pointer va_display;

  file_t file;
  gbm_t gbm;
  display_t display;
  ctx_t ctx;

  gl::tex_t tex_in;
  nv12_t nv12;
  gl::program_t program[2];
  gl::buffer_t color_matrix;
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

  auto egl = (platf::egl::egl_t *)base;
  auto fd  = dup(egl->file.el);

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

  egl->va_display = display.get();

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
  auto egl = std::make_shared<egl_t>();

  auto render_device = config::video.adapter_name.empty() ? "/dev/dri/renderD128" : config::video.adapter_name.c_str();
  if(egl->init(width, height, render_device)) {
    return nullptr;
  }

  return egl;
}
} // namespace egl

std::unique_ptr<deinit_t> init() {
  gbm::init();
  va::init_drm();

  if(!gladLoaderLoadEGL(EGL_NO_DISPLAY) || !eglGetPlatformDisplay) {
    BOOST_LOG(warning) << "Couldn't load EGL library"sv;
  }

  return std::make_unique<deinit_t>();
}
} // namespace platf