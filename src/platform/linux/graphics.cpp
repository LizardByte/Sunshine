/**
 * @file src/platform/linux/graphics.cpp
 * @brief Definitions for graphics related functions.
 */
// standard includes
#include <array>
#include <atomic>
#include <cstdlib>
#include <fcntl.h>
#include <string>

// local includes
#include "graphics.h"
#include "src/file_handler.h"
#include "src/logging.h"
#include "src/video.h"

// platform includes
#if !defined(__FreeBSD__)
  #include <sys/capability.h>
#endif

extern "C" {
#include <libavutil/pixdesc.h>
}

// I want to have as little build dependencies as possible
// There aren't that many DRM_FORMAT I need to use, so define them here
//
// They aren't likely to change any time soon.
#define fourcc_code(a, b, c, d) ((std::uint32_t) (a) | ((std::uint32_t) (b) << 8) | ((std::uint32_t) (c) << 16) | ((std::uint32_t) (d) << 24))
#define fourcc_mod_code(vendor, val) ((((uint64_t) vendor) << 56) | ((val) & 0x00ffffffffffffffULL))
#define DRM_FORMAT_MOD_INVALID fourcc_mod_code(0, ((1ULL << 56) - 1))
#ifndef DRM_FORMAT_XRGB8888
  #define DRM_FORMAT_XRGB8888 fourcc_code('X', 'R', '2', '4')
#endif

#ifndef DRM_FORMAT_ARGB8888
  #define DRM_FORMAT_ARGB8888 fourcc_code('A', 'R', '2', '4')
#endif

#ifndef GL_TEXTURE_EXTERNAL_OES
  #define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

#ifndef GL_TEXTURE_RECTANGLE
  #define GL_TEXTURE_RECTANGLE 0x84F5
#endif

#if !defined(SUNSHINE_SHADERS_DIR)  // for testing this needs to be defined in cmake as we don't do an install
  #define SUNSHINE_SHADERS_DIR SUNSHINE_ASSETS_DIR "/shaders/opengl"
#endif

using namespace std::literals;

namespace gl {
  GladGLContext ctx;

  static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC egl_image_target_texture_2d_fn = nullptr;

  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC egl_image_target_texture_2d() {
    return egl_image_target_texture_2d_fn;
  }

  void drain_errors(const std::string_view &prefix) {
    GLenum err;
    while ((err = ctx.GetError()) != GL_NO_ERROR) {
      BOOST_LOG(error) << "GL: "sv << prefix << ": ["sv << util::hex(err).to_string_view() << ']';
    }
  }

  tex_t::~tex_t() {
    if (size() != 0) {
      ctx.DeleteTextures(size(), begin());
    }
  }

  tex_t tex_t::make(std::size_t count) {
    tex_t textures {count};

    ctx.GenTextures(textures.size(), textures.begin());

    float color[] = {0.0f, 0.0f, 0.0f, 1.0f};

    for (auto tex : textures) {
      gl::ctx.BindTexture(GL_TEXTURE_2D, tex);
      gl::ctx.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);  // x
      gl::ctx.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);  // y
      gl::ctx.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      gl::ctx.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      gl::ctx.TexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, color);
    }

    return textures;
  }

  frame_buf_t::~frame_buf_t() {
    if (begin()) {
      ctx.DeleteFramebuffers(size(), begin());
    }
  }

  frame_buf_t frame_buf_t::make(std::size_t count) {
    frame_buf_t frame_buf {count};

    ctx.GenFramebuffers(frame_buf.size(), frame_buf.begin());

    return frame_buf;
  }

  void frame_buf_t::copy(int id, int texture, int offset_x, int offset_y, int width, int height) {
    gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, (*this)[id]);
    gl::ctx.ReadBuffer(GL_COLOR_ATTACHMENT0 + id);
    gl::ctx.BindTexture(GL_TEXTURE_2D, texture);
    gl::ctx.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, offset_x, offset_y, width, height);
  }

  std::string shader_t::err_str() {
    int length;
    ctx.GetShaderiv(handle(), GL_INFO_LOG_LENGTH, &length);

    std::string string;
    string.resize(length);

    ctx.GetShaderInfoLog(handle(), length, &length, string.data());

    string.resize(length - 1);

    return string;
  }

  util::Either<shader_t, std::string> shader_t::compile(const std::string_view &source, GLenum type) {
    shader_t shader;

    auto data = source.data();
    GLint length = source.length();

    shader._shader.el = ctx.CreateShader(type);
    ctx.ShaderSource(shader.handle(), 1, &data, &length);
    ctx.CompileShader(shader.handle());

    int status = 0;
    ctx.GetShaderiv(shader.handle(), GL_COMPILE_STATUS, &status);

    if (!status) {
      return shader.err_str();
    }

    return shader;
  }

  GLuint shader_t::handle() const {
    return _shader.el;
  }

  buffer_t buffer_t::make(util::buffer_t<GLint> &&offsets, const char *block, const std::string_view &data) {
    buffer_t buffer;
    buffer._block = block;
    buffer._size = data.size();
    buffer._offsets = std::move(offsets);

    ctx.GenBuffers(1, &buffer._buffer.el);
    ctx.BindBuffer(GL_UNIFORM_BUFFER, buffer.handle());
    ctx.BufferData(GL_UNIFORM_BUFFER, data.size(), (const std::uint8_t *) data.data(), GL_DYNAMIC_DRAW);

    return buffer;
  }

  GLuint buffer_t::handle() const {
    return _buffer.el;
  }

  const char *buffer_t::block() const {
    return _block;
  }

  void buffer_t::update(const std::string_view &view, std::size_t offset) {
    ctx.BindBuffer(GL_UNIFORM_BUFFER, handle());
    ctx.BufferSubData(GL_UNIFORM_BUFFER, offset, view.size(), (const void *) view.data());
  }

  void buffer_t::update(std::string_view *members, std::size_t count, std::size_t offset) {
    util::buffer_t<std::uint8_t> buffer {_size};

    for (int x = 0; x < count; ++x) {
      auto val = members[x];

      std::copy_n((const std::uint8_t *) val.data(), val.size(), &buffer[_offsets[x]]);
    }

    update(util::view(buffer.begin(), buffer.end()), offset);
  }

  std::string program_t::err_str() {
    int length;
    ctx.GetProgramiv(handle(), GL_INFO_LOG_LENGTH, &length);

    std::string string;
    string.resize(length);

    ctx.GetShaderInfoLog(handle(), length, &length, string.data());

    string.resize(length - 1);

    return string;
  }

  util::Either<program_t, std::string> program_t::link(const shader_t &vert, const shader_t &frag) {
    program_t program;

    program._program.el = ctx.CreateProgram();

    ctx.AttachShader(program.handle(), vert.handle());
    ctx.AttachShader(program.handle(), frag.handle());

    // p_handle stores a copy of the program handle, since program will be moved before
    // the fail guard function is called.
    auto fg = util::fail_guard([p_handle = program.handle(), &vert, &frag]() {
      ctx.DetachShader(p_handle, vert.handle());
      ctx.DetachShader(p_handle, frag.handle());
    });

    ctx.LinkProgram(program.handle());

    int status = 0;
    ctx.GetProgramiv(program.handle(), GL_LINK_STATUS, &status);

    if (!status) {
      return program.err_str();
    }

    return program;
  }

  void program_t::bind(const buffer_t &buffer) {
    ctx.UseProgram(handle());
    auto i = ctx.GetUniformBlockIndex(handle(), buffer.block());

    ctx.BindBufferBase(GL_UNIFORM_BUFFER, i, buffer.handle());
  }

  std::optional<buffer_t> program_t::uniform(const char *block, std::pair<const char *, std::string_view> *members, std::size_t count) {
    auto i = ctx.GetUniformBlockIndex(handle(), block);
    if (i == GL_INVALID_INDEX) {
      BOOST_LOG(error) << "Couldn't find index of ["sv << block << ']';
      return std::nullopt;
    }

    int size;
    ctx.GetActiveUniformBlockiv(handle(), i, GL_UNIFORM_BLOCK_DATA_SIZE, &size);

    bool error_flag = false;

    util::buffer_t<GLint> offsets {count};
    auto indices = (std::uint32_t *) alloca(count * sizeof(std::uint32_t));
    auto names = (const char **) alloca(count * sizeof(const char *));
    auto names_p = names;

    std::for_each_n(members, count, [names_p](auto &member) mutable {
      *names_p++ = std::get<0>(member);
    });

    std::fill_n(indices, count, GL_INVALID_INDEX);
    ctx.GetUniformIndices(handle(), count, names, indices);

    for (int x = 0; x < count; ++x) {
      if (indices[x] == GL_INVALID_INDEX) {
        error_flag = true;

        BOOST_LOG(error) << "Couldn't find ["sv << block << '.' << members[x].first << ']';
      }
    }

    if (error_flag) {
      return std::nullopt;
    }

    ctx.GetActiveUniformsiv(handle(), count, indices, GL_UNIFORM_OFFSET, offsets.begin());
    util::buffer_t<std::uint8_t> buffer {(std::size_t) size};

    for (int x = 0; x < count; ++x) {
      auto val = std::get<1>(members[x]);

      std::copy_n((const std::uint8_t *) val.data(), val.size(), &buffer[offsets[x]]);
    }

    return buffer_t::make(std::move(offsets), block, std::string_view {(char *) buffer.begin(), buffer.size()});
  }

  GLuint program_t::handle() const {
    return _program.el;
  }

}  // namespace gl

