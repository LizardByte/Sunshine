/**
 * @file src/platform/windows/display.h
 * @brief Declarations for the Windows display backend.
 */
#pragma once

// standard includes
#include <atomic>

// platform includes
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3dcommon.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <Unknwn.h>
#include <winrt/windows.graphics.capture.h>
#include <wrl/client.h>

// local includes
#include "src/platform/common.h"
#include "src/platform/windows/ipc/pipes.h"
#include "src/platform/windows/ipc/process_handler.h"
#include "src/utility.h"
#include "src/video.h"

namespace platf::dxgi {
  extern const char *format_str[];

  // Add D3D11_CREATE_DEVICE_DEBUG here to enable the D3D11 debug runtime.
  // You should have a debugger like WinDbg attached to receive debug messages.
  auto constexpr D3D11_CREATE_DEVICE_FLAGS = 0;

  template<class T>
  void Release(T *dxgi) {
    dxgi->Release();
  }

  using factory1_t = util::safe_ptr<IDXGIFactory1, Release<IDXGIFactory1>>;
  using dxgi_t = util::safe_ptr<IDXGIDevice, Release<IDXGIDevice>>;
  using dxgi1_t = util::safe_ptr<IDXGIDevice1, Release<IDXGIDevice1>>;
  using device_t = util::safe_ptr<ID3D11Device, Release<ID3D11Device>>;
  using device1_t = util::safe_ptr<ID3D11Device1, Release<ID3D11Device1>>;
  using device_ctx_t = util::safe_ptr<ID3D11DeviceContext, Release<ID3D11DeviceContext>>;
  using adapter_t = util::safe_ptr<IDXGIAdapter1, Release<IDXGIAdapter1>>;
  using output_t = util::safe_ptr<IDXGIOutput, Release<IDXGIOutput>>;
  using output1_t = util::safe_ptr<IDXGIOutput1, Release<IDXGIOutput1>>;
  using output5_t = util::safe_ptr<IDXGIOutput5, Release<IDXGIOutput5>>;
  using output6_t = util::safe_ptr<IDXGIOutput6, Release<IDXGIOutput6>>;
  using dup_t = util::safe_ptr<IDXGIOutputDuplication, Release<IDXGIOutputDuplication>>;
  using texture2d_t = util::safe_ptr<ID3D11Texture2D, Release<ID3D11Texture2D>>;
  using texture1d_t = util::safe_ptr<ID3D11Texture1D, Release<ID3D11Texture1D>>;
  using resource_t = util::safe_ptr<IDXGIResource, Release<IDXGIResource>>;
  using resource1_t = util::safe_ptr<IDXGIResource1, Release<IDXGIResource1>>;
  using multithread_t = util::safe_ptr<ID3D11Multithread, Release<ID3D11Multithread>>;
  using vs_t = util::safe_ptr<ID3D11VertexShader, Release<ID3D11VertexShader>>;
  using ps_t = util::safe_ptr<ID3D11PixelShader, Release<ID3D11PixelShader>>;
  using blend_t = util::safe_ptr<ID3D11BlendState, Release<ID3D11BlendState>>;
  using input_layout_t = util::safe_ptr<ID3D11InputLayout, Release<ID3D11InputLayout>>;
  using render_target_t = util::safe_ptr<ID3D11RenderTargetView, Release<ID3D11RenderTargetView>>;
  using shader_res_t = util::safe_ptr<ID3D11ShaderResourceView, Release<ID3D11ShaderResourceView>>;
  using buf_t = util::safe_ptr<ID3D11Buffer, Release<ID3D11Buffer>>;
  using raster_state_t = util::safe_ptr<ID3D11RasterizerState, Release<ID3D11RasterizerState>>;
  using sampler_state_t = util::safe_ptr<ID3D11SamplerState, Release<ID3D11SamplerState>>;
  using blob_t = util::safe_ptr<ID3DBlob, Release<ID3DBlob>>;
  using depth_stencil_state_t = util::safe_ptr<ID3D11DepthStencilState, Release<ID3D11DepthStencilState>>;
  using depth_stencil_view_t = util::safe_ptr<ID3D11DepthStencilView, Release<ID3D11DepthStencilView>>;
  using keyed_mutex_t = util::safe_ptr<IDXGIKeyedMutex, Release<IDXGIKeyedMutex>>;

