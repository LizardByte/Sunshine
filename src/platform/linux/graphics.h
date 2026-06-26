/**
 * @file src/platform/linux/graphics.h
 * @brief Declarations for graphics related functions.
 */
#pragma once

// standard includes
#include <optional>
#include <string_view>

// lib includes
#include <glad/egl.h>
#include <glad/gl.h>

// local includes
#include "misc.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"
#include "src/video_colorspace.h"

/**
 * @def SUNSHINE_STRINGIFY_HELPER(x)
 * @brief Macro for SUNSHINE STRINGIFY HELPER.
 */
#define SUNSHINE_STRINGIFY_HELPER(x) #x
/**
 * @def SUNSHINE_STRINGIFY(x)
 * @brief Macro for SUNSHINE STRINGIFY.
 */
#define SUNSHINE_STRINGIFY(x) SUNSHINE_STRINGIFY_HELPER(x)
/**
 * @def gl_drain_errors_helper(x)
 * @brief Macro for gl drain errors helper.
 */
#define gl_drain_errors_helper(x) gl::drain_errors(x)
/**
 * @def gl_drain_errors
 * @brief Macro for gl drain errors.
 */
#define gl_drain_errors gl_drain_errors_helper(__FILE__ ":" SUNSHINE_STRINGIFY(__LINE__))

/**
 * @brief Release the native resource held by the RAII wrapper.
 *
 * @param __fd File descriptor owned by the RAII wrapper.
 * @return 0 on success, or -1 with errno set by the system close call.
 */
extern "C" int close(int __fd);

// X11 Display
extern "C" struct _XDisplay;

struct AVFrame;
/**
 * @brief Release an FFmpeg frame allocated by the capture or conversion backend.
 *
 * @param frame Video or graphics frame being processed.
 */
void free_frame(AVFrame *frame);

/**
 * @brief Owning pointer for an EGL frame object.
 */
using frame_t = util::safe_ptr<AVFrame, free_frame>;

namespace gl {
  extern GladGLContext ctx;

  // glEGLImageTargetTexture2DOES (GL_OES_EGL_image) is not part of desktop GL —
  // it is a GLES extension that must be loaded manually via eglGetProcAddress.
  // GLeglImageOES is typedef void* per the Khronos spec (gl.xml).
  /**
   * @brief Function pointer type for glEGLImageTargetTexture2DOES.
   */
  using PFNGLEGLIMAGETARGETTEXTURE2DOESPROC = void (*)(GLenum target, void *image);
  /**
   * @brief Resolve the GLES extension used to bind EGL images as textures.
   *
   * @return Extension function pointer, or nullptr when the driver does not expose it.
   */
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC egl_image_target_texture_2d();

  /**
   * @brief Drain and log pending OpenGL errors.
   *
   * @param prefix Text prefix used when formatting the message.
   */
  void drain_errors(const std::string_view &prefix);

  /**
   * @brief OpenGL texture handle wrapper.
   */
  class tex_t: public util::buffer_t<GLuint> {
    using util::buffer_t<GLuint>::buffer_t;

  public:
    /**
     * @brief Move ownership of OpenGL texture object names.
     */
    tex_t(tex_t &&) = default;
    /**
     * @brief Assign state from another instance while preserving ownership semantics.
     *
     * @return Reference or value produced by the operator.
     */
    tex_t &operator=(tex_t &&) = default;

    ~tex_t();

    /**
     * @brief Allocate the underlying object and wrap it in the owning handle.
     *
     * @param count Number of objects or handles to create.
     * @return Created backend object, or null when creation fails.
     */
    static tex_t make(std::size_t count);
  };

  /**
   * @brief OpenGL framebuffer handle wrapper.
   */
  class frame_buf_t: public util::buffer_t<GLuint> {
    using util::buffer_t<GLuint>::buffer_t;

  public:
    /**
     * @brief Move ownership of OpenGL framebuffer object names.
     */
    frame_buf_t(frame_buf_t &&) = default;
    /**
     * @brief Assign state from another instance while preserving ownership semantics.
     *
     * @return Reference or value produced by the operator.
     */
    frame_buf_t &operator=(frame_buf_t &&) = default;

