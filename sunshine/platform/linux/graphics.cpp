#include "graphics.h"
#include "sunshine/video.h"

#include <fcntl.h>

// I want to have as little build dependencies as possible
// There aren't that many DRM_FORMAT I need to use, so define them here
//
// They aren't likely to change any time soon.
#define fourcc_code(a, b, c, d) ((std::uint32_t)(a) | ((std::uint32_t)(b) << 8) | \
                                 ((std::uint32_t)(c) << 16) | ((std::uint32_t)(d) << 24))
#define fourcc_mod_code(vendor, val) ((((uint64_t)vendor) << 56) | ((val)&0x00ffffffffffffffULL))
#define DRM_FORMAT_R8 fourcc_code('R', '8', ' ', ' ')       /* [7:0] R */
#define DRM_FORMAT_GR88 fourcc_code('G', 'R', '8', '8')     /* [15:0] G:R 8:8 little endian */
#define DRM_FORMAT_ARGB8888 fourcc_code('A', 'R', '2', '4') /* [31:0] A:R:G:B 8:8:8:8 little endian */
#define DRM_FORMAT_XRGB8888 fourcc_code('X', 'R', '2', '4') /* [31:0] x:R:G:B 8:8:8:8 little endian */
#define DRM_FORMAT_XBGR8888 fourcc_code('X', 'B', '2', '4') /* [31:0] x:B:G:R 8:8:8:8 little endian */
#define DRM_FORMAT_MOD_INVALID fourcc_mod_code(0, ((1ULL << 56) - 1))

#define SUNSHINE_SHADERS_DIR SUNSHINE_ASSETS_DIR "/shaders/opengl"