  namespace video {
    using device_t = util::safe_ptr<ID3D11VideoDevice, Release<ID3D11VideoDevice>>;
    using ctx_t = util::safe_ptr<ID3D11VideoContext, Release<ID3D11VideoContext>>;
    using processor_t = util::safe_ptr<ID3D11VideoProcessor, Release<ID3D11VideoProcessor>>;
    using processor_out_t = util::safe_ptr<ID3D11VideoProcessorOutputView, Release<ID3D11VideoProcessorOutputView>>;
    using processor_in_t = util::safe_ptr<ID3D11VideoProcessorInputView, Release<ID3D11VideoProcessorInputView>>;
    using processor_enum_t = util::safe_ptr<ID3D11VideoProcessorEnumerator, Release<ID3D11VideoProcessorEnumerator>>;
  }  // namespace video

  class hwdevice_t;

  struct cursor_t {
    std::vector<std::uint8_t> img_data;

    DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info;
    int x, y;
    bool visible;
  };

  class gpu_cursor_t {
  public:
    gpu_cursor_t():
        cursor_view {0, 0, 0, 0, 0.0f, 1.0f} {};

    void set_pos(LONG topleft_x, LONG topleft_y, LONG display_width, LONG display_height, DXGI_MODE_ROTATION display_rotation, bool visible) {
      this->topleft_x = topleft_x;
      this->topleft_y = topleft_y;
      this->display_width = display_width;
      this->display_height = display_height;
      this->display_rotation = display_rotation;
      this->visible = visible;
      update_viewport();
    }

    void set_texture(LONG texture_width, LONG texture_height, texture2d_t &&texture) {
      this->texture = std::move(texture);
      this->texture_width = texture_width;
      this->texture_height = texture_height;
      update_viewport();
    }

    void update_viewport() {
      switch (display_rotation) {
        case DXGI_MODE_ROTATION_UNSPECIFIED:
        case DXGI_MODE_ROTATION_IDENTITY:
          cursor_view.TopLeftX = topleft_x;
          cursor_view.TopLeftY = topleft_y;
          cursor_view.Width = texture_width;
          cursor_view.Height = texture_height;
          break;

        case DXGI_MODE_ROTATION_ROTATE90:
          cursor_view.TopLeftX = topleft_y;
          cursor_view.TopLeftY = display_width - texture_width - topleft_x;
          cursor_view.Width = texture_height;
          cursor_view.Height = texture_width;
          break;

        case DXGI_MODE_ROTATION_ROTATE180:
          cursor_view.TopLeftX = display_width - texture_width - topleft_x;
          cursor_view.TopLeftY = display_height - texture_height - topleft_y;
          cursor_view.Width = texture_width;
          cursor_view.Height = texture_height;
          break;

        case DXGI_MODE_ROTATION_ROTATE270:
          cursor_view.TopLeftX = display_height - texture_height - topleft_y;
          cursor_view.TopLeftY = topleft_x;
          cursor_view.Width = texture_height;
          cursor_view.Height = texture_width;
          break;
      }
    }

    texture2d_t texture;
    LONG texture_width;
    LONG texture_height;

    LONG topleft_x;
    LONG topleft_y;

    LONG display_width;
    LONG display_height;
    DXGI_MODE_ROTATION display_rotation;

    shader_res_t input_res;

    D3D11_VIEWPORT cursor_view;

    bool visible;
  };

  class display_base_t: public display_t {
  public:
    int init(const ::video::config_t &config, const std::string &display_name);

    capture_e capture(const push_captured_image_cb_t &push_captured_image_cb, const pull_free_image_cb_t &pull_free_image_cb, bool *cursor) override;

