#include <string>

#include <glad/egl.h>
#include <glad/gl.h>

#include <fcntl.h>
#include <gbm.h>

#include <va/va.h>
#include <va/va_drm.h>
#include <va/va_drmcommon.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_vaapi.h>
}

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

namespace va {
using display_t = util::safe_ptr_v2<void, VAStatus, vaTerminate>;
}

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
      gl::ctx.TexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
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
auto constexpr render_device = "/dev/dri/renderD129";

constexpr auto EGL_LINUX_DMA_BUF_EXT         = 0x3270;
constexpr auto EGL_LINUX_DRM_FOURCC_EXT      = 0x3271;
constexpr auto EGL_DMA_BUF_PLANE0_FD_EXT     = 0x3272;
constexpr auto EGL_DMA_BUF_PLANE0_OFFSET_EXT = 0x3273;
constexpr auto EGL_DMA_BUF_PLANE0_PITCH_EXT  = 0x3274;

using display_t = util::dyn_safe_ptr_v2<void, EGLBoolean, &eglTerminate>;
using gbm_t     = util::safe_ptr<gbm_device, gbm_device_destroy>;

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
  std::optional<nv12_t> import(VASurfaceID surface) {
    // No deallocation necessary
    VADRMPRIMESurfaceDescriptor prime;

    auto status = vaExportSurfaceHandle(
      va_display,
      surface,
      VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
      VA_EXPORT_SURFACE_WRITE_ONLY | VA_EXPORT_SURFACE_COMPOSED_LAYERS,
      &prime);
    if(status) {

      BOOST_LOG(error) << "Couldn't export va surface handle: "sv << vaErrorStr(status);

      return std::nullopt;
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
      gl::frame_buf_t::make(2)
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

  int init(const char *render_device) {
    file.el = open(render_device, O_RDWR);

    if(file.el < 0) {
      char error_buf[1024];
      BOOST_LOG(error) << "Couldn't open ["sv << render_device << "]: "sv << strerror_r(errno, error_buf, sizeof(error_buf));
      return -1;
    }

    gbm.reset(gbm_create_device(file.el));
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

    data = (void *)vaapi_make_hwdevice_ctx;
    gl_drain_errors;
    return 0;
  }

  int convert(platf::img_t &img) override {
    auto tex = tex_in[0];

    gl::ctx.ActiveTexture(GL_TEXTURE0);
    gl::ctx.BindTexture(GL_TEXTURE_2D, tex);
    gl::ctx.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, out_width, out_height, GL_BGRA, GL_UNSIGNED_BYTE, img.data);

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

      gl::ctx.Viewport(0, 0, out_width / (x + 1), out_height / (x + 1));
      gl::ctx.DrawArrays(GL_TRIANGLES, 0, 3);
    }

    return 0;
  }

  int set_frame(AVFrame *frame) {
    this->frame = frame;

    if(av_hwframe_get_buffer(frame->hw_frames_ctx, frame, 0)) {
      BOOST_LOG(error) << "Couldn't get hwframe for VAAPI"sv;

      return -1;
    }

    VASurfaceID surface = (std::uintptr_t)frame->data[3];

    auto nv12_opt = import(surface);
    if(!nv12_opt) {
      return -1;
    }

    nv12 = std::move(*nv12_opt);

    out_width  = frame->width;
    out_height = frame->height;

    auto tex = tex_in[0];

    // gl::ctx.ActiveTexture(GL_TEXTURE0);
    gl::ctx.BindTexture(GL_TEXTURE_2D, tex);
    // gl::ctx.TexImage2D(GL_TEXTURE_2D, 0, 4, out_width, out_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, (void *)dummy_img.begin());
    gl::ctx.TexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, out_width, out_height);

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

  std::uint32_t out_width, out_height;

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

static void __log(void *level, const char *msg) {
  BOOST_LOG(*(boost::log::sources::severity_logger<int> *)level) << msg;
}

int vaapi_make_hwdevice_ctx(platf::hwdevice_t *base, AVBufferRef **hw_device_buf) {
  auto *priv   = (VAAPIDevicePriv *)av_mallocz(sizeof(VAAPIDevicePriv));
  priv->drm_fd = -1;
  priv->drm.fd = -1;

  auto fg = util::fail_guard([priv]() {
    av_free(priv);
  });

  auto egl = (platf::egl::egl_t *)base;

  va::display_t display { vaGetDisplayDRM(egl->file.el) };
  if(!display) {
    BOOST_LOG(error) << "Couldn't open a va display from DRM with device: "sv << platf::egl::render_device;
    return -1;
  }

  egl->va_display = display.get();

  vaSetErrorCallback(display.get(), __log, &error);
  vaSetErrorCallback(display.get(), __log, &info);

  int major, minor;
  auto status = vaInitialize(display.get(), &major, &minor);
  if(status) {
    BOOST_LOG(error) << "Couldn't initialize va display: "sv << vaErrorStr(status);
    return -1;
  }

  BOOST_LOG(debug) << "vaapi vendor: "sv << vaQueryVendorString(display.get());

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

std::shared_ptr<platf::hwdevice_t> make_hwdevice() {
  auto egl = std::make_shared<egl_t>();

  if(egl->init(render_device)) {
    return nullptr;
  }

  return egl;
}
} // namespace egl

std::unique_ptr<deinit_t> init() {
  if(!gladLoaderLoadEGL(EGL_NO_DISPLAY) || !eglGetPlatformDisplay) {
    BOOST_LOG(error) << "Couldn't load EGL library"sv;
    return nullptr;
  }

  return std::make_unique<deinit_t>();
}
} // namespace platf