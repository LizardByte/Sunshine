#ifndef SUNSHINE_PLATFORM_LINUX_OPENGL_H
#define SUNSHINE_PLATFORM_LINUX_OPENGL_H

#include <optional>
#include <string_view>

#include <glad/egl.h>
#include <glad/gl.h>

#include "misc.h"
#include "sunshine/main.h"
#include "sunshine/platform/common.h"
#include "sunshine/utility.h"

#define SUNSHINE_STRINGIFY(x) #x
#define gl_drain_errors_helper(x) gl::drain_errors("line " SUNSHINE_STRINGIFY(x))
#define gl_drain_errors gl_drain_errors_helper(__LINE__)

extern "C" int close(int __fd);

struct AVFrame;
void free_frame(AVFrame *frame);

using frame_t = util::safe_ptr<AVFrame, free_frame>;

namespace gl {
extern GladGLContext ctx;
void drain_errors(const std::string_view &prefix);

class tex_t : public util::buffer_t<GLuint> {
  using util::buffer_t<GLuint>::buffer_t;

public:
  tex_t(tex_t &&) = default;
  tex_t &operator=(tex_t &&) = default;

  ~tex_t();

  static tex_t make(std::size_t count);
};

class frame_buf_t : public util::buffer_t<GLuint> {
  using util::buffer_t<GLuint>::buffer_t;

public:
  frame_buf_t(frame_buf_t &&) = default;
  frame_buf_t &operator=(frame_buf_t &&) = default;

  ~frame_buf_t();

  static frame_buf_t make(std::size_t count);

  template<class It>
  void bind(It it_begin, It it_end) {
    using namespace std::literals;
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
  std::string err_str();

  static util::Either<shader_t, std::string> compile(const std::string_view &source, GLenum type);

  GLuint handle() const;

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
  static buffer_t make(util::buffer_t<GLint> &&offsets, const char *block, const std::string_view &data);

  GLuint handle() const;

  const char *block() const;

  void update(const std::string_view &view, std::size_t offset = 0);
  void update(std::string_view *members, std::size_t count, std::size_t offset = 0);

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
  std::string err_str();

  static util::Either<program_t, std::string> link(const shader_t &vert, const shader_t &frag);

  void bind(const buffer_t &buffer);

  std::optional<buffer_t> uniform(const char *block, std::pair<const char *, std::string_view> *members, std::size_t count);

  GLuint handle() const;

private:
  program_internal_t _program;
};
} // namespace gl

namespace gbm {
struct device;
typedef void (*device_destroy_fn)(device *gbm);
typedef device *(*create_device_fn)(int fd);

extern device_destroy_fn device_destroy;
extern create_device_fn create_device;

using gbm_t = util::dyn_safe_ptr<device, &device_destroy>;

int init();

} // namespace gbm

namespace egl {
using display_t = util::dyn_safe_ptr_v2<void, EGLBoolean, &eglTerminate>;

struct rgb_img_t {
  display_t::pointer display;
  EGLImage xrgb8;

  gl::tex_t tex;
};

struct nv12_img_t {
  display_t::pointer display;
  EGLImage r8;
  EGLImage bg88;

  gl::tex_t tex;
  gl::frame_buf_t buf;

  // sizeof(va::DRMPRIMESurfaceDescriptor::objects) / sizeof(va::DRMPRIMESurfaceDescriptor::objects[0]);
  static constexpr std::size_t num_fds = 4;

  std::array<file_t, num_fds> fds;
};

KITTY_USING_MOVE_T(rgb_t, rgb_img_t, , {
  if(el.xrgb8) {
    eglDestroyImage(el.display, el.xrgb8);
  }
});

KITTY_USING_MOVE_T(nv12_t, nv12_img_t, , {
  if(el.r8) {
    eglDestroyImage(el.display, el.r8);
  }

  if(el.bg88) {
    eglDestroyImage(el.display, el.bg88);
  }
});

KITTY_USING_MOVE_T(ctx_t, (std::tuple<display_t::pointer, EGLContext>), , {
  TUPLE_2D_REF(disp, ctx, el);
  if(ctx) {
    eglMakeCurrent(disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(disp, ctx);
  }
});

struct surface_descriptor_t {
  int width;
  int height;
  int fds[4];
  uint32_t fourcc;
  uint64_t modifier;
  uint32_t pitches[4];
  uint32_t offsets[4];
};

display_t make_display(gbm::gbm_t::pointer gbm);
std::optional<ctx_t> make_ctx(display_t::pointer display);

std::optional<rgb_t> import_source(
  display_t::pointer egl_display,
  const surface_descriptor_t &xrgb);

std::optional<nv12_t> import_target(
  display_t::pointer egl_display,
  std::array<file_t, nv12_img_t::num_fds> &&fds,
  const surface_descriptor_t &r8, const surface_descriptor_t &gr88);

class cursor_t : public platf::img_t {
public:
  int x, y;

  unsigned long serial;

  std::vector<std::uint8_t> buffer;
};

class sws_t {
public:
  static std::optional<sws_t> make(int in_width, int in_height, int out_width, int out_heigth, gl::tex_t &&tex);
  static std::optional<sws_t> make(int in_width, int in_height, int out_width, int out_heigth);

  int convert(nv12_t &nv12);

  void load_ram(platf::img_t &img);
  void load_vram(cursor_t &img, int offset_x, int offset_y, int framebuffer);

  void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range);

  // The first texture is the monitor image.
  // The second texture is the cursor image
  gl::tex_t tex;

  // The cursor image will be blended into this framebuffer
  gl::frame_buf_t cursor_framebuffer;

  // Y - shader, UV - shader, Cursor - shader
  gl::program_t program[3];
  gl::buffer_t color_matrix;

  int out_width, out_height;
  int in_width, in_height;
  int offsetX, offsetY;

  // Store latest cursor for load_vram
  std::uint64_t serial;
};

bool fail();
} // namespace egl

#endif