    factory1_t factory;
    adapter_t adapter;
    output_t output;
    device_t device;
    device_ctx_t device_ctx;
    DXGI_RATIONAL display_refresh_rate;
    int display_refresh_rate_rounded;

    DXGI_MODE_ROTATION display_rotation = DXGI_MODE_ROTATION_UNSPECIFIED;
    int width_before_rotation;
    int height_before_rotation;

    int client_frame_rate;

    DXGI_FORMAT capture_format;
    D3D_FEATURE_LEVEL feature_level;

    std::unique_ptr<high_precision_timer> timer = create_high_precision_timer();

    typedef enum _D3DKMT_SCHEDULINGPRIORITYCLASS {
      D3DKMT_SCHEDULINGPRIORITYCLASS_IDLE,  ///< Idle priority class
      D3DKMT_SCHEDULINGPRIORITYCLASS_BELOW_NORMAL,  ///< Below normal priority class
      D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL,  ///< Normal priority class
      D3DKMT_SCHEDULINGPRIORITYCLASS_ABOVE_NORMAL,  ///< Above normal priority class
      D3DKMT_SCHEDULINGPRIORITYCLASS_HIGH,  ///< High priority class
      D3DKMT_SCHEDULINGPRIORITYCLASS_REALTIME  ///< Realtime priority class
    } D3DKMT_SCHEDULINGPRIORITYCLASS;

    typedef UINT D3DKMT_HANDLE;

    typedef struct _D3DKMT_OPENADAPTERFROMLUID {
      LUID AdapterLuid;
      D3DKMT_HANDLE hAdapter;
    } D3DKMT_OPENADAPTERFROMLUID;

    typedef struct _D3DKMT_WDDM_2_7_CAPS {
      union {
        struct
        {
          UINT HwSchSupported : 1;
          UINT HwSchEnabled : 1;
          UINT HwSchEnabledByDefault : 1;
          UINT IndependentVidPnVSyncControl : 1;
          UINT Reserved : 28;
        };

        UINT Value;
      };
    } D3DKMT_WDDM_2_7_CAPS;

    typedef struct _D3DKMT_QUERYADAPTERINFO {
      D3DKMT_HANDLE hAdapter;
      UINT Type;
      VOID *pPrivateDriverData;
      UINT PrivateDriverDataSize;
    } D3DKMT_QUERYADAPTERINFO;

    const UINT KMTQAITYPE_WDDM_2_7_CAPS = 70;

    typedef struct _D3DKMT_CLOSEADAPTER {
      D3DKMT_HANDLE hAdapter;
    } D3DKMT_CLOSEADAPTER;

    typedef NTSTATUS(WINAPI *PD3DKMTSetProcessSchedulingPriorityClass)(HANDLE, D3DKMT_SCHEDULINGPRIORITYCLASS);
    typedef NTSTATUS(WINAPI *PD3DKMTOpenAdapterFromLuid)(D3DKMT_OPENADAPTERFROMLUID *);
    typedef NTSTATUS(WINAPI *PD3DKMTQueryAdapterInfo)(D3DKMT_QUERYADAPTERINFO *);
    typedef NTSTATUS(WINAPI *PD3DKMTCloseAdapter)(D3DKMT_CLOSEADAPTER *);

    virtual bool is_hdr() override;
    virtual bool get_hdr_metadata(SS_HDR_METADATA &metadata) override;

    const char *dxgi_format_to_string(DXGI_FORMAT format);
    const char *colorspace_to_string(DXGI_COLOR_SPACE_TYPE type);
    virtual std::vector<DXGI_FORMAT> get_supported_capture_formats() = 0;

  protected:
    int get_pixel_pitch() {
      return (capture_format == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 8 : 4;
    }

    virtual capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) = 0;
    virtual capture_e release_snapshot() = 0;
    virtual int complete_img(img_t *img, bool dummy) = 0;
  };

  /**
   * Display component for devices that use software encoders.
   */
  class display_ram_t: public display_base_t {
  public:
    std::shared_ptr<img_t> alloc_img() override;
    int dummy_img(img_t *img) override;
    int complete_img(img_t *img, bool dummy) override;
    std::vector<DXGI_FORMAT> get_supported_capture_formats() override;

