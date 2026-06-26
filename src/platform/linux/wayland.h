/**
 * @file src/platform/linux/wayland.h
 * @brief Declarations for Wayland capture.
 */
#pragma once

// standard includes
#include <bitset>
#include <cstdint>
#include <map>
#include <vector>

#ifdef SUNSHINE_BUILD_WAYLAND
  #include <linux-dmabuf-unstable-v1.h>
  #include <wlr-screencopy-unstable-v1.h>
  #include <xdg-output-unstable-v1.h>
#endif

// local includes
#include "graphics.h"

/**
 * The classes defined in this macro block should only be used by
 * cpp files whose compilation depends on SUNSHINE_BUILD_WAYLAND
 */
#ifdef SUNSHINE_BUILD_WAYLAND

namespace wl {
  /**
   * @brief Owning pointer for a Wayland display connection.
   */
  using display_internal_t = util::safe_ptr<wl_display, wl_display_disconnect>;

  /**
   * @brief Captured Wayland frame metadata and DMA-BUF surface state.
   */
  class frame_t {
  public:
    frame_t();
    /**
     * @brief Release file descriptors and native buffers associated with this frame.
     */
    void destroy();

    egl::surface_descriptor_t sd;  ///< DMA-BUF surface descriptor received from the compositor.
    std::optional<std::chrono::steady_clock::time_point> frame_timestamp;  ///< Capture timestamp associated with the frame.
  };

  /**
   * @brief Listener state for Wayland screencopy frames backed by DMA-BUFs.
   */
  class dmabuf_t {
  public:
    /**
     * @brief Capture state for the active screencopy request.
     */
    enum status_e {
      WAITING,  ///< Waiting for a frame
      READY,  ///< Frame is ready
      REINIT,  ///< Reinitialize the frame
    };

    dmabuf_t();
    ~dmabuf_t();

    dmabuf_t(dmabuf_t &&) = delete;
    dmabuf_t(const dmabuf_t &) = delete;
    dmabuf_t &operator=(const dmabuf_t &) = delete;
    dmabuf_t &operator=(dmabuf_t &&) = delete;

    /**
     * @brief Start a screencopy request and attach the DMA-BUF listener callbacks.
     *
     * @param screencopy_manager Compositor screencopy manager used to request frames.
     * @param dmabuf_interface Compositor DMA-BUF interface used to allocate buffers.
     * @param supported_modifiers DMA-BUF format modifiers supported by the compositor.
     * @param output Wayland output to capture.
     * @param blend_cursor Whether the compositor should include the cursor in the frame.
     */
    void listen(zwlr_screencopy_manager_v1 *screencopy_manager, zwp_linux_dmabuf_v1 *dmabuf_interface, const std::map<std::uint32_t, std::vector<std::uint64_t>> *supported_modifiers, wl_output *output, bool blend_cursor = false);
    /**
     * @brief Store the Wayland buffer created for a DMA-BUF parameter request.
     *
     * @param data Pointer to the `dmabuf_t` instance associated with the callback.
     * @param params DMA-BUF parameter object that completed buffer creation.
     * @param wl_buffer Wayland buffer created from the DMA-BUF planes.
     */
    static void buffer_params_created(void *data, struct zwp_linux_buffer_params_v1 *params, struct wl_buffer *wl_buffer);
    /**
     * @brief Mark DMA-BUF buffer creation as failed.
     *
     * @param data Pointer to the `dmabuf_t` instance associated with the callback.
     * @param params DMA-BUF parameter object that failed buffer creation.
     */
    static void buffer_params_failed(void *data, struct zwp_linux_buffer_params_v1 *params);
    /**
     * @brief Record DMA-BUF buffer parameters from the compositor.
     *
     * @param frame Screencopy frame that advertised shared-memory fallback data.
     * @param format DRM pixel format for the shared-memory buffer.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param stride Number of bytes per row in the buffer.
     */
    void buffer(zwlr_screencopy_frame_v1 *frame, std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t stride);
    /**
     * @brief Record Linux DMA-BUF parameters advertised for the frame.
     *
     * @param frame Screencopy frame that advertised DMA-BUF data.
     * @param format DRM pixel format for the DMA-BUF.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     */
    void linux_dmabuf(zwlr_screencopy_frame_v1 *frame, std::uint32_t format, std::uint32_t width, std::uint32_t height);
    /**
     * @brief Allocate or import the buffer once the compositor finished describing it.
     *
     * @param frame Screencopy frame whose buffer description is complete.
     */
    void buffer_done(zwlr_screencopy_frame_v1 *frame);
    /**
     * @brief Record frame flags reported by the compositor.
     *
     * @param frame Screencopy frame that reported flags.
     * @param flags Wayland screencopy flags such as vertical inversion.
     */
    void flags(zwlr_screencopy_frame_v1 *frame, std::uint32_t flags);
    /**
     * @brief Record damaged frame bounds reported by the compositor.
     *
     * @param frame Screencopy frame that reported damage.
     * @param x Left edge of the damaged rectangle.
     * @param y Top edge of the damaged rectangle.
     * @param width Damaged rectangle width in pixels.
     * @param height Damaged rectangle height in pixels.
     */
    void damage(zwlr_screencopy_frame_v1 *frame, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height);
    /**
     * @brief Mark the current screencopy frame as ready and store its timestamp.
     *
     * @param frame Screencopy frame that completed.
     * @param tv_sec_hi High 32 bits of the compositor-provided seconds value.
     * @param tv_sec_lo Low 32 bits of the compositor-provided seconds value.
     * @param tv_nsec Nanosecond component of the compositor-provided timestamp.
     */
    void ready(zwlr_screencopy_frame_v1 *frame, std::uint32_t tv_sec_hi, std::uint32_t tv_sec_lo, std::uint32_t tv_nsec);
    /**
     * @brief Mark the frame capture as failed.
     *
     * @param frame Screencopy frame that failed.
     */
    void failed(zwlr_screencopy_frame_v1 *frame);