    ~frame_buf_t();

    /**
     * @brief Allocate the underlying object and wrap it in the owning handle.
     *
     * @param count Number of objects or handles to create.
     * @return Created backend object, or null when creation fails.
     */
    static frame_buf_t make(std::size_t count);

    /**
     * @brief Bind each framebuffer and clear its color attachment.
     */
    inline void bind(std::nullptr_t, std::nullptr_t) {
      int x = 0;
      for (auto fb : (*this)) {
        ctx.BindFramebuffer(GL_FRAMEBUFFER, fb);
        ctx.FramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + x, 0, 0);

        ++x;
      }
      return;
    }

    /**
     * @brief Bind textures to this object's framebuffers as color attachments.
     *
     * @param it_begin First texture object to attach.
     * @param it_end One-past-the-end iterator for texture objects to attach.
     */
    template<class It>
    void bind(It it_begin, It it_end) {
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
     *
     * @param id Framebuffer index to copy from.
     * @param texture Destination texture receiving the copied pixels.
     * @param offset_x Source X offset in pixels.
     * @param offset_y Source Y offset in pixels.
     * @param width Frame or display width in pixels.
     * @param height Frame or display height in pixels.
     */
    void copy(int id, int texture, int offset_x, int offset_y, int width, int height);
  };

  /**
   * @brief OpenGL shader object that compiles GLSL source.
   */
  class shader_t {
    KITTY_USING_MOVE_T(shader_internal_t, GLuint, std::numeric_limits<GLuint>::max(), {
      if (el != std::numeric_limits<GLuint>::max()) {
        ctx.DeleteShader(el);
      }
    });

  public:
    /**
     * @brief Read the shader compiler log.
     *
     * @return Shader compiler error log.
     */
    std::string err_str();

    /**
     * @brief Compile an OpenGL shader and report compiler errors.
     *
     * @param source Shader source code to compile.
     * @param type OpenGL shader type, such as GL_VERTEX_SHADER or GL_FRAGMENT_SHADER.
     * @return Compiled shader object, or compiler log on failure.
     */
    static util::Either<shader_t, std::string> compile(const std::string_view &source, GLenum type);

    /**
     * @brief Return the native handle owned by the wrapper.
     *
     * @return OpenGL shader object name.
     */
    GLuint handle() const;

  private:
    shader_internal_t _shader;
  };

  /**
   * @brief EGL image buffer with plane descriptors and imported GL textures.
   */
  class buffer_t {
    KITTY_USING_MOVE_T(buffer_internal_t, GLuint, std::numeric_limits<GLuint>::max(), {
      if (el != std::numeric_limits<GLuint>::max()) {
        ctx.DeleteBuffers(1, &el);
      }
    });

  public:
    /**
     * @brief Allocate the underlying object and wrap it in the owning handle.
     *
     * @param offsets Byte offsets for each uniform member in the block.
     * @param block Uniform block name used for diagnostics and updates.
     * @param data Initial bytes copied into the uniform buffer.
     * @return Created backend object, or null when creation fails.
     */
    static buffer_t make(util::buffer_t<GLint> &&offsets, const char *block, const std::string_view &data);

    /**
     * @brief Return the native handle owned by the wrapper.
     *
     * @return OpenGL buffer object name.
     */
    GLuint handle() const;

    /**
     * @brief Query a uniform block index from an OpenGL program.
     *
     * @return Uniform block name associated with the buffer.
     */
    const char *block() const;

    /**
     * @brief Update one uniform member in the block buffer.
     *
     * @param view Raw bytes to copy into the uniform block.
     * @param offset Uniform-member index whose offset is used as the destination.
     */
    void update(const std::string_view &view, std::size_t offset = 0);
    /**
     * @brief Update multiple uniform members in the block buffer.
     *
     * @param members Uniform members to query within the block.
     * @param count Number of members in the array.
     * @param offset First uniform-member index to update.
     */
    void update(std::string_view *members, std::size_t count, std::size_t offset = 0);

  private:
    const char *_block;