using namespace std::literals;
namespace gl {
GladGLContext ctx;

void drain_errors(const std::string_view &prefix) {
  GLenum err;
  while((err = ctx.GetError()) != GL_NO_ERROR) {
    BOOST_LOG(error) << "GL: "sv << prefix << ": ["sv << util::hex(err).to_string_view() << ']';
  }
}

tex_t::~tex_t() {
  if(!size() == 0) {
    ctx.DeleteTextures(size(), begin());
  }
}

tex_t tex_t::make(std::size_t count) {
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

frame_buf_t::~frame_buf_t() {
  if(begin()) {
    ctx.DeleteFramebuffers(size(), begin());
  }
}

frame_buf_t frame_buf_t::make(std::size_t count) {
  frame_buf_t frame_buf { count };

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

GLuint shader_t::handle() const {
  return _shader.el;
}

buffer_t buffer_t::make(util::buffer_t<GLint> &&offsets, const char *block, const std::string_view &data) {
  buffer_t buffer;
  buffer._block   = block;
  buffer._size    = data.size();
  buffer._offsets = std::move(offsets);

  ctx.GenBuffers(1, &buffer._buffer.el);
  ctx.BindBuffer(GL_UNIFORM_BUFFER, buffer.handle());
  ctx.BufferData(GL_UNIFORM_BUFFER, data.size(), (const std::uint8_t *)data.data(), GL_DYNAMIC_DRAW);

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
  ctx.BufferSubData(GL_UNIFORM_BUFFER, offset, view.size(), (const void *)view.data());
}

void buffer_t::update(std::string_view *members, std::size_t count, std::size_t offset) {
  util::buffer_t<std::uint8_t> buffer { _size };

  for(int x = 0; x < count; ++x) {
    auto val = members[x];

    std::copy_n((const std::uint8_t *)val.data(), val.size(), &buffer[_offsets[x]]);
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

void program_t::bind(const buffer_t &buffer) {
  ctx.UseProgram(handle());
  auto i = ctx.GetUniformBlockIndex(handle(), buffer.block());

  ctx.BindBufferBase(GL_UNIFORM_BUFFER, i, buffer.handle());
}

std::optional<buffer_t> program_t::uniform(const char *block, std::pair<const char *, std::string_view> *members, std::size_t count) {
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

GLuint program_t::handle() const {
  return _program.el;
}

} // namespace gl

namespace gbm {
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

namespace egl {
constexpr auto EGL_LINUX_DMA_BUF_EXT              = 0x3270;
constexpr auto EGL_LINUX_DRM_FOURCC_EXT           = 0x3271;
constexpr auto EGL_DMA_BUF_PLANE0_FD_EXT          = 0x3272;
constexpr auto EGL_DMA_BUF_PLANE0_OFFSET_EXT      = 0x3273;
constexpr auto EGL_DMA_BUF_PLANE0_PITCH_EXT       = 0x3274;
constexpr auto EGL_DMA_BUF_PLANE1_FD_EXT          = 0x3275;
constexpr auto EGL_DMA_BUF_PLANE1_OFFSET_EXT      = 0x3276;
constexpr auto EGL_DMA_BUF_PLANE1_PITCH_EXT       = 0x3277;
constexpr auto EGL_DMA_BUF_PLANE2_FD_EXT          = 0x3278;
constexpr auto EGL_DMA_BUF_PLANE2_OFFSET_EXT      = 0x3279;
constexpr auto EGL_DMA_BUF_PLANE2_PITCH_EXT       = 0x327A;
constexpr auto EGL_DMA_BUF_PLANE3_FD_EXT          = 0x3440;
constexpr auto EGL_DMA_BUF_PLANE3_OFFSET_EXT      = 0x3441;
constexpr auto EGL_DMA_BUF_PLANE3_PITCH_EXT       = 0x3442;
constexpr auto EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT = 0x3443;
constexpr auto EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT = 0x3444;
constexpr auto EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT = 0x3445;
constexpr auto EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT = 0x3446;
constexpr auto EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT = 0x3447;
constexpr auto EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT = 0x3448;
constexpr auto EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT = 0x3449;
constexpr auto EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT = 0x344A;

bool fail() {
  return eglGetError() != EGL_SUCCESS;
}

display_t make_display(std::variant<gbm::gbm_t::pointer, wl_display *, _XDisplay *> native_display) {
  constexpr auto EGL_PLATFORM_GBM_MESA    = 0x31D7;
  constexpr auto EGL_PLATFORM_WAYLAND_KHR = 0x31D8;
  constexpr auto EGL_PLATFORM_X11_KHR     = 0x31D5;

  int egl_platform;
  void *native_display_p;

  switch(native_display.index()) {
  case 0:
    egl_platform     = EGL_PLATFORM_GBM_MESA;
    native_display_p = std::get<0>(native_display);
    break;
  case 1:
    egl_platform     = EGL_PLATFORM_WAYLAND_KHR;
    native_display_p = std::get<1>(native_display);
    break;
  case 2:
    egl_platform     = EGL_PLATFORM_X11_KHR;
    native_display_p = std::get<2>(native_display);
    break;
  default:
    BOOST_LOG(error) << "egl::make_display(): Index ["sv << native_display.index() << "] not implemented"sv;
    return nullptr;
  }

  // native_display.left() equals native_display.right()
  display_t display = eglGetPlatformDisplay(egl_platform, native_display_p, nullptr);

  if(fail()) {
    BOOST_LOG(error) << "Couldn't open EGL display: ["sv << util::hex(eglGetError()).to_string_view() << ']';
    return nullptr;
  }

  int major, minor;
  if(!eglInitialize(display.get(), &major, &minor)) {
    BOOST_LOG(error) << "Couldn't initialize EGL display: ["sv << util::hex(eglGetError()).to_string_view() << ']';
    return nullptr;
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
  };

  for(auto ext : extensions) {
    if(!std::strstr(extension_st, ext)) {
      BOOST_LOG(error) << "Missing extension: ["sv << ext << ']';
      return nullptr;
    }
  }

  return display;
}

std::optional<ctx_t> make_ctx(display_t::pointer display) {
  constexpr int conf_attr[] {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE
  };

  int count;
  EGLConfig conf;
  if(!eglChooseConfig(display, conf_attr, &conf, 1, &count)) {
    BOOST_LOG(error) << "Couldn't set config attributes: ["sv << util::hex(eglGetError()).to_string_view() << ']';
    return std::nullopt;
  }

  if(!eglBindAPI(EGL_OPENGL_API)) {
    BOOST_LOG(error) << "Couldn't bind API: ["sv << util::hex(eglGetError()).to_string_view() << ']';
    return std::nullopt;
  }

  constexpr int attr[] {
    EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE
  };

  ctx_t ctx { display, eglCreateContext(display, conf, EGL_NO_CONTEXT, attr) };
  if(fail()) {
    BOOST_LOG(error) << "Couldn't create EGL context: ["sv << util::hex(eglGetError()).to_string_view() << ']';
    return std::nullopt;
  }

  TUPLE_EL_REF(ctx_p, 1, ctx.el);
  if(!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx_p)) {
    BOOST_LOG(error) << "Couldn't make current display"sv;
    return std::nullopt;
  }

  if(!gladLoadGLContext(&gl::ctx, eglGetProcAddress)) {
    BOOST_LOG(error) << "Couldn't load OpenGL library"sv;
    return std::nullopt;
  }

  BOOST_LOG(debug) << "GL: vendor: "sv << gl::ctx.GetString(GL_VENDOR);
  BOOST_LOG(debug) << "GL: renderer: "sv << gl::ctx.GetString(GL_RENDERER);
  BOOST_LOG(debug) << "GL: version: "sv << gl::ctx.GetString(GL_VERSION);
  BOOST_LOG(debug) << "GL: shader: "sv << gl::ctx.GetString(GL_SHADING_LANGUAGE_VERSION);

  gl::ctx.PixelStorei(GL_UNPACK_ALIGNMENT, 1);

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
  switch(plane_indice) {
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

std::optional<rgb_t> import_source(display_t::pointer egl_display, const surface_descriptor_t &xrgb) {
  EGLAttrib attribs[47];
  int atti        = 0;
  attribs[atti++] = EGL_WIDTH;
  attribs[atti++] = xrgb.width;
  attribs[atti++] = EGL_HEIGHT;
  attribs[atti++] = xrgb.height;
  attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
  attribs[atti++] = xrgb.fourcc;

  for(auto x = 0; x < 4; ++x) {
    auto fd = xrgb.fds[x];

    if(fd < 0) {
      continue;
    }

    auto plane_attr = get_plane(x);

    attribs[atti++] = plane_attr.fd;
    attribs[atti++] = fd;
    attribs[atti++] = plane_attr.offset;
    attribs[atti++] = xrgb.offsets[x];
    attribs[atti++] = plane_attr.pitch;
    attribs[atti++] = xrgb.pitches[x];

    if(xrgb.modifier != DRM_FORMAT_MOD_INVALID) {
      attribs[atti++] = plane_attr.lo;
      attribs[atti++] = xrgb.modifier & 0xFFFFFFFF;
      attribs[atti++] = plane_attr.hi;
      attribs[atti++] = xrgb.modifier >> 32;
    }
  }
  attribs[atti++] = EGL_NONE;

  rgb_t rgb {
    egl_display,
    eglCreateImage(egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attribs),
    gl::tex_t::make(1)
  };

  if(!rgb->xrgb8) {
    BOOST_LOG(error) << "Couldn't import RGB Image: "sv << util::hex(eglGetError()).to_string_view();

    return std::nullopt;
  }

  gl::ctx.BindTexture(GL_TEXTURE_2D, rgb->tex[0]);
  gl::ctx.EGLImageTargetTexture2DOES(GL_TEXTURE_2D, rgb->xrgb8);

  gl::ctx.BindTexture(GL_TEXTURE_2D, 0);

  gl_drain_errors;

  return rgb;
}

std::optional<nv12_t> import_target(display_t::pointer egl_display, std::array<file_t, nv12_img_t::num_fds> &&fds, const surface_descriptor_t &r8, const surface_descriptor_t &gr88) {
  EGLAttrib img_attr_planes[2][13] {
    { EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_R8,
      EGL_WIDTH, r8.width,
      EGL_HEIGHT, r8.height,
      EGL_DMA_BUF_PLANE0_FD_EXT, r8.fds[0],
      EGL_DMA_BUF_PLANE0_OFFSET_EXT, r8.offsets[0],
      EGL_DMA_BUF_PLANE0_PITCH_EXT, r8.pitches[0],
      EGL_NONE },

    { EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_GR88,
      EGL_WIDTH, gr88.width,
      EGL_HEIGHT, gr88.height,
      EGL_DMA_BUF_PLANE0_FD_EXT, r8.fds[0],
      EGL_DMA_BUF_PLANE0_OFFSET_EXT, gr88.offsets[0],
      EGL_DMA_BUF_PLANE0_PITCH_EXT, gr88.pitches[0],
      EGL_NONE },
  };

  nv12_t nv12 {
    egl_display,
    eglCreateImage(egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, img_attr_planes[0]),
    eglCreateImage(egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, img_attr_planes[1]),
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

void sws_t::set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) {
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

  program[0].bind(color_matrix);
  program[1].bind(color_matrix);
}

std::optional<sws_t> sws_t::make(int in_width, int in_height, int out_width, int out_heigth, gl::tex_t &&tex) {
  sws_t sws;

  sws.serial = std::numeric_limits<std::uint64_t>::max();

  // Ensure aspect ratio is maintained
  auto scalar       = std::fminf(out_width / (float)in_width, out_heigth / (float)in_height);
  auto out_width_f  = in_width * scalar;
  auto out_height_f = in_height * scalar;

  // result is always positive
  auto offsetX_f = (out_width - out_width_f) / 2;
  auto offsetY_f = (out_heigth - out_height_f) / 2;

  sws.out_width  = out_width_f;
  sws.out_height = out_height_f;

  sws.in_width  = in_width;
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
      return std::nullopt;
    }

    auto program = gl::program_t::link(compiled_sources[3].left(), compiled_sources[4].left());
    if(program.has_right()) {
      BOOST_LOG(error) << "GL linker: "sv << program.right();
      return std::nullopt;
    }

    // Cursor - shader
    sws.program[2] = std::move(program.left());

    program = gl::program_t::link(compiled_sources[1].left(), compiled_sources[0].left());
    if(program.has_right()) {
      BOOST_LOG(error) << "GL linker: "sv << program.right();
      return std::nullopt;
    }

    // UV - shader
    sws.program[1] = std::move(program.left());

    program = gl::program_t::link(compiled_sources[3].left(), compiled_sources[2].left());
    if(program.has_right()) {
      BOOST_LOG(error) << "GL linker: "sv << program.right();
      return std::nullopt;
    }

    // Y - shader
    sws.program[0] = std::move(program.left());
  }

  auto loc_width_i = gl::ctx.GetUniformLocation(sws.program[1].handle(), "width_i");
  if(loc_width_i < 0) {
    BOOST_LOG(error) << "Couldn't find uniform [width_i]"sv;
    return std::nullopt;
  }

  gl::ctx.UseProgram(sws.program[1].handle());
  gl::ctx.Uniform1fv(loc_width_i, 1, &width_i);

  auto color_p = &video::colors[0];
  std::pair<const char *, std::string_view> members[] {
    std::make_pair("color_vec_y", util::view(color_p->color_vec_y)),
    std::make_pair("color_vec_u", util::view(color_p->color_vec_u)),
    std::make_pair("color_vec_v", util::view(color_p->color_vec_v)),
    std::make_pair("range_y", util::view(color_p->range_y)),
    std::make_pair("range_uv", util::view(color_p->range_uv)),
  };

  auto color_matrix = sws.program[0].uniform("ColorMatrix", members, sizeof(members) / sizeof(decltype(members[0])));
  if(!color_matrix) {
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

  return std::move(sws);
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

std::optional<sws_t> sws_t::make(int in_width, int in_height, int out_width, int out_heigth) {
  auto tex = gl::tex_t::make(2);
  gl::ctx.BindTexture(GL_TEXTURE_2D, tex[0]);
  gl::ctx.TexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, in_width, in_height);

  return make(in_width, in_height, out_width, out_heigth, std::move(tex));
}

void sws_t::load_ram(platf::img_t &img) {
  loaded_texture = tex[0];

  gl::ctx.BindTexture(GL_TEXTURE_2D, loaded_texture);
  gl::ctx.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.width, img.height, GL_BGRA, GL_UNSIGNED_BYTE, img.data);
}

void sws_t::load_vram(img_descriptor_t &img, int offset_x, int offset_y, int texture) {
  // When only a sub-part of the image must be encoded...
  const bool copy = offset_x || offset_y || img.sd.width != in_width || img.sd.height != in_height;
  if(copy) {
    auto framebuf = gl::frame_buf_t::make(1);
    framebuf.bind(&texture, &texture + 1);

    loaded_texture = tex[0];
    framebuf.copy(0, loaded_texture, offset_x, offset_y, in_width, in_height);
  }
  else {
    loaded_texture = texture;
  }

  if(img.data) {
    GLenum attachment = GL_COLOR_ATTACHMENT0;

    gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, cursor_framebuffer[0]);
    gl::ctx.UseProgram(program[2].handle());

    // When a copy has already been made...
    if(!copy) {
      gl::ctx.BindTexture(GL_TEXTURE_2D, texture);
      gl::ctx.DrawBuffers(1, &attachment);

      gl::ctx.Viewport(0, 0, in_width, in_height);
      gl::ctx.DrawArrays(GL_TRIANGLES, 0, 3);

      loaded_texture = tex[0];
    }

    gl::ctx.BindTexture(GL_TEXTURE_2D, tex[1]);
    if(serial != img.serial) {
      serial = img.serial;

      gl::ctx.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_BGRA, GL_UNSIGNED_BYTE, img.data);
    }

    gl::ctx.Enable(GL_BLEND);

    gl::ctx.DrawBuffers(1, &attachment);

#ifndef NDEBUG
    auto status = gl::ctx.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status != GL_FRAMEBUFFER_COMPLETE) {
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
  gl::ctx.BindTexture(GL_TEXTURE_2D, loaded_texture);

  GLenum attachments[] {
    GL_COLOR_ATTACHMENT0,
    GL_COLOR_ATTACHMENT1
  };

  for(int x = 0; x < sizeof(attachments) / sizeof(decltype(attachments[0])); ++x) {
    gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, fb[x]);
    gl::ctx.DrawBuffers(1, &attachments[x]);

#ifndef NDEBUG
    auto status = gl::ctx.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status != GL_FRAMEBUFFER_COMPLETE) {
      BOOST_LOG(error) << "Pass "sv << x << ": CheckFramebufferStatus() --> [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
#endif

    gl::ctx.UseProgram(program[x].handle());
    gl::ctx.Viewport(offsetX / (x + 1), offsetY / (x + 1), out_width / (x + 1), out_height / (x + 1));
    gl::ctx.DrawArrays(GL_TRIANGLES, 0, 3);
  }

  gl::ctx.BindTexture(GL_TEXTURE_2D, 0);

  gl::ctx.Flush();

  return 0;
}
} // namespace egl

void free_frame(AVFrame *frame) {
  av_frame_free(&frame);
}