    /**
     * @brief Select the inactive frame slot for the next screencopy request.
     *
     * @return Inactive frame buffer that can receive the next capture.
     */
    frame_t *get_next_frame() {
      return current_frame == &frames[0] ? &frames[1] : &frames[0];
    }

    status_e status;  ///< Current state of the active screencopy request.
    std::array<frame_t, 2> frames;  ///< Double-buffered frame descriptors.
    frame_t *current_frame;  ///< Frame descriptor currently being filled by the compositor.
    zwlr_screencopy_frame_v1_listener listener;  ///< Callback table registered on screencopy frames.

  private:
    bool init_gbm();
    void cleanup_gbm();
    void create_and_copy_dmabuf(zwlr_screencopy_frame_v1 *frame);

    zwp_linux_dmabuf_v1 *dmabuf_interface {nullptr};
    const std::map<std::uint32_t, std::vector<std::uint64_t>> *supported_modifiers {nullptr};

    struct {
      bool supported {false};
      std::uint32_t format;
      std::uint32_t width;
      std::uint32_t height;
      std::uint32_t stride;
    } shm_info;

    struct {
      bool supported {false};
      std::uint32_t format;
      std::uint32_t width;
      std::uint32_t height;
    } dmabuf_info;

    struct gbm_device *gbm_device {nullptr};
    struct gbm_bo *current_bo {nullptr};
    struct wl_buffer *current_wl_buffer {nullptr};
    bool y_invert {false};
  };

  /**
   * @brief Wayland output metadata used to match a configured display name.
   */
  class monitor_t {
  public:
    /**
     * @brief Track a Wayland output and its advertised modes.
     *
     * @param output Wayland output object being described.
     */
    explicit monitor_t(wl_output *output);

    monitor_t(monitor_t &&) = delete;
    monitor_t(const monitor_t &) = delete;
    monitor_t &operator=(const monitor_t &) = delete;
    monitor_t &operator=(monitor_t &&) = delete;