    std::size_t _size;

    util::buffer_t<GLint> _offsets;

    buffer_internal_t _buffer;
  };

  /**
   * @brief OpenGL shader program with attached shader stages.
   */
  class program_t {
    KITTY_USING_MOVE_T(program_internal_t, GLuint, std::numeric_limits<GLuint>::max(), {
      if (el != std::numeric_limits<GLuint>::max()) {
        ctx.DeleteProgram(el);
      }
    });

  public:
    /**
     * @brief Read the program linker log.
     *
     * @return OpenGL program link error log.
     */
    std::string err_str();

    /**
     * @brief Link an OpenGL program from compiled shaders.
     *
     * @param vert Compiled vertex shader object.
     * @param frag Compiled fragment shader object.
     * @return Linked program object, or linker log on failure.
     */
    static util::Either<program_t, std::string> link(const shader_t &vert, const shader_t &frag);

    /**
     * @brief Bind this program and attach a uniform buffer block.
     *
     * @param buffer Uniform buffer block used by the program.
     */
    void bind(const buffer_t &buffer);

    /**
     * @brief Query a uniform location from an OpenGL program.
     *
     * @param block Uniform block name to bind.
     * @param members Uniform members to query within the block.
     * @param count Number of uniform members to resolve.
     * @return Uniform buffer wrapper, or std::nullopt when lookup/allocation fails.
     */
    std::optional<buffer_t> uniform(const char *block, std::pair<const char *, std::string_view> *members, std::size_t count);

    /**
     * @brief Return the native handle owned by the wrapper.
     *
     * @return OpenGL program object name.
     */
    GLuint handle() const;

  private:
    program_internal_t _program;
  };
}  // namespace gl

namespace gbm {
  struct device;
  /**
   * @brief Function pointer used to destroy a GBM device.
   */
  typedef void (*device_destroy_fn)(device *gbm);
  /**
   * @brief Function pointer used to create a GBM device from a file descriptor.
   */
  typedef device *(*create_device_fn)(int fd);

  extern device_destroy_fn device_destroy;
  extern create_device_fn create_device;

  /**
   * @brief Owning GBM device pointer released with the GBM destroy callback.
   */
  using gbm_t = util::dyn_safe_ptr<device, &device_destroy>;

  /**
   * @brief Load GBM symbols required for EGL device creation.
   *
   * @return 0 on success; nonzero or negative platform status on failure.
   */
  int init();

}  // namespace gbm

namespace egl {
  /**
   * @brief Owning pointer for an EGL display connection.
   */
  using display_t = util::dyn_safe_ptr_v2<void, EGLBoolean, &eglTerminate>;

  /**
   * @brief RGB capture image backed by EGL and OpenGL resources.
   */
  struct rgb_img_t {
    display_t::pointer display;  ///< EGL display that owns the imported image.
    EGLImage xrgb8;  ///< EGL image for the imported XRGB plane.

    gl::tex_t tex;  ///< Texture containing the imported RGB plane.
  };

  /**
   * @brief NV12 capture image backed by EGL and OpenGL resources.
   */
  struct nv12_img_t {
    display_t::pointer display;  ///< EGL display that owns the imported planes.
    EGLImage r8;  ///< EGL image for the NV12 luma plane.
    EGLImage bg88;  ///< EGL image for the NV12 interleaved chroma plane.

    gl::tex_t tex;  ///< Textures containing the imported Y and UV planes.
    gl::frame_buf_t buf;  ///< OpenGL framebuffer object used for rendering.

    // sizeof(va::DRMPRIMESurfaceDescriptor::objects) / sizeof(va::DRMPRIMESurfaceDescriptor::objects[0]);
    static constexpr std::size_t num_fds = 4;  ///< Maximum number of DMA-BUF plane file descriptors exported by VAAPI.

    std::array<file_t, num_fds> fds;  ///< DMA-BUF file descriptors for each exported plane.
  };