    std::unique_ptr<avcodec_encode_device_t> make_avcodec_encode_device(pix_fmt_e pix_fmt) override;

    D3D11_MAPPED_SUBRESOURCE img_info;
    texture2d_t texture;
  };

  /**
   * Display component for devices that use hardware encoders.
   */
  class display_vram_t: public display_base_t, public std::enable_shared_from_this<display_vram_t> {
  public:
    std::shared_ptr<img_t> alloc_img() override;
    int dummy_img(img_t *img_base) override;
    int complete_img(img_t *img_base, bool dummy) override;
    std::vector<DXGI_FORMAT> get_supported_capture_formats() override;

    bool is_codec_supported(std::string_view name, const ::video::config_t &config) override;

    std::unique_ptr<avcodec_encode_device_t> make_avcodec_encode_device(pix_fmt_e pix_fmt) override;

    std::unique_ptr<nvenc_encode_device_t> make_nvenc_encode_device(pix_fmt_e pix_fmt) override;

    std::atomic<uint32_t> next_image_id;
  };

  /**
   * Display duplicator that uses the DirectX Desktop Duplication API.
   */
  class duplication_t {
  public:
    dup_t dup;
    bool has_frame {};
    std::chrono::steady_clock::time_point last_protected_content_warning_time {};

    int init(display_base_t *display, const ::video::config_t &config);
    capture_e next_frame(DXGI_OUTDUPL_FRAME_INFO &frame_info, std::chrono::milliseconds timeout, resource_t::pointer *res_p);
    capture_e reset(dup_t::pointer dup_p = dup_t::pointer());
    capture_e release_frame();

    ~duplication_t();
  };

  /**
   * Display backend that uses DDAPI with a software encoder.
   */
  class display_ddup_ram_t: public display_ram_t {
  public:
    int init(const ::video::config_t &config, const std::string &display_name);
    capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) override;
    capture_e release_snapshot() override;

