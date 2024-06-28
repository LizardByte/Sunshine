/**
 * @file src/platform/linux/graphics.h
 * @brief Declarations for graphics related functions.
 */
#pragma once

#include <optional>
#include <string_view>

#include <glad/egl.h>
#include <glad/gl.h>

#include "misc.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"
#include "src/video_colorspace.h"

#define SUNSHINE_STRINGIFY_HELPER(x) #x
#define SUNSHINE_STRINGIFY(x) SUNSHINE_STRINGIFY_HELPER(x)
#define gl_drain_errors_helper(x) gl::drain_errors(x)
#define gl_drain_errors gl_drain_errors_helper(__FILE__ ":" SUNSHINE_STRINGIFY(__LINE__))

extern "C" int
close(int __fd);

// X11 Display
extern "C" struct _XDisplay;

struct AVFrame;
void
free_frame(AVFrame *frame);

using frame_t = util::safe_ptr<AVFrame, free_frame>;

namespace gl {
  extern GladGLContext ctx;
  void
  drain_errors(const std::string_view &prefix);

  class tex_t: public util::buffer_t<GLuint> {
    using util::buffer_t<GLuint>::buffer_t;

  public:
    tex_t(tex_t &&) = default;
    tex_t &
    operator=(tex_t &&) = default;

    ~tex_t();

    static tex_t
    make(std::size_t count);
  };

  class frame_buf_t: public util::buffer_t<GLuint> {
    using util::buffer_t<GLuint>::buffer_t;

  public:
    frame_buf_t(frame_buf_t &&) = default;
    frame_buf_t &
    operator=(frame_buf_t &&) = default;

    ~frame_buf_t();

    static frame_buf_t
    make(std::size_t count);

    inline void
    bind(std::nullptr_t, std::nullptr_t) {
      int x = 0;
      for (auto fb : (*this)) {
        ctx.BindFramebuffer(GL_FRAMEBUFFER, fb);
        ctx.FramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + x, 0, 0);

        ++x;
      }
      return;
    }

    template <class It>
    void
    bind(It it_begin, It it_end) {
      using namespace std::literals;
      if (std::distance(it_begin, it_end) > size()) {
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

    /**
     * Copies a part of the framebuffer to texture
     */
    void
    copy(int id, int texture, int offset_x, int offset_y, int width, int height);
  };

  class shader_t {
    KITTY_USING_MOVE_T(shader_internal_t, GLuint, std::numeric_limits<GLuint>::max(), {
      if (el != std::numeric_limits<GLuint>::max()) {
        ctx.DeleteShader(el);
      }
    });

  public:
    std::string
    err_str();

    static util::Either<shader_t, std::string>
    compile(const std::string_view &source, GLenum type);

    GLuint
    handle() const;

  private:
    shader_internal_t _shader;
  };

  class buffer_t {
    KITTY_USING_MOVE_T(buffer_internal_t, GLuint, std::numeric_limits<GLuint>::max(), {
      if (el != std::numeric_limits<GLuint>::max()) {
        ctx.DeleteBuffers(1, &el);
      }
    });

  public:
    static buffer_t
    make(util::buffer_t<GLint> &&offsets, const char *block, const std::string_view &data);

    GLuint
    handle() const;

    const char *
    block() const;

    void
    update(const std::string_view &view, std::size_t offset = 0);
    void
    update(std::string_view *members, std::size_t count, std::size_t offset = 0);

  private:
    const char *_block;

    std::size_t _size;

    util::buffer_t<GLint> _offsets;

    buffer_internal_t _buffer;
  };

  class program_t {
    KITTY_USING_MOVE_T(program_internal_t, GLuint, std::numeric_limits<GLuint>::max(), {
      if (el != std::numeric_limits<GLuint>::max()) {
        ctx.DeleteProgram(el);
      }
    });

  public:
    std::string
    err_str();

    static util::Either<program_t, std::string>
    link(const shader_t &vert, const shader_t &frag);

    void
    bind(const buffer_t &buffer);

    std::optional<buffer_t>
    uniform(const char *block, std::pair<const char *, std::string_view> *members, std::size_t count);

    GLuint
    handle() const;

  private:
    program_internal_t _program;
  };
}  // namespace gl

namespace gbm {
  struct device;
  typedef void (*device_destroy_fn)(device *gbm);
  typedef device *(*create_device_fn)(int fd);