  /**
   * @brief YUV 4:4:4 capture image backed by EGL and OpenGL resources.
   */
  struct yuv444_img_t {
    display_t::pointer display;  ///< EGL display that owns the imported planes.
    EGLImage r8;  ///< EGL image for the Y plane.
    EGLImage g8;  ///< EGL image for the U plane.
    EGLImage b8;  ///< EGL image for the V plane.

    gl::tex_t tex;  ///< Textures containing the imported Y, U, and V planes.
    gl::frame_buf_t buf;  ///< OpenGL framebuffer object used for rendering.

    static constexpr std::size_t num_fds = 4;  ///< Num fds.

    std::array<file_t, num_fds> fds;  ///< DMA-BUF file descriptors for each exported plane.
  };

#ifndef DOXYGEN
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

  KITTY_USING_MOVE_T(yuv444_t, yuv444_img_t, , {
    if (el.r8) {
      eglDestroyImage(el.display, el.r8);
    }

    if (el.g8) {
      eglDestroyImage(el.display, el.g8);
    }

    if (el.b8) {
      eglDestroyImage(el.display, el.b8);
    }
  });

  KITTY_USING_MOVE_T(ctx_t, (std::tuple<display_t::pointer, EGLContext>), , {
    TUPLE_2D_REF(disp, ctx, el);
    if (ctx) {
      eglMakeCurrent(disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      eglDestroyContext(disp, ctx);
    }
  });
#else
  /**
   * @brief Move-only wrapper for RGB EGL image resources.
   */
  class rgb_t;

  /**
   * @brief Move-only wrapper for NV12 EGL image resources.
   */
  class nv12_t;

  /**
   * @brief Move-only wrapper for YUV444 EGL image resources.
   */
  class yuv444_t;

  /**
   * @brief Move-only wrapper for an EGL context.
   */
  class ctx_t;
#endif

  /**
   * @brief EGL surface descriptor used to import a captured DMA-BUF.
   */
  struct surface_descriptor_t {
    int width;  ///< Frame or display width in pixels.
    int height;  ///< Frame or display height in pixels.
    int fds[4];  ///< DMA-BUF file descriptors for up to four planes.
    std::uint32_t fourcc;  ///< DRM fourcc pixel format for the buffer.
    std::uint64_t modifier;  ///< DRM format modifier describing the buffer layout.
    std::uint32_t pitches[4];  ///< Row stride in bytes for each DMA-BUF plane.
    std::uint32_t offsets[4];  ///< Byte offset to the first pixel for each DMA-BUF plane.
  };

  /**
   * @brief Open and initialize the display connection used for capture.
   *
   * @param native_display Native display.
   * @return Constructed display object.
   */
  display_t make_display(std::variant<gbm::gbm_t::pointer, wl_display *, _XDisplay *> native_display);
  /**
   * @brief Create an EGL/OpenGL context for capture or conversion.
   *
   * @param display Display object or identifier associated with the operation.
   * @return EGL context wrapper, or std::nullopt when context creation fails.
   */
  std::optional<ctx_t> make_ctx(display_t::pointer display);

  /**
   * @brief Import an RGB source surface.
   *
   * @return Imported RGB image, or std::nullopt on failure.
   */
  std::optional<rgb_t> import_source(display_t::pointer egl_display, const surface_descriptor_t &xrgb);

  /**
   * @brief Create a blank RGB texture for an image.
   *
   * @return Blank RGB image.
   */
  rgb_t create_blank(platf::img_t &img);

  /**
   * @brief Import an NV12 target surface.
   *
   * @return Imported NV12 image, or std::nullopt on failure.
   */
  std::optional<nv12_t> import_target(
    display_t::pointer egl_display,
    std::array<file_t, nv12_img_t::num_fds> &&fds,
    const surface_descriptor_t &y,
    const surface_descriptor_t &uv
  );

  /**
   * @brief Import a YUV444 target surface.
   *
   * @param egl_display EGL display.
   * @param fds Target plane file descriptors.
   * @param y Y plane descriptor.
   * @param u U plane descriptor.
   * @param v V plane descriptor.
   * @return Imported YUV444 image, or std::nullopt on failure.
   */
  std::optional<yuv444_t> import_target(
    display_t::pointer egl_display,
    std::array<file_t, yuv444_img_t::num_fds> &&fds,
    const surface_descriptor_t &y,
    const surface_descriptor_t &u,
    const surface_descriptor_t &v
  );

  /**
   * @brief Creates biplanar YUV textures to render into.
   * @param width Width of the target frame.
   * @param height Height of the target frame.
   * @param format Format of the target frame.
   * @return The new RGB texture.
   */
  std::optional<nv12_t> create_nv12_target(int width, int height, AVPixelFormat format);

  /**
   * @brief Create YUV444 target.
   *
   * @param width Frame or display width in pixels.
   * @param height Frame or display height in pixels.
   * @param format Pixel, audio, or protocol format being converted.
   * @return Created YUV444 target object or status.
   */
  std::optional<yuv444_t> create_yuv444_target(int width, int height, AVPixelFormat format);

  /**
   * @brief Cursor image and hotspot metadata captured from the window system.
   */
  class cursor_t: public platf::img_t {
  public:
    int x;  ///< Cursor hotspot or surface X coordinate.
    int y;  ///< Cursor hotspot or surface Y coordinate.
    int src_w;  ///< Cursor source image width in pixels.
    int src_h;  ///< Cursor source image height in pixels.

    unsigned long serial;  ///< X11 cursor serial used to detect cursor image changes.

    std::vector<std::uint8_t> buffer;  ///< Cursor image pixels.
  };

  // Allow cursor and the underlying image to be kept together
  /**
   * @brief Captured image descriptor shared by EGL conversion paths.
   */
  class img_descriptor_t: public cursor_t {
  public:
    ~img_descriptor_t() {
      reset();
    }

    /**
     * @brief Reset the object to its initial empty state.
     */
    void reset() {
      for (auto x = 0; x < 4; ++x) {
        if (sd.fds[x] >= 0) {
          close(sd.fds[x]);

          sd.fds[x] = -1;
        }
      }
    }

    surface_descriptor_t sd;  ///< DMA-BUF surface descriptor for the captured image.

    // Increment sequence when new rgb_t needs to be created
    std::uint64_t sequence;  ///< Monotonic value used to detect when GL resources must be recreated.

    // Frame is vertically flipped (GL convention)
    bool y_invert {false};  ///< Whether the shader should invert the Y axis.

    // PipeWire metadata
    std::optional<uint64_t> pts;  ///< PipeWire presentation timestamp.
    std::optional<uint64_t> seq;  ///< PipeWire frame sequence number.
    std::optional<bool> pw_damage;  ///< Whether PipeWire damage tracking should be used.
    std::optional<uint32_t> pw_flags;  ///< PipeWire frame flags reported with the buffer.
  };

  /**
   * @brief EGL/OpenGL scaler and colorspace conversion pipeline.
   */
  class sws_t {
  public:
    /**
     * @brief Create a software-scaling pipeline that renders to NV12 planes.
     *
     * @param in_width Source frame width in pixels.
     * @param in_height Source frame height in pixels.
     * @param out_width Destination frame width in pixels.
     * @param out_height Destination frame height in pixels.
     * @param tex Texture resource used by the converter.
     * @return Constructed NV12 object.
     */
    static std::optional<sws_t> make_nv12(int in_width, int in_height, int out_width, int out_height, gl::tex_t &&tex);
    /**
     * @brief Create a software-scaling pipeline that renders to YUV444 planes.
     *
     * @param in_width Source frame width in pixels.
     * @param in_height Source frame height in pixels.
     * @param out_width Destination frame width in pixels.
     * @param out_height Destination frame height in pixels.
     * @param tex Texture resource used by the converter.
     * @return Constructed YUV444 object.
     */
    static std::optional<sws_t> make_yuv444(int in_width, int in_height, int out_width, int out_height, gl::tex_t &&tex);

    /**
     * @brief Allocate the underlying object and wrap it in the owning handle.
     *
     * @param in_width Source frame width in pixels.
     * @param in_height Source frame height in pixels.
     * @param out_width Destination frame width in pixels.
     * @param out_height Destination frame height in pixels.
     * @param format Destination FFmpeg pixel format.
     * @param is_yuv444 Whether the destination uses three full-resolution planes.
     * @return Created backend object, or null when creation fails.
     */
    static std::optional<sws_t> make(int in_width, int in_height, int out_width, int out_height, AVPixelFormat format, bool is_yuv444);

    // Convert the loaded image into the first two framebuffers
    /**
     * @brief Convert the loaded source image into NV12 output planes.
     *
     * @param fb Framebuffer object to bind or update.
     * @return Conversion status.
     */
    int convert_nv12(gl::frame_buf_t &fb);

    // Convert the loaded image into the first three framebuffers
    /**
     * @brief Convert the loaded source image into YUV444 output planes.
     *
     * @param fb Framebuffer object to bind or update.
     * @return Conversion status.
     */
    int convert_yuv444(gl::frame_buf_t &fb);

    // Draw loaded image by programs to frame buffers
    /**
     * @brief Render the loaded source texture into output framebuffers.
     *
     * @param attachments Framebuffer attachments to bind.
     * @param fb Framebuffer object to bind or update.
     * @param count Number of output planes to draw.
     * @param is_yuv444 Whether to render three YUV444 planes instead of NV12 planes.
     * @return 0 when all draw calls complete; nonzero on OpenGL failure.
     */
    int draw_programs_to_buffers(GLenum attachments[], gl::frame_buf_t &fb, int count, bool is_yuv444);

    // Make an area of the image black
    /**
     * @brief Clear the render target to a blank frame.
     *
     * @param fb Framebuffer object to bind or update.
     * @param offsetX_ Offset x.
     * @param offsetY_ Offset y.
     * @param width Frame or display width in pixels.
     * @param height Frame or display height in pixels.
     * @param is_yuv444 Is YUV444.
     * @return 0 when the target area is cleared; nonzero on OpenGL failure.
     */
    int blank(gl::frame_buf_t &fb, int offsetX_, int offsetY_, int width, int height, bool is_yuv444);

    /**
     * @brief Load ram data from the backing API or store.
     *
     * @param img Image or frame object to read from or populate.
     */
    void load_ram(platf::img_t &img);
    /**
     * @brief Load vram data from the backing API or store.
     *
     * @param img Image or frame object to read from or populate.
     * @param offset_x Offset x.
     * @param offset_y Offset y.
     * @param texture Texture resource to bind, update, or attach.
     * @param is_yuv444 Is YUV444.
     */
    void load_vram(img_descriptor_t &img, int offset_x, int offset_y, int texture, bool is_yuv444);

    /**
     * @brief Apply the configured colorspace metadata to the active frame.
     *
     * @param colorspace Colorimetry information used for conversion or encoding.
     * @param is_yuv444 Is YUV444.
     */
    void apply_colorspace(const video::sunshine_colorspace_t &colorspace, bool is_yuv444);

    // The first texture is the monitor image.
    // The second texture is the cursor image
    gl::tex_t tex;  ///< Source and cursor textures used by the conversion pipeline.

    // The cursor image will be blended into this framebuffer
    gl::frame_buf_t cursor_framebuffer;  ///< Cursor framebuffer.
    gl::frame_buf_t copy_framebuffer;  ///< Copy framebuffer.

    // Y - shader, UV - shader, Cursor - shader : for nv12
    // Y - shader, U - shader, V - shader, Cursor - shader : for yuv444
    std::array<gl::program_t, 4> program;  ///< Program.
    gl::buffer_t color_matrix;  ///< Color matrix.

    int out_width;  ///< Out width.
    int out_height;  ///< Out height.
    int in_width;  ///< In width.
    int in_height;  ///< In height.
    int offsetX;  ///< Offset x.
    int offsetY;  ///< Offset y.

    // Pointer to the texture to be converted to nv12
    int loaded_texture;  ///< Loaded texture.

    // Store latest cursor for load_vram
    std::uint64_t serial;  ///< Serial.
  };

  /**
   * @brief Log EGL failure details and return an error code.
   *
   * @return False after logging the EGL failure.
   */
  bool fail();
}  // namespace egl