    /**
     * @brief Attach xdg-output and wl-output listeners for this monitor.
     *
     * @param output_manager xdg-output manager used to query logical monitor metadata.
     */
    void listen(zxdg_output_manager_v1 *output_manager);
    /**
     * @brief Store the xdg-output logical monitor name.
     *
     * @param name Human-readable name to assign.
     */
    void xdg_name(zxdg_output_v1 *, const char *name);
    /**
     * @brief Store the xdg-output human-readable monitor description.
     *
     * @param description Human-readable description used in log output.
     */
    void xdg_description(zxdg_output_v1 *, const char *description);
    /**
     * @brief Store the logical monitor position reported by xdg-output.
     *
     * @param x Horizontal coordinate reported by Wayland.
     * @param y Vertical coordinate reported by Wayland.
     */
    void xdg_position(zxdg_output_v1 *, std::int32_t x, std::int32_t y);
    /**
     * @brief Store the logical monitor size reported by xdg-output.
     *
     * @param width Frame or display width in pixels.
     * @param height Frame or display height in pixels.
     */
    void xdg_size(zxdg_output_v1 *, std::int32_t width, std::int32_t height);

    /**
     * @brief Acknowledge completion of an xdg-output metadata update.
     */
    void xdg_done(zxdg_output_v1 *) {}

    /**
     * @brief Receive physical wl-output geometry metadata.
     *
     * @param wl_output Wayland output.
     * @param x Horizontal coordinate reported by Wayland.
     * @param y Vertical coordinate reported by Wayland.
     * @param physical_width Physical width in millimeters.
     * @param physical_height Physical height in millimeters.
     * @param subpixel Wayland subpixel layout enum.
     * @param make Monitor manufacturer string reported by Wayland.
     * @param model Monitor model string reported by Wayland.
     * @param transform Wayland output transform value.
     */
    void wl_geometry(wl_output *wl_output, std::int32_t x, std::int32_t y, std::int32_t physical_width, std::int32_t physical_height, std::int32_t subpixel, const char *make, const char *model, std::int32_t transform) {}

    /**
     * @brief Store the active pixel mode reported by wl-output.
     *
     * @param wl_output Wayland output.
     * @param flags Bit flags that modify the requested operation.
     * @param width Frame or display width in pixels.
     * @param height Frame or display height in pixels.
     * @param refresh Output refresh rate in mHz.
     */
    void wl_mode(wl_output *wl_output, std::uint32_t flags, std::int32_t width, std::int32_t height, std::int32_t refresh);

    /**
     * @brief Acknowledge completion of a wl-output metadata update.
     *
     * @param wl_output Wayland output.
     */
    void wl_done(wl_output *wl_output) {}

    /**
     * @brief Receive the wl-output scale factor.
     *
     * @param wl_output Wayland output.
     * @param factor Wayland output scale factor.
     */
    void wl_scale(wl_output *wl_output, std::int32_t factor) {}

    wl_output *output;  ///< Wayland output associated with this monitor.
    std::string name;  ///< xdg-output name used for display selection.
    std::string description;  ///< xdg-output description used for logs and UI.
    platf::touch_port_t viewport;  ///< Logical monitor bounds used to scale absolute input.
    wl_output_listener wl_listener;  ///< Callback table for wl-output events.
    zxdg_output_v1_listener xdg_listener;  ///< Callback table for xdg-output events.
  };

  /**
   * @brief Wayland registry state for screencopy, DMA-BUF, and output globals.
   */
  class interface_t {
    struct bind_t {
      std::uint32_t id;
      std::uint32_t version;
    };

  public:
    /**
     * @brief Wayland globals required by the WLR capture backend.
     */
    enum interface_e {
      XDG_OUTPUT,  ///< xdg-output
      WLR_EXPORT_DMABUF,  ///< screencopy manager
      LINUX_DMABUF,  ///< linux-dmabuf protocol
      MAX_INTERFACES,  ///< Maximum number of interfaces
    };

    interface_t() noexcept;

    interface_t(interface_t &&) = delete;
    interface_t(const interface_t &) = delete;
    interface_t &operator=(const interface_t &) = delete;
    interface_t &operator=(interface_t &&) = delete;

    /**
     * @brief Discover compositor globals from the Wayland registry.
     *
     * @param registry Wayland registry announcing globals.
     */
    void listen(wl_registry *registry);

    /**
     * @brief Test whether a required Wayland global was advertised.
     *
     * @param bit Interface bit to test.
     * @return True when the requested Wayland interface bit is set.
     */
    bool operator[](interface_e bit) const {
      return interface[bit];
    }