namespace gbm {
  device_destroy_fn device_destroy;
  create_device_fn create_device;

  int init() {
    static void *handle {nullptr};
    static bool funcs_loaded = false;

    if (funcs_loaded) {
      return 0;
    }

    if (!handle) {
      handle = dyn::handle({"libgbm.so.1", "libgbm.so"});
      if (!handle) {
        return -1;
      }
    }

    std::vector<std::tuple<GLADapiproc *, const char *>> funcs {
      {(GLADapiproc *) &device_destroy, "gbm_device_destroy"},
      {(GLADapiproc *) &create_device, "gbm_create_device"},
    };

    if (dyn::load(handle, funcs)) {
      return -1;
    }

    funcs_loaded = true;
    return 0;
  }
}  // namespace gbm

namespace egl {

  bool fail() {
    return eglGetError() != EGL_SUCCESS;
  }

  /**
   * @memberof egl::display_t
   */
  display_t make_display(std::variant<gbm::gbm_t::pointer, wl_display *, _XDisplay *> native_display) {
    int egl_platform;
    void *native_display_p;

    switch (native_display.index()) {
      case 0:
        egl_platform = EGL_PLATFORM_GBM_MESA;
        native_display_p = std::get<0>(native_display);
        break;
      case 1:
        egl_platform = EGL_PLATFORM_WAYLAND_KHR;
        native_display_p = std::get<1>(native_display);
        break;
      case 2:
        egl_platform = EGL_PLATFORM_X11_KHR;
        native_display_p = std::get<2>(native_display);
        break;
      default:
        BOOST_LOG(error) << "egl::make_display(): Index ["sv << native_display.index() << "] not implemented"sv;
        return nullptr;
    }

    // native_display.left() equals native_display.right()
    EGLDisplay raw_display = EGL_NO_DISPLAY;

    if (eglGetPlatformDisplayEXT) {
      raw_display = eglGetPlatformDisplayEXT(egl_platform, native_display_p, nullptr);
    } else if (eglGetPlatformDisplay) {
      raw_display = eglGetPlatformDisplay(egl_platform, native_display_p, nullptr);
    }

    if (raw_display == EGL_NO_DISPLAY) {
      BOOST_LOG(error) << "Couldn't open EGL display: ["sv
                       << util::hex(eglGetError()).to_string_view() << ']';
      return nullptr;
    }
    display_t display {raw_display};

    int major;
    int minor;
    if (!eglInitialize(display.get(), &major, &minor)) {
      BOOST_LOG(error) << "Couldn't initialize EGL display: ["sv << util::hex(eglGetError()).to_string_view() << ']';
      return nullptr;
    }

    if (!gladLoaderLoadEGL(display.get())) {
      BOOST_LOG(error) << "Failed to reload EGL for initialized display"sv;
      return nullptr;
    }

    const char *extension_st = eglQueryString(display.get(), EGL_EXTENSIONS);
    const char *version = eglQueryString(display.get(), EGL_VERSION);
    const char *vendor = eglQueryString(display.get(), EGL_VENDOR);
    const char *apis = eglQueryString(display.get(), EGL_CLIENT_APIS);

    BOOST_LOG(debug) << "EGL: ["sv << vendor << "]: version ["sv << version << ']';
    BOOST_LOG(debug) << "API's supported: ["sv << apis << ']';

    const char *extensions[] {
      "EGL_KHR_create_context",
      "EGL_KHR_surfaceless_context",
      "EGL_EXT_image_dma_buf_import",
      "EGL_EXT_image_dma_buf_import_modifiers",
    };

    for (auto ext : extensions) {
      if (!std::strstr(extension_st, ext)) {
        BOOST_LOG(error) << "Missing extension: ["sv << ext << ']';
        return nullptr;
      }
    }

    return display;
  }

  std::optional<ctx_t> make_ctx(display_t::pointer display) {
    bool nice_warning = false;
#if !defined(__FreeBSD__)
    cap_t caps = cap_get_proc();

    cap_value_t sys_nice = CAP_SYS_NICE;
    if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &sys_nice, CAP_SET) || cap_set_proc(caps)) {
      BOOST_LOG(debug) << "Failed to gain CAP_SYS_NICE"sv;
      nice_warning = true;
    }
    cap_free(caps);
#endif

    constexpr int conf_attr[] {
      EGL_RENDERABLE_TYPE,
      EGL_OPENGL_BIT,
      EGL_NONE
    };

    int count;
    EGLConfig conf;
    if (!eglChooseConfig(display, conf_attr, &conf, 1, &count)) {
      BOOST_LOG(error) << "Couldn't set config attributes: ["sv << util::hex(eglGetError()).to_string_view() << ']';
      return std::nullopt;
    }

    if (!eglBindAPI(EGL_OPENGL_API)) {
      BOOST_LOG(error) << "Couldn't bind API: ["sv << util::hex(eglGetError()).to_string_view() << ']';
      return std::nullopt;
    }

    const char *extension_st = eglQueryString(display, EGL_EXTENSIONS);

    std::vector<EGLint> attr;
    attr.push_back(EGL_CONTEXT_CLIENT_VERSION);
    attr.push_back(3);

    // Only add the high priority attribute if the driver explicitly supports it
    if (extension_st && std::string_view(extension_st).contains("EGL_IMG_context_priority"sv)) {
      BOOST_LOG(debug) << "EGL: High priority context supported"sv;
      attr.push_back(EGL_CONTEXT_PRIORITY_LEVEL_IMG);
      attr.push_back(EGL_CONTEXT_PRIORITY_HIGH_IMG);
    }
    attr.push_back(EGL_NONE);

    EGLContext raw_ctx = eglCreateContext(display, conf, EGL_NO_CONTEXT, attr.data());
    if (raw_ctx == EGL_NO_CONTEXT) {
      BOOST_LOG(error) << "Couldn't create EGL context: ["sv << util::hex(eglGetError()).to_string_view() << ']';
      return std::nullopt;
    }

    ctx_t ctx {display, raw_ctx};

    EGLint actual_priority = EGL_CONTEXT_PRIORITY_MEDIUM_IMG;
    std::string actual_priority_str = "MEDIUM";
    if (eglQueryContext(display, raw_ctx, EGL_CONTEXT_PRIORITY_LEVEL_IMG, &actual_priority)) {
      if (actual_priority == EGL_CONTEXT_PRIORITY_HIGH_IMG) {
        actual_priority_str = "HIGH";
      }
      if (nice_warning) {
        BOOST_LOG(warning) << "EGL: context priority set to "sv << actual_priority_str << " but CAP_SYS_NICE capability is missing"sv;
      } else {
        BOOST_LOG(info) << "EGL: context priority set to "sv << actual_priority_str;
      }
    }

    if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, raw_ctx)) {
      BOOST_LOG(error) << "Couldn't make current display"sv;
      return std::nullopt;
    }

    if (!gladLoadGLContext(&gl::ctx, eglGetProcAddress)) {
      BOOST_LOG(error) << "Couldn't load OpenGL library"sv;
      return std::nullopt;
    }

    gl::egl_image_target_texture_2d_fn =
      (gl::PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) (GLADapiproc) eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!gl::egl_image_target_texture_2d_fn) {
      BOOST_LOG(warning) << "GL: glEGLImageTargetTexture2DOES not available; DMA-BUF import will fail"sv;
    }

    // GetString returns const GLubyte* (unsigned char*); convert to std::string safely (avoids sonar cpp:S6996).
    auto gl_string = [](const GLubyte *s) {
      std::string result;
      while (s && *s) {
        result += static_cast<char>(*s++);
      }
      return result;
    };
    const auto gl_vendor = gl_string(gl::ctx.GetString(GL_VENDOR));
    const auto gl_renderer = gl_string(gl::ctx.GetString(GL_RENDERER));
    const auto gl_version = gl_string(gl::ctx.GetString(GL_VERSION));
    const auto gl_shader = gl_string(gl::ctx.GetString(GL_SHADING_LANGUAGE_VERSION));
    BOOST_LOG(debug) << "GL: vendor: "sv << gl_vendor;
    BOOST_LOG(debug) << "GL: renderer: "sv << gl_renderer;
    BOOST_LOG(debug) << "GL: version: "sv << gl_version;
    BOOST_LOG(debug) << "GL: shader: "sv << gl_shader;

    gl::ctx.PixelStorei(GL_UNPACK_ALIGNMENT, 1);