    duplication_t dup;
    cursor_t cursor;
  };

  /**
   * Display backend that uses DDAPI with a hardware encoder.
   */
  class display_ddup_vram_t: public display_vram_t {
  public:
    int init(const ::video::config_t &config, const std::string &display_name);
    capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) override;
    capture_e release_snapshot() override;

    duplication_t dup;
    sampler_state_t sampler_linear;

    blend_t blend_alpha;
    blend_t blend_invert;
    blend_t blend_disable;

    ps_t cursor_ps;
    vs_t cursor_vs;

    gpu_cursor_t cursor_alpha;
    gpu_cursor_t cursor_xor;

    texture2d_t old_surface_delayed_destruction;
    std::chrono::steady_clock::time_point old_surface_timestamp;
    std::variant<std::monostate, texture2d_t, std::shared_ptr<platf::img_t>> last_frame_variant;
  };

  /**
   * Display backend that uses Windows.Graphics.Capture with a software encoder.
   * This now always uses IPC implementation via display_wgc_ipc_ram_t.
   */
  class display_wgc_ram_t {
  public:
    /**
     * @brief Factory method for initializing WGC RAM capture backend.
     * Always returns the IPC implementation for Windows.Graphics.Capture using a software encoder.
     * @param config Video configuration parameters.
     * @param display_name Name of the display to capture.
     * @return Shared pointer to the initialized display backend.
     */
    static std::shared_ptr<display_t> create(const ::video::config_t &config, const std::string &display_name);
  };

  /**
   * @class display_wgc_vram_t
   * @brief Factory class for initializing Windows.Graphics.Capture (WGC) display backends using VRAM.
   * Provides a static factory method to create and initialize a display backend for capturing
   * displays via the Windows.Graphics.Capture API, utilizing hardware encoding when available.
   */
  class display_wgc_vram_t {
  public:
    /**
     * @brief Factory method for initializing WGC VRAM capture backend.
     * Always returns the IPC implementation for Windows.Graphics.Capture using a hardware encoder.
     * @param config Video configuration parameters.
     * @param display_name Name of the display to capture.
     * @return Shared pointer to the initialized display backend.
     */
    static std::shared_ptr<display_t> create(const ::video::config_t &config, const std::string &display_name);
  };

  /**
   * @class display_wgc_ipc_vram_t
   * @brief Display capture backend using Windows.Graphics.Capture (WGC) via a separate capture process.
   * This backend utilizes a separate capture process and synchronizes frames to Sunshine,
   * allowing screen capture even when running as a SYSTEM service.
   */
  class display_wgc_ipc_vram_t: public display_vram_t {
    // Cache for frame forwarding when no new frame is available
    std::shared_ptr<platf::img_t> last_cached_frame;

  public:
    /**
     * @brief Constructs a new display_wgc_ipc_vram_t object.
     * Initializes the WGC IPC VRAM display backend for hardware encoding.
     * Sets up internal state and prepares for display capture via IPC.
     */
    display_wgc_ipc_vram_t();

    /**
     * @brief Destructor for display_wgc_ipc_vram_t.
     * Cleans up resources and IPC session associated with the WGC IPC VRAM display backend.
     */
    ~display_wgc_ipc_vram_t() override;

    /**
     * @brief Factory method to create a WGC IPC VRAM display instance or fallback.
     * Chooses the appropriate backend based on the current system state and configuration.
     * @param config Video configuration parameters.
     * @param display_name Name of the display to capture.
     * @return Instance of the display backend, using WGC IPC if available, or a secure desktop fallback if not.
     */
    static std::shared_ptr<display_t> create(const ::video::config_t &config, const std::string &display_name);

    /**
     * @brief Initializes the WGC IPC VRAM display backend.
     * Sets up the display backend with the provided configuration and display name.
     * @param config Video configuration parameters.
     * @param display_name Name of the display to capture.
     * @return 0 on success, negative on failure.
     */
    int init(const ::video::config_t &config, const std::string &display_name);

    /**
     * @brief Captures a snapshot of the display.
     * @param pull_free_image_cb Callback to pull a free image buffer.
     * @param img_out Output parameter for the captured image.
     * @param timeout Maximum time to wait for a frame.
     * @param cursor_visible Whether the cursor should be included in the capture.
     * @return Status of the capture operation.
     */
    capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) override;

    /**
     * @brief Fills an image with dummy data.
     * Used for testing or fallback scenarios where a real image is not available.
     * @param img_base Pointer to the image to fill.
     * @return 0 on success, negative on failure.
     */
    int dummy_img(platf::img_t *img_base) override;

  protected:
    /**
     * @brief Acquires the next frame from the display.
     * @param timeout Maximum time to wait for a frame.
     * @param src Output parameter for the source texture.
     * @param frame_qpc Output parameter for the frame's QPC timestamp.
     * @param cursor_visible Whether the cursor should be included in the capture.
     * @return Status of the frame acquisition operation.
     */
    capture_e acquire_next_frame(std::chrono::milliseconds timeout, texture2d_t &src, uint64_t &frame_qpc, bool cursor_visible);

    /**
     * @brief Releases resources or state after a snapshot.
     * @return Status of the release operation.
     */
    capture_e release_snapshot() override;

  private:
    std::unique_ptr<class ipc_session_t> _ipc_session;
    ::video::config_t _config;
    std::string _display_name;
    bool _session_initialized_logged = false;
  };

  class display_wgc_ipc_ram_t: public display_ram_t {
  public:
    /**
     * @brief Constructs a new display_wgc_ipc_ram_t object.
     * Initializes internal state for the WGC IPC RAM display backend.
     */
    display_wgc_ipc_ram_t();

    /**
     * @brief Destructor for display_wgc_ipc_ram_t.
     * Cleans up resources associated with the WGC IPC RAM display backend.
     */
    ~display_wgc_ipc_ram_t() override;

    /**
     * @brief Factory method to create a WGC IPC RAM display instance or fallback.
     * @param config Video configuration parameters.
     * @param display_name Name of the display to capture.
     * @return Instance of the display backend.
     */
    static std::shared_ptr<display_t> create(const ::video::config_t &config, const std::string &display_name);

    /**
     * @brief Initializes the WGC IPC RAM display backend.
     * @param config Video configuration parameters.
     * @param display_name Name of the display to capture.
     * @return 0 on success, negative on failure.
     */
    int init(const ::video::config_t &config, const std::string &display_name);

    /**
     * @brief Captures a snapshot of the display.
     * @param pull_free_image_cb Callback to pull a free image buffer.
     * @param img_out Output parameter for the captured image.
     * @param timeout Maximum time to wait for a frame.
     * @param cursor_visible Whether the cursor should be included in the capture.
     * @return Status of the capture operation.
     */
    capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) override;

    /**
     * @brief Fills an image with dummy data.
     * Used for testing or fallback scenarios where a real image is not available.
     * @param img_base Pointer to the image to fill.
     * @return 0 on success, negative on failure.
     */
    int dummy_img(platf::img_t *img_base) override;

  protected:
    /**
     * @brief Releases resources or state after a snapshot.
     * @return Status of the release operation.
     */
    capture_e release_snapshot() override;

  private:
    /**
     * @brief IPC session for communication with capture helper.
     */
    std::unique_ptr<class ipc_session_t> _ipc_session;
    /**
     * @brief Video configuration used for capture.
     */
    ::video::config_t _config;
    /**
     * @brief Name of the display being captured.
     */
    std::string _display_name;

    /**
     * @brief Last width of the staging texture for the base class texture.
     */
    UINT _last_width = 0;
    /**
     * @brief Last height of the staging texture for the base class texture.
     */
    UINT _last_height = 0;
    /**
     * @brief Last DXGI format of the staging texture for the base class texture.
     */
    DXGI_FORMAT _last_format = DXGI_FORMAT_UNKNOWN;

    /**
     * @brief Cache for frame forwarding when no new frame is available, only used in constant capture mode.
     */
    std::shared_ptr<platf::img_t> last_cached_frame;
  };

  /**
   * @brief Temporary DXGI VRAM display backend for secure desktop scenarios.
   * This display backend uses DXGI duplication for capturing the screen when secure desktop is active.
   * It periodically checks if secure desktop is no longer active and, if so, can swap back to WGC.
   */
  class temp_dxgi_vram_t: public display_ddup_vram_t {
  private:
    std::chrono::steady_clock::time_point _last_check_time;
    static constexpr std::chrono::seconds CHECK_INTERVAL {2};  // Check every 2 seconds

  public:
    capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) override;
  };

  /**
   * @brief Display backend that uses DXGI duplication for secure desktop scenarios.
   * This display can detect when secure desktop is no longer active and swap back to WGC.
   */
  class temp_dxgi_ram_t: public display_ddup_ram_t {
  private:
    /**
     * @brief The last time a check for secure desktop status was performed.
     */
    std::chrono::steady_clock::time_point _last_check_time;

    /**
     * @brief Interval between secure desktop status checks (every 2 seconds).
     */
    static constexpr std::chrono::seconds CHECK_INTERVAL {2};

  public:
    /**
     * @brief Captures a snapshot of the display using DXGI duplication.
     * This method attempts to capture the current frame from the display, handling secure desktop scenarios.
     * If secure desktop is no longer active, it can swap back to WGC.
     * @param pull_free_image_cb Callback to pull a free image buffer.
     * @param img_out Output parameter for the captured image.
     * @param timeout Maximum time to wait for a frame.
     * @param cursor_visible Whether the cursor should be included in the capture.
     * @return Status of the capture operation.
     */
    capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) override;
  };

  // Type aliases for WGC data structures
  using shared_handle_data_t = platf::dxgi::shared_handle_data_t;
  using config_data_t = platf::dxgi::config_data_t;

}  // namespace platf::dxgi