    /**
     * @brief Record a DMA-BUF format advertised by the compositor.
     *
     * @param zwp_linux_dmabuf DMA-BUF interface that emitted the format event.
     * @param format DRM format supported by the compositor.
     */
    void dmabuf_format(zwp_linux_dmabuf_v1 *zwp_linux_dmabuf, uint32_t format);
    /**
     * @brief Record a DMA-BUF format modifier advertised by the compositor.
     *
     * @param zwp_linux_dmabuf DMA-BUF interface that emitted the modifier event.
     * @param format DRM format associated with the modifier.
     * @param modifier_hi High 32 bits of the DRM format modifier.
     * @param modifier_lo Low 32 bits of the DRM format modifier.
     */
    void dmabuf_modifier(zwp_linux_dmabuf_v1 *zwp_linux_dmabuf, uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo);

    std::vector<std::unique_ptr<monitor_t>> monitors;  ///< Outputs discovered from the Wayland registry.
    std::map<std::uint32_t, std::vector<std::uint64_t>> supported_modifiers;  ///< DRM format modifiers grouped by format.
    zwlr_screencopy_manager_v1 *screencopy_manager {nullptr};  ///< WLR screencopy global used to request frames.
    zwp_linux_dmabuf_v1 *dmabuf_interface {nullptr};  ///< Linux DMA-BUF global used to allocate frame buffers.
    zxdg_output_manager_v1 *output_manager {nullptr};  ///< xdg-output global used to query monitor names and sizes.

  private:
    void add_interface(wl_registry *registry, std::uint32_t id, const char *interface, std::uint32_t version);
    void del_interface(wl_registry *registry, uint32_t id);

    std::bitset<MAX_INTERFACES> interface;
    wl_registry_listener listener;
    zwp_linux_dmabuf_v1_listener dmabuf_listener;
  };

  /**
   * @brief Wayland display connection used to dispatch capture events.
   */
  class display_t {
  public:
    /**
     * @brief Connect to the requested Wayland display.
     *
     * @param display_name Wayland display name, or nullptr to use WAYLAND_DISPLAY.
     * @return 0 when the connection is opened and initialized; -1 on failure.
     */
    int init(const char *display_name = nullptr);

    // Roundtrip with Wayland connection
    /**
     * @brief Flush pending Wayland requests and wait for replies.
     */
    void roundtrip();

    // Wait up to the timeout to read and dispatch new events
    bool dispatch(std::chrono::milliseconds timeout);

    /**
     * @brief Return the Wayland registry for global discovery.
     *
     * @return Wayland registry used to discover compositor globals.
     */
    wl_registry *registry();

    /**
     * @brief Return the native Wayland display pointer.
     *
     * @return Native display connection owned by this wrapper.
     */
    inline display_internal_t::pointer get() {
      return display_internal.get();
    }

  private:
    display_internal_t display_internal;
  };

  /**
   * @brief Refresh the monitor list reported by the display server.
   *
   * @param display_name Display name.
   * @return Monitors reported by the Wayland compositor, optionally filtered by name.
   */
  std::vector<std::unique_ptr<monitor_t>> monitors(const char *display_name = nullptr);
  /**
   * @brief Initialize Wayland registry interfaces required for capture.
   *
   * @return 0 on success; nonzero or negative platform status on failure.
   */
  int init();
}  // namespace wl
#else

struct wl_output;
struct zxdg_output_manager_v1;

namespace wl {
  class monitor_t {
  public:
    monitor_t(wl_output *output);

    monitor_t(monitor_t &&) = delete;
    monitor_t(const monitor_t &) = delete;
    monitor_t &operator=(const monitor_t &) = delete;
    monitor_t &operator=(monitor_t &&) = delete;

    void listen(zxdg_output_manager_v1 *output_manager);

    wl_output *output;
    std::string name;
    std::string description;
    platf::touch_port_t viewport;
  };

  inline std::vector<std::unique_ptr<monitor_t>> monitors(const char *display_name = nullptr) {
    return {};
  }

  inline int init() {
    return -1;
  }
}  // namespace wl
#endif