#if !defined(__FreeBSD__)
    caps = cap_get_proc();
    if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &sys_nice, CAP_CLEAR) || cap_set_proc(caps)) {
      BOOST_LOG(debug) << "Failed to drop CAP_SYS_NICE"sv;
    }
    cap_free(caps);
#endif

    return ctx;
  }

  struct plane_attr_t {
    EGLAttrib fd;
    EGLAttrib offset;
    EGLAttrib pitch;
    EGLAttrib lo;
    EGLAttrib hi;
  };

  inline plane_attr_t get_plane(std::uint32_t plane_indice) {
    switch (plane_indice) {
      case 0:
        return {
          EGL_DMA_BUF_PLANE0_FD_EXT,
          EGL_DMA_BUF_PLANE0_OFFSET_EXT,
          EGL_DMA_BUF_PLANE0_PITCH_EXT,
          EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
          EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
        };
      case 1:
        return {
          EGL_DMA_BUF_PLANE1_FD_EXT,
          EGL_DMA_BUF_PLANE1_OFFSET_EXT,
          EGL_DMA_BUF_PLANE1_PITCH_EXT,
          EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
          EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT,
        };
      case 2:
        return {
          EGL_DMA_BUF_PLANE2_FD_EXT,
          EGL_DMA_BUF_PLANE2_OFFSET_EXT,
          EGL_DMA_BUF_PLANE2_PITCH_EXT,
          EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
          EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT,
        };
      case 3:
        return {
          EGL_DMA_BUF_PLANE3_FD_EXT,
          EGL_DMA_BUF_PLANE3_OFFSET_EXT,
          EGL_DMA_BUF_PLANE3_PITCH_EXT,
          EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT,
          EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT,
        };
    }

    // Avoid warning
    return {};
  }

  bool stream_diag_env_enabled(const char *name) {
    const auto *value = std::getenv(name);
    if (!value) {
      return false;
    }

    const std::string_view flag {value};
    return flag != "0"sv && flag != "false"sv && flag != "FALSE"sv && flag != "no"sv && flag != "NO"sv;
  }

  std::string_view stream_diag_env_value(const char *name) {
    const auto *value = std::getenv(name);
    return value ? std::string_view {value} : std::string_view {};
  }

  const char *stream_diag_env_or_unset(const char *name) {
    const auto *value = std::getenv(name);
    return value ? value : "<unset>";
  }

  std::string stream_diag_fourcc_to_string(std::uint32_t fourcc) {
    std::string text;
    text.resize(4);
    for (int i = 0; i < 4; ++i) {
      const auto ch = static_cast<char>((fourcc >> (i * 8)) & 0xFF);
      text[i] = (ch >= 32 && ch <= 126) ? ch : '.';
    }
    return text;
  }

  bool stream_diag_should_log(std::atomic_uint64_t &counter) {
    const auto count = counter.fetch_add(1) + 1;
    return count <= 60 || count % 120 == 0;
  }

  std::string_view stream_diag_texture_target_to_string(GLenum target) {
    switch (target) {
      case GL_TEXTURE_2D:
        return "GL_TEXTURE_2D"sv;
      case GL_TEXTURE_EXTERNAL_OES:
        return "GL_TEXTURE_EXTERNAL_OES"sv;
      case GL_TEXTURE_RECTANGLE:
        return "GL_TEXTURE_RECTANGLE"sv;
      default:
        return "UNKNOWN"sv;
    }
  }

  bool stream_diag_drain_gl_errors(const std::string_view &prefix, GLenum *first_error = nullptr) {
    bool had_error = false;
    GLenum err;
    while ((err = gl::ctx.GetError()) != GL_NO_ERROR) {
      if (!had_error && first_error) {
        *first_error = err;
      }
      had_error = true;
      BOOST_LOG(error) << "GL: "sv << prefix << ": ["sv << util::hex(err).to_string_view() << ']';
    }
    return had_error;
  }

  std::optional<std::array<GLfloat, 4>> stream_diag_color_from_env(const char *name) {
    const auto *value = std::getenv(name);
    if (!value) {
      return std::nullopt;
    }

    const std::string_view color {value};
    if (color == "red"sv) {
      return std::array<GLfloat, 4> {1.0f, 0.0f, 0.0f, 1.0f};
    }
    if (color == "magenta"sv) {
      return std::array<GLfloat, 4> {1.0f, 0.0f, 1.0f, 1.0f};
    }

    BOOST_LOG(warning) << "STREAM_DIAG unsupported diagnostic color "sv << name << '=' << color
                       << " supported=red,magenta"sv;
    return std::nullopt;
  }

  rgb_t create_solid_color_texture(platf::img_t &img, const std::array<GLfloat, 4> &color) {
    rgb_t rgb {
      EGL_NO_DISPLAY,
      EGL_NO_IMAGE,
      gl::tex_t::make(1)
    };

    gl::ctx.BindTexture(GL_TEXTURE_2D, rgb->tex[0]);
    gl::ctx.TexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, img.width, img.height);
    gl::ctx.BindTexture(GL_TEXTURE_2D, 0);

    auto framebuf = gl::frame_buf_t::make(1);
    framebuf.bind(&rgb->tex[0], &rgb->tex[0] + 1);

    gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, framebuf[0]);
    const auto status = gl::ctx.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      BOOST_LOG(error) << "STREAM_DIAG diagnostic solid texture framebuffer incomplete status=0x"sv
                       << util::hex(status).to_string_view()
                       << " texture="sv << rgb->tex[0]
                       << " width="sv << img.width
                       << " height="sv << img.height;
    }

    GLenum attachment = GL_COLOR_ATTACHMENT0;
    gl::ctx.DrawBuffers(1, &attachment);
    gl::ctx.ClearBufferfv(GL_COLOR, 0, color.data());
    gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, 0);

    stream_diag_drain_gl_errors("STREAM_DIAG create_solid_color_texture"sv);

    return rgb;
  }

  /**
   * @brief Get EGL attributes for eglCreateImage() to import the provided surface.
   * @param surface The surface descriptor.
   * @return Vector of EGL attributes.
   */
  std::vector<EGLAttrib> surface_descriptor_to_egl_attribs(const surface_descriptor_t &surface, std::uint32_t import_fourcc, bool include_modifier, const std::string_view &attempt_label) {
    static std::atomic_uint64_t attr_log_counter {0};

    std::vector<EGLAttrib> attribs;
    const bool log_this = stream_diag_should_log(attr_log_counter);

    int fd_count = 0;
    for (auto fd : surface.fds) {
      if (fd >= 0) {
        ++fd_count;
      }
    }

    if (log_this) {
      BOOST_LOG(info) << "STREAM_DIAG egl dmabuf attribs"
                      << " attempt="sv << attempt_label
                      << " width="sv << surface.width
                      << " height="sv << surface.height
                      << " original_fourcc="sv << stream_diag_fourcc_to_string(surface.fourcc)
                      << " original_fourcc_hex=0x"sv << util::hex(surface.fourcc).to_string_view()
                      << " import_fourcc="sv << stream_diag_fourcc_to_string(import_fourcc)
                      << " import_fourcc_hex=0x"sv << util::hex(import_fourcc).to_string_view()
                      << " fd_count="sv << fd_count
                      << " modifier="sv << surface.modifier
                      << " modifier_hex=0x"sv << util::hex(surface.modifier).to_string_view()
                      << " include_modifiers="sv << include_modifier;
    }

    attribs.emplace_back(EGL_WIDTH);
    attribs.emplace_back(surface.width);
    attribs.emplace_back(EGL_HEIGHT);
    attribs.emplace_back(surface.height);
    attribs.emplace_back(EGL_LINUX_DRM_FOURCC_EXT);
    attribs.emplace_back(import_fourcc);

    for (auto x = 0; x < 4; ++x) {
      auto fd = surface.fds[x];
      if (fd < 0) {
        continue;
      }

      auto plane_attr = get_plane(x);

      attribs.emplace_back(plane_attr.fd);
      attribs.emplace_back(fd);
      attribs.emplace_back(plane_attr.offset);
      attribs.emplace_back(surface.offsets[x]);
      attribs.emplace_back(plane_attr.pitch);
      attribs.emplace_back(surface.pitches[x]);

      if (log_this) {
        BOOST_LOG(info) << "STREAM_DIAG egl dmabuf plane"
                        << " index="sv << x
                        << " fd="sv << fd
                        << " offset="sv << surface.offsets[x]
                        << " pitch="sv << surface.pitches[x]
                        << " modifier_included="sv << include_modifier;
      }

      if (include_modifier) {
        attribs.emplace_back(plane_attr.lo);
        attribs.emplace_back(surface.modifier & 0xFFFFFFFF);
        attribs.emplace_back(plane_attr.hi);
        attribs.emplace_back(surface.modifier >> 32);
      }
    }

    attribs.emplace_back(EGL_NONE);
    return attribs;
  }

  bool surface_default_include_modifier(const surface_descriptor_t &surface) {
    const bool omit_linear_modifier = stream_diag_env_enabled("SUNSHINE_STREAM_DIAG_OMIT_LINEAR_MODIFIER");
    return surface.modifier != DRM_FORMAT_MOD_INVALID && !(omit_linear_modifier && surface.modifier == 0);
  }

  GLenum stream_diag_import_target_from_env() {
    const auto target = stream_diag_env_value("SUNSHINE_STREAM_DIAG_IMPORT_TARGET");
    if (target.empty() || target == "2d"sv || target == "2D"sv) {
      return GL_TEXTURE_2D;
    }
    if (target == "external"sv || target == "EXTERNAL"sv) {
      return GL_TEXTURE_EXTERNAL_OES;
    }
    if (target == "rectangle"sv || target == "RECTANGLE"sv) {
      return GL_TEXTURE_RECTANGLE;
    }

    BOOST_LOG(warning) << "STREAM_DIAG unsupported SUNSHINE_STREAM_DIAG_IMPORT_TARGET="sv << target
                       << " supported=2d,external,rectangle; using GL_TEXTURE_2D"sv;
    return GL_TEXTURE_2D;
  }

  std::uint32_t stream_diag_import_fourcc_from_env(std::uint32_t original_fourcc) {
    const auto override = stream_diag_env_value("SUNSHINE_STREAM_DIAG_IMPORT_FOURCC_OVERRIDE");
    if (override.empty()) {
      return original_fourcc;
    }

    if (override == "AR24"sv && original_fourcc == DRM_FORMAT_XRGB8888) {
      BOOST_LOG(warning) << "STREAM_DIAG import fourcc override active"
                         << " original_fourcc="sv << stream_diag_fourcc_to_string(original_fourcc)
                         << " import_fourcc=AR24"sv;
      return DRM_FORMAT_ARGB8888;
    }

    BOOST_LOG(warning) << "STREAM_DIAG import fourcc override skipped"
                       << " requested="sv << override
                       << " original_fourcc="sv << stream_diag_fourcc_to_string(original_fourcc)
                       << " supported_override=AR24_for_XR24"sv;
    return original_fourcc;
  }

  std::optional<gl::program_t> make_import_blit_program(GLenum source_target) {
    constexpr std::string_view vertex_shader_300_es {
      R"(#version 300 es
#ifdef GL_ES
precision mediump float;
#endif
out vec2 tex;
void main()
{
  float idHigh = float(gl_VertexID >> 1);
  float idLow = float(gl_VertexID & int(1));
  float x = idHigh * 4.0 - 1.0;
  float y = idLow * 4.0 - 1.0;
  float u = idHigh * 2.0;
  float v = idLow * 2.0;
  gl_Position = vec4(x, y, 0.0, 1.0);
  tex = vec2(u, v);
}
)"
    };

    constexpr std::string_view vertex_shader_330 {
      R"(#version 330 core
out vec2 tex;
void main()
{
  float idHigh = float(gl_VertexID >> 1);
  float idLow = float(gl_VertexID & int(1));
  float x = idHigh * 4.0 - 1.0;
  float y = idLow * 4.0 - 1.0;
  float u = idHigh * 2.0;
  float v = idLow * 2.0;
  gl_Position = vec4(x, y, 0.0, 1.0);
  tex = vec2(u, v);
}
)"
    };

    constexpr std::string_view external_fragment_shader {
      R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
#ifdef GL_ES
precision mediump float;
#endif
uniform samplerExternalOES image;
in vec2 tex;
layout(location = 0) out vec4 color;
void main()
{
  color = texture(image, tex);
}
)"
    };

    constexpr std::string_view rectangle_fragment_shader {
      R"(#version 330 core
uniform sampler2DRect image;
uniform vec2 source_size;
in vec2 tex;
out vec4 color;
void main()
{
  color = texture(image, tex * source_size);
}
)"
    };

    const auto vertex_source = source_target == GL_TEXTURE_RECTANGLE ? vertex_shader_330 : vertex_shader_300_es;
    const auto fragment_source = source_target == GL_TEXTURE_RECTANGLE ? rectangle_fragment_shader : external_fragment_shader;

    auto vertex = gl::shader_t::compile(vertex_source, GL_VERTEX_SHADER);
    stream_diag_drain_gl_errors("STREAM_DIAG import blit vertex shader compile"sv);
    if (vertex.has_right()) {
      BOOST_LOG(error) << "STREAM_DIAG import blit vertex shader failed target="sv
                       << stream_diag_texture_target_to_string(source_target)
                       << " error="sv << vertex.right();
      return std::nullopt;
    }

    auto fragment = gl::shader_t::compile(fragment_source, GL_FRAGMENT_SHADER);
    stream_diag_drain_gl_errors("STREAM_DIAG import blit fragment shader compile"sv);
    if (fragment.has_right()) {
      BOOST_LOG(error) << "STREAM_DIAG import blit fragment shader failed target="sv
                       << stream_diag_texture_target_to_string(source_target)
                       << " error="sv << fragment.right();
      return std::nullopt;
    }

    auto program = gl::program_t::link(vertex.left(), fragment.left());
    stream_diag_drain_gl_errors("STREAM_DIAG import blit program link"sv);
    if (program.has_right()) {
      BOOST_LOG(error) << "STREAM_DIAG import blit program link failed target="sv
                       << stream_diag_texture_target_to_string(source_target)
                       << " error="sv << program.right();
      return std::nullopt;
    }

    return std::move(program.left());
  }

  bool blit_imported_texture_to_2d(GLenum source_target, GLuint source_texture, GLuint output_texture, int width, int height, const std::string_view &attempt_label) {
    auto program = make_import_blit_program(source_target);
    if (!program) {
      return false;
    }

    auto framebuf = gl::frame_buf_t::make(1);
    framebuf.bind(&output_texture, &output_texture + 1);

    gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, framebuf[0]);
    GLenum attachment = GL_COLOR_ATTACHMENT0;
    gl::ctx.DrawBuffers(1, &attachment);

    const auto status = gl::ctx.CheckFramebufferStatus(GL_FRAMEBUFFER);
    BOOST_LOG(info) << "STREAM_DIAG import blit framebuffer"
                    << " attempt="sv << attempt_label
                    << " source_target="sv << stream_diag_texture_target_to_string(source_target)
                    << " source_texture="sv << source_texture
                    << " output_texture="sv << output_texture
                    << " framebuffer="sv << framebuf[0]
                    << " completeness=0x"sv << util::hex(status).to_string_view();

    if (status != GL_FRAMEBUFFER_COMPLETE) {
      gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, 0);
      return false;
    }

    stream_diag_drain_gl_errors("STREAM_DIAG import blit before draw"sv);
    gl::ctx.ActiveTexture(GL_TEXTURE0);
    gl::ctx.BindTexture(source_target, source_texture);
    gl::ctx.UseProgram(program->handle());

    const auto image_uniform = gl::ctx.GetUniformLocation(program->handle(), "image");
    if (image_uniform >= 0) {
      gl::ctx.Uniform1i(image_uniform, 0);
    }

    if (source_target == GL_TEXTURE_RECTANGLE) {
      const auto source_size_uniform = gl::ctx.GetUniformLocation(program->handle(), "source_size");
      if (source_size_uniform >= 0) {
        gl::ctx.Uniform2f(source_size_uniform, static_cast<float>(width), static_cast<float>(height));
      }
    }

    gl::ctx.Viewport(0, 0, width, height);
    gl::ctx.DrawArrays(GL_TRIANGLES, 0, 3);

    GLenum first_error = GL_NO_ERROR;
    const bool had_error = stream_diag_drain_gl_errors("STREAM_DIAG import blit after draw"sv, &first_error);
    gl::ctx.BindTexture(source_target, 0);
    gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, 0);

    if (had_error) {
      BOOST_LOG(error) << "STREAM_DIAG import blit failed"
                       << " attempt="sv << attempt_label
                       << " source_target="sv << stream_diag_texture_target_to_string(source_target)
                       << " gl_error=0x"sv << util::hex(first_error).to_string_view();
      return false;
    }

    BOOST_LOG(info) << "STREAM_DIAG import blit succeeded"
                    << " attempt="sv << attempt_label
                    << " source_target="sv << stream_diag_texture_target_to_string(source_target)
                    << " source_texture="sv << source_texture
                    << " output_texture="sv << output_texture;
    return true;
  }

  bool configure_import_texture(GLenum target) {
    gl::ctx.TexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::ctx.TexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::ctx.TexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::ctx.TexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return !stream_diag_drain_gl_errors("STREAM_DIAG configure import texture"sv);
  }

  std::optional<rgb_t> try_import_source_attempt(display_t::pointer egl_display, const surface_descriptor_t &source, std::uint32_t import_fourcc, bool include_modifier, GLenum texture_target, const std::string_view &attempt_label) {
    auto attribs = surface_descriptor_to_egl_attribs(source, import_fourcc, include_modifier, attempt_label);
    EGLImage image = eglCreateImage(egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attribs.data());
    if (!image) {
      const auto egl_error = eglGetError();
      BOOST_LOG(error) << "STREAM_DIAG import attempt eglCreateImage failed"
                       << " attempt="sv << attempt_label
                       << " original_fourcc="sv << stream_diag_fourcc_to_string(source.fourcc)
                       << " import_fourcc="sv << stream_diag_fourcc_to_string(import_fourcc)
                       << " target="sv << stream_diag_texture_target_to_string(texture_target)
                       << " include_modifiers="sv << include_modifier
                       << " modifier="sv << source.modifier
                       << " pitch0="sv << source.pitches[0]
                       << " egl_error=0x"sv << util::hex(egl_error).to_string_view();
      return std::nullopt;
    }

    BOOST_LOG(info) << "STREAM_DIAG import attempt eglCreateImage succeeded"
                    << " attempt="sv << attempt_label
                    << " original_fourcc="sv << stream_diag_fourcc_to_string(source.fourcc)
                    << " import_fourcc="sv << stream_diag_fourcc_to_string(import_fourcc)
                    << " target="sv << stream_diag_texture_target_to_string(texture_target)
                    << " include_modifiers="sv << include_modifier
                    << " modifier="sv << source.modifier
                    << " pitch0="sv << source.pitches[0]
                    << " width="sv << source.width
                    << " height="sv << source.height;

    if (!gl::egl_image_target_texture_2d()) {
      BOOST_LOG(error) << "glEGLImageTargetTexture2DOES is not available; cannot import RGB DMA-BUF"sv;
      eglDestroyImage(egl_display, image);
      return std::nullopt;
    }

    if (texture_target == GL_TEXTURE_2D) {
      rgb_t rgb {
        egl_display,
        image,
        gl::tex_t::make(1)
      };

      gl::ctx.BindTexture(GL_TEXTURE_2D, rgb->tex[0]);
      stream_diag_drain_gl_errors("STREAM_DIAG import_source before glEGLImageTargetTexture2DOES"sv);
      gl::egl_image_target_texture_2d()(GL_TEXTURE_2D, rgb->xrgb8);

      GLenum first_error = GL_NO_ERROR;
      const bool import_gl_error = stream_diag_drain_gl_errors("STREAM_DIAG import_source after glEGLImageTargetTexture2DOES"sv, &first_error);
      if (import_gl_error) {
        BOOST_LOG(error) << "STREAM_DIAG import_source glEGLImageTargetTexture2DOES failed"
                         << " attempt="sv << attempt_label
                         << " target="sv << stream_diag_texture_target_to_string(texture_target)
                         << " original_fourcc="sv << stream_diag_fourcc_to_string(source.fourcc)
                         << " import_fourcc="sv << stream_diag_fourcc_to_string(import_fourcc)
                         << " modifier="sv << source.modifier
                         << " modifier_hex=0x"sv << util::hex(source.modifier).to_string_view()
                         << " include_modifiers="sv << include_modifier
                         << " pitch0="sv << source.pitches[0]
                         << " width="sv << source.width
                         << " height="sv << source.height
                         << " gl_error=0x"sv << util::hex(first_error).to_string_view();
        gl::ctx.BindTexture(GL_TEXTURE_2D, 0);
        return std::nullopt;
      }

      BOOST_LOG(info) << "STREAM_DIAG import attempt glEGLImageTargetTexture2DOES succeeded"
                      << " attempt="sv << attempt_label
                      << " target="sv << stream_diag_texture_target_to_string(texture_target)
                      << " texture="sv << rgb->tex[0]
                      << " original_fourcc="sv << stream_diag_fourcc_to_string(source.fourcc)
                      << " import_fourcc="sv << stream_diag_fourcc_to_string(import_fourcc);

      gl::ctx.BindTexture(GL_TEXTURE_2D, 0);
      return std::move(rgb);
    }

    GLuint import_texture = 0;
    gl::ctx.GenTextures(1, &import_texture);
    gl::ctx.BindTexture(texture_target, import_texture);
    configure_import_texture(texture_target);

    stream_diag_drain_gl_errors("STREAM_DIAG import_source alt target before glEGLImageTargetTexture2DOES"sv);
    gl::egl_image_target_texture_2d()(texture_target, image);

    GLenum first_error = GL_NO_ERROR;
    const bool import_gl_error = stream_diag_drain_gl_errors("STREAM_DIAG import_source alt target after glEGLImageTargetTexture2DOES"sv, &first_error);
    if (import_gl_error) {
      BOOST_LOG(error) << "STREAM_DIAG import_source glEGLImageTargetTexture2DOES failed"
                       << " attempt="sv << attempt_label
                       << " target="sv << stream_diag_texture_target_to_string(texture_target)
                       << " original_fourcc="sv << stream_diag_fourcc_to_string(source.fourcc)
                       << " import_fourcc="sv << stream_diag_fourcc_to_string(import_fourcc)
                       << " modifier="sv << source.modifier
                       << " include_modifiers="sv << include_modifier
                       << " pitch0="sv << source.pitches[0]
                       << " width="sv << source.width
                       << " height="sv << source.height
                       << " gl_error=0x"sv << util::hex(first_error).to_string_view();
      gl::ctx.BindTexture(texture_target, 0);
      gl::ctx.DeleteTextures(1, &import_texture);
      eglDestroyImage(egl_display, image);
      return std::nullopt;
    }

    BOOST_LOG(info) << "STREAM_DIAG import attempt glEGLImageTargetTexture2DOES succeeded"
                    << " attempt="sv << attempt_label
                    << " target="sv << stream_diag_texture_target_to_string(texture_target)
                    << " texture="sv << import_texture
                    << " original_fourcc="sv << stream_diag_fourcc_to_string(source.fourcc)
                    << " import_fourcc="sv << stream_diag_fourcc_to_string(import_fourcc);

    rgb_t rgb {
      EGL_NO_DISPLAY,
      EGL_NO_IMAGE,
      gl::tex_t::make(1)
    };

    gl::ctx.BindTexture(GL_TEXTURE_2D, rgb->tex[0]);
    gl::ctx.TexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, source.width, source.height);
    gl::ctx.BindTexture(GL_TEXTURE_2D, 0);

    const bool blit_ok = blit_imported_texture_to_2d(texture_target, import_texture, rgb->tex[0], source.width, source.height, attempt_label);
    gl::ctx.BindTexture(texture_target, 0);
    gl::ctx.DeleteTextures(1, &import_texture);
    eglDestroyImage(egl_display, image);

    if (!blit_ok) {
      return std::nullopt;
    }

    return std::move(rgb);
  }

  std::optional<rgb_t> import_source(display_t::pointer egl_display, const surface_descriptor_t &xrgb) {
    if (stream_diag_env_enabled("SUNSHINE_STREAM_DIAG_TRY_IMPORT_MATRIX")) {
      if (xrgb.fourcc != DRM_FORMAT_XRGB8888) {
        BOOST_LOG(warning) << "STREAM_DIAG import matrix skipped"
                           << " reason=source_fourcc_not_XR24"
                           << " original_fourcc="sv << stream_diag_fourcc_to_string(xrgb.fourcc);
      } else {
        struct import_attempt_t {
          std::string_view label;
          std::uint32_t fourcc;
          GLenum target;
          bool include_modifier;
        };

        const import_attempt_t attempts[] {
          {"matrix.XR24.2D.modifier"sv, DRM_FORMAT_XRGB8888, GL_TEXTURE_2D, xrgb.modifier != DRM_FORMAT_MOD_INVALID},
          {"matrix.XR24.2D.no_modifier"sv, DRM_FORMAT_XRGB8888, GL_TEXTURE_2D, false},
          {"matrix.AR24.2D.no_modifier"sv, DRM_FORMAT_ARGB8888, GL_TEXTURE_2D, false},
          {"matrix.XR24.external.no_modifier"sv, DRM_FORMAT_XRGB8888, GL_TEXTURE_EXTERNAL_OES, false},
          {"matrix.AR24.external.no_modifier"sv, DRM_FORMAT_ARGB8888, GL_TEXTURE_EXTERNAL_OES, false},
        };

        for (const auto &attempt : attempts) {
          auto rgb = try_import_source_attempt(egl_display, xrgb, attempt.fourcc, attempt.include_modifier, attempt.target, attempt.label);
          if (rgb) {
            BOOST_LOG(warning) << "STREAM_DIAG import matrix selected successful path"
                               << " attempt="sv << attempt.label
                               << " original_fourcc="sv << stream_diag_fourcc_to_string(xrgb.fourcc)
                               << " import_fourcc="sv << stream_diag_fourcc_to_string(attempt.fourcc)
                               << " target="sv << stream_diag_texture_target_to_string(attempt.target)
                               << " include_modifiers="sv << attempt.include_modifier;
            return rgb;
          }
        }

        BOOST_LOG(error) << "STREAM_DIAG import matrix failed all attempts"
                         << " original_fourcc="sv << stream_diag_fourcc_to_string(xrgb.fourcc)
                         << " width="sv << xrgb.width
                         << " height="sv << xrgb.height
                         << " modifier="sv << xrgb.modifier
                         << " pitch0="sv << xrgb.pitches[0];
        return std::nullopt;
      }
    }

    const auto import_fourcc = stream_diag_import_fourcc_from_env(xrgb.fourcc);
    const auto import_target = stream_diag_import_target_from_env();
    const bool include_modifier = surface_default_include_modifier(xrgb);

    std::string attempt_label {"env."};
    attempt_label += stream_diag_fourcc_to_string(import_fourcc);
    attempt_label += '.';
    attempt_label += std::string {stream_diag_texture_target_to_string(import_target)};
    attempt_label += include_modifier ? ".modifier" : ".no_modifier";

    auto rgb = try_import_source_attempt(egl_display, xrgb, import_fourcc, include_modifier, import_target, attempt_label);
    if (rgb) {
      BOOST_LOG(info) << "STREAM_DIAG import_source selected successful path"
                      << " attempt="sv << attempt_label
                      << " original_fourcc="sv << stream_diag_fourcc_to_string(xrgb.fourcc)
                      << " import_fourcc="sv << stream_diag_fourcc_to_string(import_fourcc)
                      << " target="sv << stream_diag_texture_target_to_string(import_target)
                      << " include_modifiers="sv << include_modifier;
    }
    return rgb;
  }

  bool diagnostic_gpu_solid_color_enabled() {
    return stream_diag_color_from_env("SUNSHINE_STREAM_DIAG_GPU_SOLID_COLOR").has_value();
  }

  rgb_t create_diagnostic_solid_color(platf::img_t &img) {
    static std::atomic_bool logged_enabled {false};
    const auto color = stream_diag_color_from_env("SUNSHINE_STREAM_DIAG_GPU_SOLID_COLOR")
                         .value_or(std::array<GLfloat, 4> {1.0f, 0.0f, 1.0f, 1.0f});

    if (!logged_enabled.exchange(true)) {
      BOOST_LOG(warning) << "STREAM_DIAG GPU solid color source active"
                         << " color="sv << stream_diag_env_or_unset("SUNSHINE_STREAM_DIAG_GPU_SOLID_COLOR")
                         << " width="sv << img.width
                         << " height="sv << img.height;
    }

    return create_solid_color_texture(img, color);
  }

  /**
   * @brief Create a black RGB texture of the specified image size.
   * @param img The image to use for texture sizing.
   * @return The new RGB texture.
   */
  rgb_t create_blank(platf::img_t &img) {
    const auto color = stream_diag_color_from_env("SUNSHINE_STREAM_DIAG_BLANK_COLOR");
    if (color) {
      static std::atomic_bool logged_blank_color {false};
      if (!logged_blank_color.exchange(true)) {
        BOOST_LOG(warning) << "STREAM_DIAG create_blank diagnostic color active"
                           << " color="sv << stream_diag_env_or_unset("SUNSHINE_STREAM_DIAG_BLANK_COLOR")
                           << " width="sv << img.width
                           << " height="sv << img.height;
      }
      return create_solid_color_texture(img, *color);
    }

    return create_solid_color_texture(img, {0.0f, 0.0f, 0.0f, 1.0f});
  }

  std::optional<nv12_t> import_target(display_t::pointer egl_display, std::array<file_t, nv12_img_t::num_fds> &&fds, const surface_descriptor_t &y, const surface_descriptor_t &uv) {
    auto y_attribs = surface_descriptor_to_egl_attribs(y, y.fourcc, surface_default_include_modifier(y), "target.y"sv);
    auto uv_attribs = surface_descriptor_to_egl_attribs(uv, uv.fourcc, surface_default_include_modifier(uv), "target.uv"sv);

    nv12_t nv12 {
      egl_display,
      eglCreateImage(egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, y_attribs.data()),
      eglCreateImage(egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, uv_attribs.data()),
      gl::tex_t::make(2),
      gl::frame_buf_t::make(2),
      std::move(fds)
    };

    if (!nv12->r8 || !nv12->bg88) {
      BOOST_LOG(error) << "Couldn't import YUV target: "sv << util::hex(eglGetError()).to_string_view();

      return std::nullopt;
    }

    gl::ctx.BindTexture(GL_TEXTURE_2D, nv12->tex[0]);
    if (!gl::egl_image_target_texture_2d()) {
      BOOST_LOG(error) << "glEGLImageTargetTexture2DOES is not available; cannot import YUV DMA-BUF"sv;
      return std::nullopt;
    }
    gl::egl_image_target_texture_2d()(GL_TEXTURE_2D, nv12->r8);

    gl::ctx.BindTexture(GL_TEXTURE_2D, nv12->tex[1]);
    gl::egl_image_target_texture_2d()(GL_TEXTURE_2D, nv12->bg88);

    nv12->buf.bind(std::begin(nv12->tex), std::end(nv12->tex));

    GLenum attachments[] {
      GL_COLOR_ATTACHMENT0,
      GL_COLOR_ATTACHMENT1
    };

    for (int x = 0; x < sizeof(attachments) / sizeof(decltype(attachments[0])); ++x) {
      gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, nv12->buf[x]);
      gl::ctx.DrawBuffers(1, &attachments[x]);

      const float y_black[] = {0.0f, 0.0f, 0.0f, 0.0f};
      const float uv_black[] = {0.5f, 0.5f, 0.5f, 0.5f};
      gl::ctx.ClearBufferfv(GL_COLOR, 0, x == 0 ? y_black : uv_black);
    }

    gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, 0);

    gl_drain_errors;

    return nv12;
  }

  /**
   * @brief Create biplanar YUV textures to render into.
   * @param width Width of the target frame.
   * @param height Height of the target frame.
   * @param format Format of the target frame.
   * @return The new RGB texture.
   */
  std::optional<nv12_t> create_target(int width, int height, AVPixelFormat format) {
    nv12_t nv12 {
      EGL_NO_DISPLAY,
      EGL_NO_IMAGE,
      EGL_NO_IMAGE,
      gl::tex_t::make(2),
      gl::frame_buf_t::make(2),
    };

    GLint y_format;
    GLint uv_format;

    // Determine the size of each plane element
    auto fmt_desc = av_pix_fmt_desc_get(format);
    if (fmt_desc->comp[0].depth <= 8) {
      y_format = GL_R8;
      uv_format = GL_RG8;
    } else if (fmt_desc->comp[0].depth <= 16) {
      y_format = GL_R16;
      uv_format = GL_RG16;
    } else {
      BOOST_LOG(error) << "Unsupported target pixel format: "sv << format;
      return std::nullopt;
    }

    gl::ctx.BindTexture(GL_TEXTURE_2D, nv12->tex[0]);
    gl::ctx.TexStorage2D(GL_TEXTURE_2D, 1, y_format, width, height);

    gl::ctx.BindTexture(GL_TEXTURE_2D, nv12->tex[1]);
    gl::ctx.TexStorage2D(GL_TEXTURE_2D, 1, uv_format, width >> fmt_desc->log2_chroma_w, height >> fmt_desc->log2_chroma_h);

    nv12->buf.bind(std::begin(nv12->tex), std::end(nv12->tex));

    GLenum attachments[] {
      GL_COLOR_ATTACHMENT0,
      GL_COLOR_ATTACHMENT1
    };

    for (int x = 0; x < sizeof(attachments) / sizeof(decltype(attachments[0])); ++x) {
      gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, nv12->buf[x]);
      gl::ctx.DrawBuffers(1, &attachments[x]);

      const float y_black[] = {0.0f, 0.0f, 0.0f, 0.0f};
      const float uv_black[] = {0.5f, 0.5f, 0.5f, 0.5f};
      gl::ctx.ClearBufferfv(GL_COLOR, 0, x == 0 ? y_black : uv_black);
    }

    gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, 0);

    gl_drain_errors;

    return nv12;
  }

  void sws_t::apply_colorspace(const video::sunshine_colorspace_t &colorspace) {
    auto color_p = video::color_vectors_from_colorspace(colorspace, true);

    std::string_view members[] {
      util::view(color_p->color_vec_y),
      util::view(color_p->color_vec_u),
      util::view(color_p->color_vec_v),
      util::view(color_p->range_y),
      util::view(color_p->range_uv),
    };

    color_matrix.update(members, sizeof(members) / sizeof(decltype(members[0])));

    program[0].bind(color_matrix);
    program[1].bind(color_matrix);
  }

  std::optional<sws_t> sws_t::make(int in_width, int in_height, int out_width, int out_height, gl::tex_t &&tex) {
    sws_t sws;

    sws.serial = std::numeric_limits<std::uint64_t>::max();

    // Ensure aspect ratio is maintained
    auto scalar = std::fminf(out_width / (float) in_width, out_height / (float) in_height);
    auto out_width_f = in_width * scalar;
    auto out_height_f = in_height * scalar;

    // result is always positive
    auto offsetX_f = (out_width - out_width_f) / 2;
    auto offsetY_f = (out_height - out_height_f) / 2;

    sws.out_width = out_width_f;
    sws.out_height = out_height_f;

    sws.in_width = in_width;
    sws.in_height = in_height;

    sws.offsetX = offsetX_f;
    sws.offsetY = offsetY_f;

    auto width_i = 1.0f / sws.out_width;

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
      for (int x = 0; x < count; ++x) {
        auto &compiled_source = compiled_sources[x];

        compiled_source = gl::shader_t::compile(file_handler::read_file(sources[x]), shader_type[x % 2]);
        gl_drain_errors;

        if (compiled_source.has_right()) {
          BOOST_LOG(error) << sources[x] << ": "sv << compiled_source.right();
          error_flag = true;
        }
      }

      if (error_flag) {
        return std::nullopt;
      }

      auto program = gl::program_t::link(compiled_sources[3].left(), compiled_sources[4].left());
      if (program.has_right()) {
        BOOST_LOG(error) << "GL linker: "sv << program.right();
        return std::nullopt;
      }

      // Cursor - shader
      sws.program[2] = std::move(program.left());

      program = gl::program_t::link(compiled_sources[1].left(), compiled_sources[0].left());
      if (program.has_right()) {
        BOOST_LOG(error) << "GL linker: "sv << program.right();
        return std::nullopt;
      }

      // UV - shader
      sws.program[1] = std::move(program.left());

      program = gl::program_t::link(compiled_sources[3].left(), compiled_sources[2].left());
      if (program.has_right()) {
        BOOST_LOG(error) << "GL linker: "sv << program.right();
        return std::nullopt;
      }

      // Y - shader
      sws.program[0] = std::move(program.left());
    }

    auto loc_width_i = gl::ctx.GetUniformLocation(sws.program[1].handle(), "width_i");
    if (loc_width_i < 0) {
      BOOST_LOG(error) << "Couldn't find uniform [width_i]"sv;
      return std::nullopt;
    }

    gl::ctx.UseProgram(sws.program[1].handle());
    gl::ctx.Uniform1fv(loc_width_i, 1, &width_i);

    auto color_p = video::color_vectors_from_colorspace({video::colorspace_e::rec601, false, 8}, true);
    std::pair<const char *, std::string_view> members[] {
      std::make_pair("color_vec_y", util::view(color_p->color_vec_y)),
      std::make_pair("color_vec_u", util::view(color_p->color_vec_u)),
      std::make_pair("color_vec_v", util::view(color_p->color_vec_v)),
      std::make_pair("range_y", util::view(color_p->range_y)),
      std::make_pair("range_uv", util::view(color_p->range_uv)),
    };

    auto color_matrix = sws.program[0].uniform("ColorMatrix", members, sizeof(members) / sizeof(decltype(members[0])));
    if (!color_matrix) {
      return std::nullopt;
    }

    sws.color_matrix = std::move(*color_matrix);

    sws.tex = std::move(tex);

    sws.cursor_framebuffer = gl::frame_buf_t::make(1);
    sws.cursor_framebuffer.bind(&sws.tex[0], &sws.tex[1]);

    sws.program[0].bind(sws.color_matrix);
    sws.program[1].bind(sws.color_matrix);

    gl::ctx.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    gl_drain_errors;

    return sws;
  }

  int sws_t::blank(gl::frame_buf_t &fb, int offsetX, int offsetY, int width, int height) {
    auto f = [&]() {
      std::swap(offsetX, this->offsetX);
      std::swap(offsetY, this->offsetY);
      std::swap(width, this->out_width);
      std::swap(height, this->out_height);
    };

    f();
    auto fg = util::fail_guard(f);

    return convert(fb);
  }

  std::optional<sws_t> sws_t::make(int in_width, int in_height, int out_width, int out_height, AVPixelFormat format) {
    GLint gl_format;

    // Decide the bit depth format of the backing texture based the target frame format
    auto fmt_desc = av_pix_fmt_desc_get(format);
    switch (fmt_desc->comp[0].depth) {
      case 8:
        gl_format = GL_RGBA8;
        break;

      case 10:
        gl_format = GL_RGB10_A2;
        break;

      case 12:
        gl_format = GL_RGBA12;
        break;

      case 16:
        gl_format = GL_RGBA16;
        break;

      default:
        BOOST_LOG(error) << "Unsupported pixel format for EGL frame: "sv << (int) format;
        return std::nullopt;
    }

    auto tex = gl::tex_t::make(2);
    gl::ctx.BindTexture(GL_TEXTURE_2D, tex[0]);
    gl::ctx.TexStorage2D(GL_TEXTURE_2D, 1, gl_format, in_width, in_height);

    return make(in_width, in_height, out_width, out_height, std::move(tex));
  }

  void sws_t::load_ram(platf::img_t &img) {
    loaded_texture = tex[0];

    gl::ctx.BindTexture(GL_TEXTURE_2D, loaded_texture);
    gl::ctx.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.width, img.height, GL_BGRA, GL_UNSIGNED_BYTE, img.data);
  }

  void sws_t::load_vram(img_descriptor_t &img, int offset_x, int offset_y, int texture) {
    // When only a sub-part of the image must be encoded...
    const bool copy = offset_x || offset_y || img.sd.width != in_width || img.sd.height != in_height;
    if (copy) {
      auto framebuf = gl::frame_buf_t::make(1);
      framebuf.bind(&texture, &texture + 1);

      loaded_texture = tex[0];
      framebuf.copy(0, loaded_texture, offset_x, offset_y, in_width, in_height);
    } else {
      loaded_texture = texture;
    }

    if (img.data) {
      GLenum attachment = GL_COLOR_ATTACHMENT0;

      gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, cursor_framebuffer[0]);
      gl::ctx.UseProgram(program[2].handle());

      // When a copy has already been made...
      if (!copy) {
        gl::ctx.BindTexture(GL_TEXTURE_2D, texture);
        gl::ctx.DrawBuffers(1, &attachment);

        gl::ctx.Viewport(0, 0, in_width, in_height);
        gl::ctx.DrawArrays(GL_TRIANGLES, 0, 3);

        loaded_texture = tex[0];
      }

      gl::ctx.BindTexture(GL_TEXTURE_2D, tex[1]);
      if (serial != img.serial) {
        serial = img.serial;

        gl::ctx.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.src_w, img.src_h, 0, GL_BGRA, GL_UNSIGNED_BYTE, img.data);
      }

      gl::ctx.Enable(GL_BLEND);

      gl::ctx.DrawBuffers(1, &attachment);