  extern device_destroy_fn device_destroy;
  extern create_device_fn create_device;

  using gbm_t = util::dyn_safe_ptr<device, &device_destroy>;

  int
  init();

}  // namespace gbm

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
    if (el.xrgb8) {
      eglDestroyImage(el.display, el.xrgb8);
    }
  });

  KITTY_USING_MOVE_T(nv12_t, nv12_img_t, , {
    if (el.r8) {
      eglDestroyImage(el.display, el.r8);
    }

    if (el.bg88) {
      eglDestroyImage(el.display, el.bg88);
    }
  });

  KITTY_USING_MOVE_T(ctx_t, (std::tuple<display_t::pointer, EGLContext>), , {
    TUPLE_2D_REF(disp, ctx, el);
    if (ctx) {
      eglMakeCurrent(disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      eglDestroyContext(disp, ctx);
    }
  });

  struct surface_descriptor_t {
    int width;
    int height;
    int fds[4];
    std::uint32_t fourcc;
    std::uint64_t modifier;
    std::uint32_t pitches[4];
    std::uint32_t offsets[4];
  };

  display_t
  make_display(std::variant<gbm::gbm_t::pointer, wl_display *, _XDisplay *> native_display);
  std::optional<ctx_t>
  make_ctx(display_t::pointer display);

  std::optional<rgb_t>
  import_source(
    display_t::pointer egl_display,
    const surface_descriptor_t &xrgb);

  rgb_t
  create_blank(platf::img_t &img);

  std::optional<nv12_t>
  import_target(
    display_t::pointer egl_display,
    std::array<file_t, nv12_img_t::num_fds> &&fds,
    const surface_descriptor_t &y, const surface_descriptor_t &uv);

  /**
   * @brief Creates biplanar YUV textures to render into.
   * @param width Width of the target frame.
   * @param height Height of the target frame.
   * @param format Format of the target frame.
   * @return The new RGB texture.
   */
  std::optional<nv12_t>
  create_target(int width, int height, AVPixelFormat format);

  class cursor_t: public platf::img_t {
  public:
    int x, y;
    int src_w, src_h;

    unsigned long serial;

    std::vector<std::uint8_t> buffer;
  };

  // Allow cursor and the underlying image to be kept together
  class img_descriptor_t: public cursor_t {
  public:
    ~img_descriptor_t() {
      reset();
    }

    void
    reset() {
      for (auto x = 0; x < 4; ++x) {
        if (sd.fds[x] >= 0) {
          close(sd.fds[x]);

          sd.fds[x] = -1;
        }
      }
    }

    surface_descriptor_t sd;

    // Increment sequence when new rgb_t needs to be created
    std::uint64_t sequence;
  };

  class sws_t {
  public:
    static std::optional<sws_t>
    make(int in_width, int in_height, int out_width, int out_height, gl::tex_t &&tex);
    static std::optional<sws_t>
    make(int in_width, int in_height, int out_width, int out_height, AVPixelFormat format);

    // Convert the loaded image into the first two framebuffers
    int
    convert(gl::frame_buf_t &fb);

    // Make an area of the image black
    int
    blank(gl::frame_buf_t &fb, int offsetX, int offsetY, int width, int height);

    void
    load_ram(platf::img_t &img);
    void
    load_vram(img_descriptor_t &img, int offset_x, int offset_y, int texture);

    void
    apply_colorspace(const video::sunshine_colorspace_t &colorspace);

    // The first texture is the monitor image.
    // The second texture is the cursor image
    gl::tex_t tex;

    // The cursor image will be blended into this framebuffer
    gl::frame_buf_t cursor_framebuffer;
    gl::frame_buf_t copy_framebuffer;

    // Y - shader, UV - shader, Cursor - shader
    gl::program_t program[3];
    gl::buffer_t color_matrix;

    int out_width, out_height;
    int in_width, in_height;
    int offsetX, offsetY;

    // Pointer to the texture to be converted to nv12
    int loaded_texture;

    // Store latest cursor for load_vram
    std::uint64_t serial;
  };

  bool
  fail();
}  // namespace egl