#ifndef NDEBUG
      auto status = gl::ctx.CheckFramebufferStatus(GL_FRAMEBUFFER);
      if (status != GL_FRAMEBUFFER_COMPLETE) {
        BOOST_LOG(error) << "Pass Cursor: CheckFramebufferStatus() --> [0x"sv << util::hex(status).to_string_view() << ']';
        return;
      }
#endif

      gl::ctx.Viewport(img.x, img.y, img.width, img.height);
      gl::ctx.DrawArrays(GL_TRIANGLES, 0, 3);

      gl::ctx.Disable(GL_BLEND);

      gl::ctx.BindTexture(GL_TEXTURE_2D, 0);
      gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }
  }

  int sws_t::convert(gl::frame_buf_t &fb) {
    static std::atomic_uint64_t convert_log_counter {0};
    const bool log_this = stream_diag_should_log(convert_log_counter);

    gl::ctx.BindTexture(GL_TEXTURE_2D, loaded_texture);

    if (log_this) {
      BOOST_LOG(info) << "STREAM_DIAG rgb_to_yuv begin"
                      << " source_texture="sv << loaded_texture
                      << " framebuffer0="sv << fb[0]
                      << " framebuffer1="sv << fb[1]
                      << " in_width="sv << in_width
                      << " in_height="sv << in_height
                      << " out_width="sv << out_width
                      << " out_height="sv << out_height
                      << " offsetX="sv << offsetX
                      << " offsetY="sv << offsetY;
    }

    GLenum attachments[] {
      GL_COLOR_ATTACHMENT0,
      GL_COLOR_ATTACHMENT1
    };

    for (int x = 0; x < sizeof(attachments) / sizeof(decltype(attachments[0])); ++x) {
      gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, fb[x]);
      gl::ctx.DrawBuffers(1, &attachments[x]);

      auto status = gl::ctx.CheckFramebufferStatus(GL_FRAMEBUFFER);
      if (log_this) {
        GLint target_texture = 0;
        gl::ctx.GetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, attachments[x], GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &target_texture);
        BOOST_LOG(info) << "STREAM_DIAG rgb_to_yuv framebuffer"
                        << " plane="sv << x
                        << " framebuffer="sv << fb[x]
                        << " target_texture="sv << target_texture
                        << " completeness=0x"sv << util::hex(status).to_string_view();
      }

      if (status != GL_FRAMEBUFFER_COMPLETE) {
        BOOST_LOG(error) << "Pass "sv << x << ": CheckFramebufferStatus() --> [0x"sv << util::hex(status).to_string_view() << ']';
#ifndef NDEBUG
        return -1;
#endif
      }

      if (log_this) {
        stream_diag_drain_gl_errors("STREAM_DIAG rgb_to_yuv before draw"sv);
      }
      gl::ctx.UseProgram(program[x].handle());
      gl::ctx.Viewport(offsetX / (x + 1), offsetY / (x + 1), out_width / (x + 1), out_height / (x + 1));
      gl::ctx.DrawArrays(GL_TRIANGLES, 0, 3);
      if (log_this) {
        stream_diag_drain_gl_errors("STREAM_DIAG rgb_to_yuv after draw"sv);
      }
    }

    gl::ctx.BindTexture(GL_TEXTURE_2D, 0);

    gl::ctx.Flush();

    return 0;
  }
}  // namespace egl

void free_frame(AVFrame *frame) {
  av_frame_free(&frame);
}
