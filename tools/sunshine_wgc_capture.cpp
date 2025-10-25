/**
 * @file sunshine_wgc_capture.cpp
 * @brief Windows Graphics Capture helper process for Sunshine.
 *
 * This standalone executable provides Windows Graphics Capture functionality
 * for the main Sunshine streaming process. It runs as a separate process to
 * isolate WGC operations and handle secure desktop scenarios. The helper
 * communicates with the main process via named pipes and shared D3D11 textures.
 */

#define WIN32_LEAN_AND_MEAN

// standard includes
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

// local includes
#include "src/logging.h"
#include "src/platform/windows/ipc/misc_utils.h"
#include "src/platform/windows/ipc/pipes.h"
#include "src/utility.h"  // For RAII utilities

// platform includes
#include <d3d11.h>
#include <dxgi1_2.h>
#include <inspectable.h>  // For IInspectable
#include <ShellScalingApi.h>  // For DPI awareness
#include <Windows.Graphics.Capture.Interop.h>
#include <Windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>  // For ApiInformation
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

// Manual declaration for CreateDirect3D11DeviceFromDXGIDevice if missing
extern "C" {
  HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(::IDXGIDevice *dxgiDevice, ::IInspectable **graphicsDevice);
}

/**
 * Windows structures sometimes have compile-time GUIDs. GCC supports this, but in a roundabout way.
 * If WINRT_IMPL_HAS_DECLSPEC_UUID is true, then the compiler supports adding this attribute to a struct. For example, Visual Studio.
 * If not, then MinGW GCC has a workaround to assign a GUID to a structure.
 */
struct
#if WINRT_IMPL_HAS_DECLSPEC_UUID
  __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
#endif
  IDirect3DDxgiInterfaceAccess: ::IUnknown {
  virtual HRESULT __stdcall GetInterface(REFIID id, IUnknown **object) = 0;
};

#if !WINRT_IMPL_HAS_DECLSPEC_UUID
static constexpr GUID GUID__IDirect3DDxgiInterfaceAccess = {
  0xA9B3D012,
  0x3DF2,
  0x4EE3,
  {0xB8, 0xD1, 0x86, 0x95, 0xF4, 0x57, 0xD3, 0xC1}
};

template<>
constexpr auto __mingw_uuidof<IDirect3DDxgiInterfaceAccess>() -> GUID const & {
  return GUID__IDirect3DDxgiInterfaceAccess;
}

static constexpr GUID GUID__IDirect3DSurface = {
  0x0BF4A146,
  0x13C1,
  0x4694,
  {0xBE, 0xE3, 0x7A, 0xBF, 0x15, 0xEA, 0xF5, 0x86}
};

template<>
constexpr auto __mingw_uuidof<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface>() -> GUID const & {
  return GUID__IDirect3DSurface;
}
#endif

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace platf::dxgi;

// GPU scheduling priority definitions for optimal capture performance under high GPU load
enum class D3DKMT_SchedulingPriorityClass : LONG {
  IDLE = 0,
  BELOW_NORMAL = 1,
  NORMAL = 2,
  ABOVE_NORMAL = 3,
  HIGH = 4,
  REALTIME = 5
};

using PD3DKMTSetProcessSchedulingPriorityClass = LONG(__stdcall *)(HANDLE, LONG);

/**
 * @brief D3D11 device creation flags for the WGC helper process.
 *
 * NOTE: This constant is NOT shared with the main process. It is defined here separately
 * because including display.h would pull in too many dependencies for this standalone helper.
 * If you change this flag, you MUST update it in both this file and src/platform/windows/display.h.
 */
constexpr UINT D3D11_CREATE_DEVICE_FLAGS = 0;

/**
 * @brief Initial log level for the helper process.
 */
const int INITIAL_LOG_LEVEL = 2;

/**
 * @brief Global configuration data received from the main process.
 */
static platf::dxgi::config_data_t g_config = {0, 0, L"", {0, 0}};

/**
 * @brief Flag indicating whether configuration data has been received from main process.
 */
static bool g_config_received = false;

/**
 * @brief Global communication pipe for sending session closed notifications.
 */
static std::weak_ptr<AsyncNamedPipe> g_communication_pipe_weak;

/**
 * @brief Global Windows event hook for desktop switch detection.
 */
static safe_winevent_hook g_desktop_switch_hook = nullptr;

/**
 * @brief Flag indicating if a secure desktop has been detected.
 */
static bool g_secure_desktop_detected = false;

/**
 * @brief System initialization class to handle DPI, threading, and MMCSS setup.
 *
 * This class manages critical system-level initialization for optimal capture performance:
 * - Sets DPI awareness to handle high-DPI displays correctly
 * - Elevates thread priority for better capture timing
 * - Configures MMCSS (Multimedia Class Scheduler Service) for audio/video workloads
 * - Initializes WinRT apartment for Windows Graphics Capture APIs
 */
class SystemInitializer {
private:
  safe_mmcss_handle _mmcss_handle = nullptr;  ///< Handle for MMCSS thread characteristics
  bool _dpi_awareness_set = false;  ///< Flag indicating if DPI awareness was successfully set
  bool _thread_priority_set = false;  ///< Flag indicating if thread priority was elevated
  bool _mmcss_characteristics_set = false;  ///< Flag indicating if MMCSS characteristics were set
  bool _gpu_priority_set = false;  ///< Flag indicating if GPU scheduling priority was set

public:
  /**
   * @brief Initializes DPI awareness for the process.
   *
   * Attempts to set per-monitor DPI awareness to handle high-DPI displays correctly.
   * First tries the newer SetProcessDpiAwarenessContext API, then falls back to
   * SetProcessDpiAwareness if the newer API is not available.
   *
   * @return true if DPI awareness was successfully set, false otherwise.
   */
  bool initialize_dpi_awareness() {
    // Try newer API first
    if (HMODULE user32_module = GetModuleHandleA("user32.dll")) {
      using set_process_dpi_awareness_context_fn = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
      auto set_process_dpi_awareness_context =
        reinterpret_cast<set_process_dpi_awareness_context_fn>(
          GetProcAddress(user32_module, "SetProcessDpiAwarenessContext")
        );

      if (set_process_dpi_awareness_context &&
          set_process_dpi_awareness_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        _dpi_awareness_set = true;
        return true;
      }
    }

    // Fallback to older API (Win 8.1+)
    if (SUCCEEDED(SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE))) {
      _dpi_awareness_set = true;
      return true;
    }

    BOOST_LOG(warning) << "Failed to set DPI awareness, display scaling issues may occur";
    return false;
  }

  /**
   * @brief Elevates the current thread priority for better capture performance.
   *
   * Sets the thread priority to THREAD_PRIORITY_HIGHEST to reduce latency
   * and improve capture frame timing consistency.
   *
   * @return true if thread priority was successfully elevated, false otherwise.
   */
  bool initialize_thread_priority() {
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
      BOOST_LOG(error) << "Failed to set thread priority: " << GetLastError();
      return false;
    }
    _thread_priority_set = true;
    return true;
  }

  /**
   * @brief Configures MMCSS (Multimedia Class Scheduler Service) characteristics.
   *
   * Registers the thread with MMCSS for multimedia workload scheduling.
   * First attempts "Pro Audio" task profile, then falls back to "Games" profile.
   * This helps ensure consistent timing for capture operations.
   * Additionally sets MMCSS thread relative priority to maximum for best performance.
   *
   * @return true if MMCSS characteristics were successfully set, false otherwise.
   */
  bool initialize_mmcss_characteristics() {
    DWORD task_idx = 0;
    HANDLE raw_handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_idx);
    if (!raw_handle) {
      raw_handle = AvSetMmThreadCharacteristicsW(L"Games", &task_idx);
      if (!raw_handle) {
        BOOST_LOG(error) << "Failed to set MMCSS characteristics: " << GetLastError();
        return false;
      }
    }

    // Set MMCSS thread relative priority to maximum (AVRT_PRIORITY_HIGH = 2)
    if (!AvSetMmThreadPriority(raw_handle, AVRT_PRIORITY_HIGH)) {
      BOOST_LOG(warning) << "Failed to set MMCSS thread priority: " << GetLastError();
      // Don't fail completely as the basic MMCSS registration still works
    }

    _mmcss_handle.reset(raw_handle);
    _mmcss_characteristics_set = true;
    return true;
  }

  /**
   * @brief Sets GPU scheduling priority for optimal capture performance under high GPU load.
   *
   * Configures the process GPU scheduling priority to REALTIME. This is critical for maintaining
   * capture performance when the GPU is under heavy load from games or other applications.
   *
   * @return true if GPU priority was successfully set, false otherwise.
   */
  bool initialize_gpu_scheduling_priority() {
    HMODULE gdi32 = GetModuleHandleA("gdi32.dll");
    if (!gdi32) {
      BOOST_LOG(warning) << "Failed to get gdi32.dll handle for GPU priority adjustment";
      return false;
    }

    auto d3dkmt_set_process_priority = reinterpret_cast<PD3DKMTSetProcessSchedulingPriorityClass>(GetProcAddress(gdi32, "D3DKMTSetProcessSchedulingPriorityClass"));
    if (!d3dkmt_set_process_priority) {
      BOOST_LOG(warning) << "D3DKMTSetProcessSchedulingPriorityClass not available, GPU priority not set";
      return false;
    }

    auto priority = static_cast<LONG>(D3DKMT_SchedulingPriorityClass::REALTIME);

    HRESULT hr = d3dkmt_set_process_priority(GetCurrentProcess(), priority);
    if (FAILED(hr)) {
      BOOST_LOG(warning) << "Failed to set GPU scheduling priority to REALTIME: " << hr
                         << " (may require administrator privileges for optimal performance)";
      return false;
    }

    BOOST_LOG(info) << "GPU scheduling priority set to REALTIME for optimal capture performance";
    _gpu_priority_set = true;
    return true;
  }

  /**
   * @brief Initializes WinRT apartment for Windows Graphics Capture APIs.
   *
   * Sets up the WinRT apartment as multi-threaded to support Windows Graphics
   * Capture operations from background threads.
   *
   * @return true always (WinRT initialization typically succeeds).
   */
  bool initialize_winrt_apartment() const {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    return true;
  }

  /**
   * @brief Initializes all system components for optimal capture performance.
   *
   * Calls all initialization methods in sequence:
   * - DPI awareness configuration
   * - Thread priority elevation
   * - GPU scheduling priority setup
   * - MMCSS characteristics setup
   * - WinRT apartment initialization
   *
   * @return true if all initialization steps succeeded, false if any failed.
   */
  bool initialize_all() {
    bool success = true;
    success &= initialize_dpi_awareness();
    success &= initialize_thread_priority();
    success &= initialize_gpu_scheduling_priority();
    success &= initialize_mmcss_characteristics();
    success &= initialize_winrt_apartment();
    return success;
  }

  /**
   * @brief Checks if DPI awareness was successfully set.
   * @return true if DPI awareness is configured, false otherwise.
   */
  bool is_dpi_awareness_set() const {
    return _dpi_awareness_set;
  }

  /**
   * @brief Checks if thread priority was successfully elevated.
   * @return true if thread priority is elevated, false otherwise.
   */
  bool is_thread_priority_set() const {
    return _thread_priority_set;
  }

  /**
   * @brief Checks if MMCSS characteristics were successfully set.
   * @return true if MMCSS characteristics are configured, false otherwise.
   */
  bool is_mmcss_characteristics_set() const {
    return _mmcss_characteristics_set;
  }

  /**
   * @brief Checks if GPU scheduling priority was successfully set.
   * @return true if GPU scheduling priority is configured, false otherwise.
   */
  bool is_gpu_priority_set() const {
    return _gpu_priority_set;
  }

  /**
   * @brief Destructor for SystemInitializer.
   *
   * RAII automatically releases MMCSS resources.
   */
  ~SystemInitializer() noexcept = default;
};

/**
 * @brief D3D11 device management class to handle device creation and WinRT interop.
 *
 * This class manages D3D11 device and context creation, as well as the WinRT
 * interop device required for Windows Graphics Capture. It handles the complex
 * process of bridging between traditional D3D11 APIs and WinRT capture APIs.
 */
class D3D11DeviceManager {
private:
  winrt::com_ptr<ID3D11Device> _device;  ///< D3D11 device for graphics operations
  winrt::com_ptr<ID3D11DeviceContext> _context;  ///< D3D11 device context for rendering
  D3D_FEATURE_LEVEL _feature_level;  ///< D3D feature level supported by the device
  winrt::com_ptr<IDXGIDevice> _dxgi_device;  ///< DXGI device interface for WinRT interop
  winrt::com_ptr<::IDirect3DDevice> _interop_device;  ///< Intermediate interop device
  IDirect3DDevice _winrt_device = nullptr;  ///< WinRT Direct3D device for capture integration

public:
  /**
   * @brief Creates a D3D11 device and context for graphics operations.
   *
   * Creates a hardware-accelerated D3D11 device using the specified adapter.
   * The device is used for texture operations and WinRT interop.
   * Also sets GPU thread priority to 7 for optimal capture performance.
   * Uses the same D3D11_CREATE_DEVICE_FLAGS as the main Sunshine process.
   *
   * @param adapter_luid LUID of the adapter to use, or all zeros for default adapter.
   * @return true if device creation succeeded, false otherwise.
   */
  bool create_device(const LUID &adapter_luid) {
    // Feature levels to try, matching the main process
    D3D_FEATURE_LEVEL featureLevels[] {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,
      D3D_FEATURE_LEVEL_9_2,
      D3D_FEATURE_LEVEL_9_1
    };

    // Find the adapter matching the LUID
    winrt::com_ptr<IDXGIAdapter1> adapter;
    if (adapter_luid.HighPart != 0 || adapter_luid.LowPart != 0) {
      // Non-zero LUID provided, find the matching adapter
      winrt::com_ptr<IDXGIFactory1> factory;
      HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.put_void());
      if (FAILED(hr)) {
        BOOST_LOG(error) << "Failed to create DXGI factory for adapter lookup";
        return false;
      }

      IDXGIAdapter1 *raw_adapter = nullptr;
      for (UINT i = 0; factory->EnumAdapters1(i, &raw_adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        winrt::com_ptr<IDXGIAdapter1> test_adapter;
        test_adapter.attach(raw_adapter);
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(test_adapter->GetDesc1(&desc))) {
          if (desc.AdapterLuid.HighPart == adapter_luid.HighPart &&
              desc.AdapterLuid.LowPart == adapter_luid.LowPart) {
            adapter = test_adapter;
            BOOST_LOG(info) << "Found matching adapter: " << wide_to_utf8(desc.Description);
            break;
          }
        }
      }

      if (!adapter) {
        BOOST_LOG(warning) << "Could not find adapter with LUID "
                           << std::hex << adapter_luid.HighPart << ":" << adapter_luid.LowPart
                           << std::dec << ", using default adapter";
      }
    } else {
      BOOST_LOG(info) << "Using default adapter (no LUID specified)";
    }

    // Create the D3D11 device using the same flags as the main process
    HRESULT hr = D3D11CreateDevice(
      adapter.get(),  // nullptr if no specific adapter found
      adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
      nullptr,
      D3D11_CREATE_DEVICE_FLAGS,
      featureLevels,
      ARRAYSIZE(featureLevels),
      D3D11_SDK_VERSION,
      _device.put(),
      &_feature_level,
      _context.put()
    );

    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to create D3D11 device: " << std::hex << hr << std::dec;
      return false;
    }

    // Set GPU thread priority to 7 for optimal capture performance under high GPU load
    _dxgi_device = _device.as<IDXGIDevice>();
    if (_dxgi_device) {
      hr = _dxgi_device->SetGPUThreadPriority(7);
      if (FAILED(hr)) {
        BOOST_LOG(warning) << "Failed to set GPU thread priority to 7: " << hr
                           << " (may require administrator privileges for optimal performance)";
      } else {
        BOOST_LOG(info) << "GPU thread priority set to 7 for optimal capture performance";
      }
    } else {
      BOOST_LOG(warning) << "Failed to query DXGI device for GPU thread priority setting";
    }

    return true;
  }

  /**
   * @brief Creates WinRT interop device from the D3D11 device.
   *
   * Bridges the D3D11 device to WinRT APIs required for Windows Graphics Capture.
   * This involves:
   * - Querying DXGI device interface from D3D11 device
   * - Creating Direct3D interop device using WinRT factory
   * - Converting to WinRT IDirect3DDevice interface
   *
   * @return true if WinRT interop device creation succeeded, false otherwise.
   */
  bool create_winrt_interop() {
    if (!_device) {
      return false;
    }

    _dxgi_device = _device.as<IDXGIDevice>();
    if (!_dxgi_device) {
      BOOST_LOG(error) << "Failed to get DXGI device";
      return false;
    }

    HRESULT hr = CreateDirect3D11DeviceFromDXGIDevice(_dxgi_device.get(), reinterpret_cast<::IInspectable **>(winrt::put_abi(_interop_device)));
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to create interop device";
      return false;
    }

    _winrt_device = _interop_device.as<IDirect3DDevice>();
    return true;
  }

  /**
   * @brief Initializes the D3D11 device and WinRT interop device.
   *
   * Calls create_device() with the specified adapter LUID, then create_winrt_interop() to create the WinRT interop device.
   *
   * @param adapter_luid LUID of the adapter to use, or all zeros for default adapter.
   * @return true if both device and interop device are successfully created, false otherwise.
   */
  bool initialize_all(const LUID &adapter_luid) {
    return create_device(adapter_luid) && create_winrt_interop();
  }

  /**
   * @brief Gets the underlying D3D11 device com_ptr.
   *
   * @return Pointer to the managed ID3D11Device, or empty if not initialized.
   */
  const winrt::com_ptr<ID3D11Device> &get_device() const {
    return _device;
  }

  /**
   * @brief Gets the underlying D3D11 device context com_ptr.
   *
   * @return Pointer to the managed ID3D11DeviceContext, or empty if not initialized.
   */
  const winrt::com_ptr<ID3D11DeviceContext> &get_context() const {
    return _context;
  }

  /**
   * @brief Gets the WinRT IDirect3DDevice for Windows Graphics Capture interop.
   *
   * @return The WinRT IDirect3DDevice, or nullptr if not initialized.
   */
  IDirect3DDevice get_winrt_device() const {
    return _winrt_device;
  }

  /**
   * @brief Destructor for D3D11DeviceManager.
   *
   * RAII automatically releases device and context resources.
   */
  ~D3D11DeviceManager() noexcept = default;
};

/**
 * @brief Monitor and display management class to handle monitor enumeration and selection.
 *
 * This class manages monitor detection, selection, and configuration for capture operations.
 * It handles scenarios with multiple monitors and provides resolution configuration based
 * on monitor capabilities and user preferences.
 */
class DisplayManager {
private:
  HMONITOR _selected_monitor = nullptr;  ///< Handle to the selected monitor for capture
  MONITORINFO _monitor_info = {sizeof(MONITORINFO)};  ///< Information about the selected monitor
  UINT _width = 0;  ///< Final capture width in pixels
  UINT _height = 0;  ///< Final capture height in pixels

public:
  /**
   * @brief Selects a monitor for capture based on the provided configuration.
   *
   * If a display name is specified in the config, attempts to find and select the monitor matching that name.
   * If not found, or if no display name is provided, falls back to selecting the primary monitor.
   *
   * @param config The configuration data containing the desired display name (if any).
   * @return true if a monitor was successfully selected; false if monitor selection failed.
   */
  bool select_monitor(const platf::dxgi::config_data_t &config) {
    if (config.display_name[0] != L'\0') {
      // Enumerate monitors to find one matching displayName
      struct EnumData {
        const wchar_t *target_name;
        HMONITOR found_monitor;
      };

      EnumData enum_data = {config.display_name, nullptr};

      auto enum_proc = +[](HMONITOR h_mon, HDC /*hdc*/, RECT * /*rc*/, LPARAM l_param) {
        auto *data = static_cast<EnumData *>(reinterpret_cast<void *>(l_param));
        if (MONITORINFOEXW m_info = {sizeof(MONITORINFOEXW)}; GetMonitorInfoW(h_mon, &m_info) && wcsncmp(m_info.szDevice, data->target_name, 32) == 0) {
          data->found_monitor = h_mon;
          return FALSE;  // Stop enumeration
        }
        return TRUE;
      };
      EnumDisplayMonitors(nullptr, nullptr, enum_proc, static_cast<LPARAM>(reinterpret_cast<std::uintptr_t>(&enum_data)));
      _selected_monitor = enum_data.found_monitor;
      if (!_selected_monitor) {
        BOOST_LOG(warning) << "Could not find monitor with name '" << winrt::to_string(config.display_name) << "', falling back to primary.";
      }
    }

    if (!_selected_monitor) {
      _selected_monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
      if (!_selected_monitor) {
        BOOST_LOG(error) << "Failed to get primary monitor";
        return false;
      }
    }
    return true;
  }

  /**
   * @brief Retrieves information about the currently selected monitor.
   *
   * This function queries the Windows API to obtain monitor information for the selected monitor,
   * including its dimensions. The width and height are stored as fallback values for later use.
   *
   * @return true if monitor information was successfully retrieved; false otherwise.
   */
  bool get_monitor_info() {
    if (!_selected_monitor) {
      return false;
    }

    if (!GetMonitorInfo(_selected_monitor, &_monitor_info)) {
      BOOST_LOG(error) << "Failed to get monitor info";
      return false;
    }

    return true;
  }

  /**
   * @brief Creates a Windows Graphics Capture item for the selected monitor.
   *
   * Uses the IGraphicsCaptureItemInterop interface to create a GraphicsCaptureItem
   * corresponding to the currently selected monitor. This item is required for
   * initiating Windows Graphics Capture sessions.
   *
   * @param[out] item Reference to a GraphicsCaptureItem that will be set on success.
   * @return true if the GraphicsCaptureItem was successfully created; false otherwise.
   */
  bool create_graphics_capture_item(GraphicsCaptureItem &item) {
    if (!_selected_monitor) {
      return false;
    }

    auto activation_factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    HRESULT hr = activation_factory->CreateForMonitor(_selected_monitor, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item));
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to create GraphicsCaptureItem for monitor: " << hr;
      return false;
    }
    return true;
  }

  /**
   * @brief Calculates the final capture resolution based on config, monitor info, and WGC item size.
   *
   * Chooses resolution in this order:
   *
   * - Uses config width/height if valid.
   *
   * - Otherwise uses monitor logical size.
   *
   * - If WGC item size differs significantly (DPI scaling), uses WGC physical size to avoid cropping/zoom.
   *
   * @param config The configuration data with requested width/height.
   * @param config_received True if config data was received.
   * @param item The GraphicsCaptureItem for the selected monitor.
   */
  void configure_capture_resolution(const GraphicsCaptureItem &item) {
    // Get actual WGC item size to ensure we capture full desktop (fixes zoomed display issue)
    auto item_size = item.Size();
    _height = item_size.Height;
    _width = item_size.Width;
  }

  /**
   * @brief Gets the handle to the selected monitor.
   * @return HMONITOR handle of the selected monitor, or nullptr if none selected.
   */
  HMONITOR get_selected_monitor() const {
    return _selected_monitor;
  }

  /**
   * @brief Gets the configured capture width.
   * @return Capture width in pixels.
   */
  UINT get_width() const {
    return _width;
  }

  /**
   * @brief Gets the configured capture height.
   * @return Capture height in pixels.
   */
  UINT get_height() const {
    return _height;
  }
};

/**
 * @brief Shared resource management class to handle texture, memory mapping, and events.
 *
 * This class manages shared D3D11 resources for inter-process communication with the main
 * Sunshine process. It creates shared textures with keyed mutexes for synchronization
 * and provides handles for cross-process resource sharing.
 */
class SharedResourceManager {
private:
  winrt::com_ptr<ID3D11Texture2D> _shared_texture;  ///< Shared D3D11 texture for frame data
  winrt::com_ptr<IDXGIKeyedMutex> _keyed_mutex;  ///< Keyed mutex for synchronization
  winrt::handle _shared_handle;  ///< Shared handle for cross-process sharing
  UINT _width = 0;  ///< Texture width in pixels
  UINT _height = 0;  ///< Texture height in pixels

public:
  /**
   * @brief Default constructor for SharedResourceManager.
   */
  SharedResourceManager() = default;

  /**
   * @brief Deleted copy constructor to prevent resource duplication.
   */
  SharedResourceManager(const SharedResourceManager &) = delete;

  /**
   * @brief Deleted copy assignment operator to prevent resource duplication.
   */
  SharedResourceManager &operator=(const SharedResourceManager &) = delete;

  /**
   * @brief Deleted move constructor to prevent resource transfer issues.
   */
  SharedResourceManager(SharedResourceManager &&) = delete;

  /**
   * @brief Deleted move assignment operator to prevent resource transfer issues.
   */
  SharedResourceManager &operator=(SharedResourceManager &&) = delete;

  /**
   * @brief Creates a shared D3D11 texture with keyed mutex for inter-process sharing.
   *
   * @param device Pointer to the D3D11 device used for texture creation.
   * @param texture_width Width of the texture in pixels.
   * @param texture_height Height of the texture in pixels.
   * @param format DXGI format for the texture.
   * @return true if the texture was successfully created; false otherwise.
   */
  bool create_shared_texture(const winrt::com_ptr<ID3D11Device> &device, UINT texture_width, UINT texture_height, DXGI_FORMAT format) {
    _width = texture_width;
    _height = texture_height;

    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = _width;
    tex_desc.Height = _height;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = format;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    // Use NT shared handles exclusively
    tex_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

    HRESULT hr = device->CreateTexture2D(&tex_desc, nullptr, _shared_texture.put());
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to create NT shared texture: " << hr;
      return false;
    }
    return true;
  }

  /**
   * @brief Acquires a keyed mutex interface from the shared texture for synchronization.
   * @return true if the keyed mutex was successfully acquired; false otherwise.
   */
  bool create_keyed_mutex() {
    if (!_shared_texture) {
      return false;
    }

    _keyed_mutex = _shared_texture.as<IDXGIKeyedMutex>();
    if (!_keyed_mutex) {
      BOOST_LOG(error) << "Failed to get keyed mutex";
      return false;
    }
    return true;
  }

  /**
   * @brief Creates a shared handle for the texture resource.
   *
   * @return true if the handle was successfully created; false otherwise.
   */
  bool create_shared_handle() {
    if (!_shared_texture) {
      BOOST_LOG(error) << "Cannot create shared handle - no shared texture available";
      return false;
    }

    winrt::com_ptr<IDXGIResource1> dxgi_resource1 = _shared_texture.as<IDXGIResource1>();
    if (!dxgi_resource1) {
      BOOST_LOG(error) << "Failed to query DXGI resource1 interface";
      return false;
    }

    // Create the shared handle
    HRESULT hr = dxgi_resource1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, _shared_handle.put());
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to create shared handle: " << hr;
      return false;
    }
    return true;
  }

  /**
   * @brief Initializes all shared resource components: texture, keyed mutex, and handle.
   *
   * @param device Pointer to the D3D11 device used for resource creation.
   * @param texture_width Width of the texture in pixels.
   * @param texture_height Height of the texture in pixels.
   * @param format DXGI format for the texture.
   * @return true if all resources were successfully initialized; false otherwise.
   */
  bool initialize_all(const winrt::com_ptr<ID3D11Device> &device, UINT texture_width, UINT texture_height, DXGI_FORMAT format) {
    return create_shared_texture(device, texture_width, texture_height, format) &&
           create_keyed_mutex() &&
           create_shared_handle();
  }

  /**
   * @brief Gets the shared handle data for inter-process sharing.
   * @return shared_handle_data_t struct containing the shared handle and texture dimensions.
   */
  platf::dxgi::shared_handle_data_t get_shared_handle_data() const {
    platf::dxgi::shared_handle_data_t data = {};
    data.texture_handle = const_cast<HANDLE>(_shared_handle.get());
    data.width = _width;
    data.height = _height;
    return data;
  }

  /**
   * @brief Gets the underlying shared D3D11 texture pointer.
   * @return Pointer to the managed ID3D11Texture2D, or nullptr if not initialized.
   */
  const winrt::com_ptr<ID3D11Texture2D> &get_shared_texture() const {
    return _shared_texture;
  }

  /**
   * @brief Gets the keyed mutex interface com_ptr for the shared texture.
   * @return const winrt::com_ptr<IDXGIKeyedMutex>& (may be empty if not initialized).
   */
  const winrt::com_ptr<IDXGIKeyedMutex> &get_keyed_mutex() const {
    return _keyed_mutex;
  }

  /**
   * @brief Destructor for SharedResourceManager.
   * Uses RAII to automatically dispose resources
   */
  ~SharedResourceManager() = default;
};

/**
 * @brief WGC capture management class to handle frame pool, capture session, and frame processing.
 *
 * This class manages the core Windows Graphics Capture functionality including:
 * - Frame pool creation and management with dynamic buffer sizing
 * - Capture session lifecycle management
 * - Frame arrival event handling and processing
 * - Frame rate optimization and adaptive buffering
 * - Integration with shared texture resources for inter-process communication
 */
struct WgcCaptureDependencies {
  // Required devices/resources
  IDirect3DDevice winrt_device;  // WinRT Direct3D device (value-type COM handle)
  GraphicsCaptureItem graphics_item;  // Target capture item
  SharedResourceManager &resource_manager;  // Shared inter-process texture/mutex manager
  winrt::com_ptr<ID3D11DeviceContext> d3d_context;  // D3D11 context for copies
  INamedPipe &pipe;  // IPC pipe for frame-ready
};

class WgcCaptureManager {
private:
  Direct3D11CaptureFramePool _frame_pool = nullptr;  ///< WinRT frame pool for capture operations
  GraphicsCaptureSession _capture_session = nullptr;  ///< WinRT capture session for monitor/window capture
  winrt::event_token _frame_arrived_token {};  ///< Event token for frame arrival notifications
  std::optional<WgcCaptureDependencies> _deps;  ///< Dependencies for frame processing

  uint32_t _current_buffer_size = 1;  ///< Current frame buffer size for dynamic adjustment
  static constexpr uint32_t MAX_BUFFER_SIZE = 4;  ///< Maximum allowed buffer size

  std::deque<std::chrono::steady_clock::time_point> _drop_timestamps;  ///< Timestamps of recent frame drops for analysis
  std::atomic<int> _outstanding_frames {0};  ///< Number of frames currently being processed
  std::atomic<int> _peak_outstanding {0};  ///< Peak number of outstanding frames (for monitoring)
  std::chrono::steady_clock::time_point _last_quiet_start = std::chrono::steady_clock::now();  ///< Last time frame processing became quiet
  std::chrono::steady_clock::time_point _last_buffer_check = std::chrono::steady_clock::now();  ///< Last time buffer size was checked
  DXGI_FORMAT _capture_format = DXGI_FORMAT_UNKNOWN;  ///< DXGI format for captured frames
  UINT _height = 0;  ///< Capture height in pixels
  UINT _width = 0;  ///< Capture width in pixels

public:
  /**
   * @brief Constructor for WgcCaptureManager.
   *
   * Initializes the capture manager for the given dimensions and capture format and
   * stores the dependencies bundle used during capture operations.
   *
   * @param capture_format DXGI format for captured frames.
   * @param width Capture width in pixels.
   * @param height Capture height in pixels.
   * @param deps Bundle of dependencies: winrt IDirect3DDevice, GraphicsCaptureItem,
   *             reference to SharedResourceManager, D3D11 context com_ptr, and INamedPipe reference.
   */
  WgcCaptureManager(DXGI_FORMAT capture_format, UINT width, UINT height, WgcCaptureDependencies deps):
      _deps(std::move(deps)),
      _capture_format(capture_format),
      _height(height),
      _width(width) {
  }

  /**
   * @brief Destructor for WgcCaptureManager.
   *
   * Automatically cleans up capture session and frame pool resources.
   */
  ~WgcCaptureManager() noexcept {
    cleanup_capture_session();
    cleanup_frame_pool();
  }

private:
  void cleanup_capture_session() noexcept {
    try {
      if (_capture_session) {
        _capture_session.Close();
        _capture_session = nullptr;
      }
    } catch (const winrt::hresult_error &ex) {
      BOOST_LOG(error) << "Exception during _capture_session.Close(): " << ex.code() << " - " << winrt::to_string(ex.message());
    } catch (...) {
      BOOST_LOG(error) << "Unknown exception during _capture_session.Close()";
    }
  }

  void cleanup_frame_pool() noexcept {
    try {
      if (_frame_pool) {
        if (_frame_arrived_token.value != 0) {
          _frame_pool.FrameArrived(_frame_arrived_token);  // Remove handler
          _frame_arrived_token.value = 0;
        }
        _frame_pool.Close();
        _frame_pool = nullptr;
      }
    } catch (const winrt::hresult_error &ex) {
      BOOST_LOG(error) << "Exception during _frame_pool.Close(): " << ex.code() << " - " << winrt::to_string(ex.message());
    } catch (...) {
      BOOST_LOG(error) << "Unknown exception during _frame_pool.Close()";
    }
  }

public:
  /**
   * @brief Deleted copy constructor to prevent resource duplication.
   */
  WgcCaptureManager(const WgcCaptureManager &) = delete;

  /**
   * @brief Deleted copy assignment operator to prevent resource duplication.
   */
  WgcCaptureManager &operator=(const WgcCaptureManager &) = delete;

  /**
   * @brief Deleted move constructor to prevent resource transfer issues.
   */
  WgcCaptureManager(WgcCaptureManager &&) = delete;
  WgcCaptureManager &operator=(WgcCaptureManager &&) = delete;

  /**
   * @brief Recreates the frame pool with a new buffer size for dynamic adjustment.
   * @param buffer_size The number of frames to buffer (1 or 2).
   * @returns true if the frame pool was recreated successfully, false otherwise.
   */
  bool create_or_adjust_frame_pool(uint32_t buffer_size) {
    if (!_deps || !_deps->winrt_device || _capture_format == DXGI_FORMAT_UNKNOWN) {
      return false;
    }

    if (_frame_pool) {
      // Use the proper Recreate method instead of closing and re-creating
      try {
        _frame_pool.Recreate(
          _deps->winrt_device,
          (_capture_format == DXGI_FORMAT_R16G16B16A16_FLOAT) ? DirectXPixelFormat::R16G16B16A16Float : DirectXPixelFormat::B8G8R8A8UIntNormalized,
          buffer_size,
          SizeInt32 {static_cast<int32_t>(_width), static_cast<int32_t>(_height)}
        );

        _current_buffer_size = buffer_size;
        BOOST_LOG(info) << "Frame pool recreated with buffer size: " << buffer_size;
        return true;
      } catch (const winrt::hresult_error &ex) {
        BOOST_LOG(error) << "Failed to recreate frame pool: " << ex.code() << " - " << winrt::to_string(ex.message());
        return false;
      }
    } else {
      // Initial creation case - create new frame pool
      _frame_pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
        _deps->winrt_device,
        (_capture_format == DXGI_FORMAT_R16G16B16A16_FLOAT) ? DirectXPixelFormat::R16G16B16A16Float : DirectXPixelFormat::B8G8R8A8UIntNormalized,
        buffer_size,
        SizeInt32 {static_cast<int32_t>(_width), static_cast<int32_t>(_height)}
      );

      if (_frame_pool) {
        _current_buffer_size = buffer_size;
        BOOST_LOG(info) << "Frame pool created with buffer size: " << buffer_size;
        return true;
      }
    }

    return false;
  }

  /**
   * @brief Processes a frame when the frame arrived event is triggered.
   * @param sender The frame pool that triggered the event.
   */
  void process_frame(Direct3D11CaptureFramePool const &sender) {
    if (auto frame = sender.TryGetNextFrame(); frame) {
      // Frame successfully retrieved
      auto surface = frame.Surface();

      try {
        // Get frame timing information from the WGC frame
        uint64_t frame_qpc = frame.SystemRelativeTime().count();
        process_surface_to_texture(surface, frame_qpc);
      } catch (const winrt::hresult_error &ex) {
        // Log error
        BOOST_LOG(error) << "WinRT error in frame processing: " << ex.code() << " - " << winrt::to_string(ex.message());
      }
    } else {
      // Frame drop detected - record timestamp for sliding window analysis
      auto now = std::chrono::steady_clock::now();
      _drop_timestamps.push_back(now);

      BOOST_LOG(info) << "Frame drop detected (total drops in 5s window: " << _drop_timestamps.size() << ")";
    }

    // Decrement outstanding frame count (always called whether frame retrieved or not)
    --_outstanding_frames;

    // Check if we need to adjust frame buffer size
    check_and_adjust_frame_buffer();
  }

private:
  /**
   * @brief Prunes old frame drop timestamps from the sliding window.
   * @param now Current timestamp for comparison.
   */
  void prune_old_drop_timestamps(const std::chrono::steady_clock::time_point &now) {
    while (!_drop_timestamps.empty() && now - _drop_timestamps.front() > std::chrono::seconds(5)) {
      _drop_timestamps.pop_front();
    }
  }

  /**
   * @brief Checks if buffer size should be increased due to recent frame drops.
   * @param now Current timestamp.
   * @return true if buffer was increased, false otherwise.
   */
  bool try_increase_buffer_size(const std::chrono::steady_clock::time_point &now) {
    if (_drop_timestamps.size() >= 2 && _current_buffer_size < MAX_BUFFER_SIZE) {
      uint32_t new_buffer_size = _current_buffer_size + 1;
      BOOST_LOG(info) << "Detected " << _drop_timestamps.size() << " frame drops in 5s window, increasing buffer from "
                      << _current_buffer_size << " to " << new_buffer_size;
      create_or_adjust_frame_pool(new_buffer_size);
      _drop_timestamps.clear();  // Reset after adjustment
      _peak_outstanding = 0;  // Reset peak tracking
      _last_quiet_start = now;  // Reset quiet timer
      return true;
    }
    return false;
  }

  /**
   * @brief Checks if buffer size should be decreased due to sustained quiet period.
   * @param now Current timestamp.
   * @return true if buffer was decreased, false otherwise.
   */
  bool try_decrease_buffer_size(const std::chrono::steady_clock::time_point &now) {
    bool is_quiet = _drop_timestamps.empty() &&
                    _peak_outstanding.load() <= static_cast<int>(_current_buffer_size) - 1;

    if (!is_quiet) {
      _last_quiet_start = now;  // Reset quiet timer
      return false;
    }

    // Check if we've been quiet for 30 seconds
    if (now - _last_quiet_start >= std::chrono::seconds(30) && _current_buffer_size > 1) {
      uint32_t new_buffer_size = _current_buffer_size - 1;
      BOOST_LOG(info) << "Sustained quiet period (30s) with peak occupancy " << _peak_outstanding.load()
                      << " ≤ " << (_current_buffer_size - 1) << ", decreasing buffer from "
                      << _current_buffer_size << " to " << new_buffer_size;
      create_or_adjust_frame_pool(new_buffer_size);
      _peak_outstanding = 0;  // Reset peak tracking
      _last_quiet_start = now;  // Reset quiet timer
      return true;
    }

    return false;
  }

  /**
   * @brief Copies the captured surface to the shared texture and notifies the main process.
   * @param surface The captured D3D11 surface.
   * @param frame_qpc The QPC timestamp from when the frame was captured.
   */
  void process_surface_to_texture(winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface surface, uint64_t frame_qpc) {
    if (!_deps) {
      return;
    }

    // Get DXGI access
    winrt::com_ptr<IDirect3DDxgiInterfaceAccess> ia;
    if (FAILED(winrt::get_unknown(surface)->QueryInterface(__uuidof(IDirect3DDxgiInterfaceAccess), ia.put_void()))) {
      BOOST_LOG(error) << "Failed to query IDirect3DDxgiInterfaceAccess";
      return;
    }

    // Get underlying texture
    winrt::com_ptr<ID3D11Texture2D> frame_tex;
    if (FAILED(ia->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<::IUnknown **>(frame_tex.put_void())))) {
      BOOST_LOG(error) << "Failed to get ID3D11Texture2D from interface";
      return;
    }

    HRESULT hr = _deps->resource_manager.get_keyed_mutex()->AcquireSync(0, 200);
    if (hr != S_OK) {
      BOOST_LOG(error) << "Failed to acquire mutex key 0: " << std::format(": 0x{:08X}", hr);
      return;
    }

    // Copy frame data and release mutex
    _deps->d3d_context->CopyResource(_deps->resource_manager.get_shared_texture().get(), frame_tex.get());
    _deps->resource_manager.get_keyed_mutex()->ReleaseSync(0);

    // Send frame ready message with QPC timing data
    frame_ready_msg_t frame_msg;
    frame_msg.frame_qpc = frame_qpc;

    std::span<const uint8_t> msg_span(reinterpret_cast<const uint8_t *>(&frame_msg), sizeof(frame_msg));
    _deps->pipe.send(msg_span, 5000);
  }

public:
  /**
   * @brief Creates a capture session for the specified capture item.
   * @returns true if the session was created successfully, false otherwise.
   */
  bool create_capture_session() {
    if (!_frame_pool) {
      return false;
    }

    _frame_arrived_token = _frame_pool.FrameArrived([this](Direct3D11CaptureFramePool const &sender, winrt::Windows::Foundation::IInspectable const &) {
      ++_outstanding_frames;
      _peak_outstanding = std::max(_peak_outstanding.load(), _outstanding_frames.load());
      process_frame(sender);
    });

    _capture_session = _frame_pool.CreateCaptureSession(_deps->graphics_item);
    _capture_session.IsBorderRequired(false);

    // Technically this is not required for users that have 24H2, but there's really no functional difference.
    // So instead of coding out a version check, we'll just set it for everyone.
    if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"MinUpdateInterval")) {
      _capture_session.MinUpdateInterval(winrt::Windows::Foundation::TimeSpan {10000});
    }

    return true;
  }

  /**
   * @brief Starts the capture session if available.
   */
  void start_capture() const {
    if (_capture_session) {
      _capture_session.StartCapture();
      BOOST_LOG(info) << "Helper process started. Capturing frames using WGC...";
    }
  }

private:
  void check_and_adjust_frame_buffer() {
    auto now = std::chrono::steady_clock::now();

    // Check every 1 second for buffer adjustments
    if (auto time_since_last_check = std::chrono::duration_cast<std::chrono::milliseconds>(now - _last_buffer_check);
        time_since_last_check.count() < 1000) {
      return;
    }

    _last_buffer_check = now;

    // 1) Prune old drop timestamps (older than 5 seconds)
    prune_old_drop_timestamps(now);

    // 2) Try to increase buffer count if we have recent drops
    if (try_increase_buffer_size(now)) {
      return;
    }

    // 3) Try to decrease buffer count if we've been quiet
    try_decrease_buffer_size(now);
  }
};

/**
 * @brief Callback procedure for desktop switch events.
 *
 * This function handles EVENT_SYSTEM_DESKTOPSWITCH events to detect when the system
 * transitions to or from secure desktop mode (such as UAC prompts or lock screens).
 * When a secure desktop is detected, it notifies the main process via the communication pipe.
 *
 * @param h_win_event_hook Handle to the event hook (unused).
 * @param event The event type that occurred.
 * @param hwnd Handle to the window (unused).
 * @param id_object Object identifier (unused).
 * @param id_child Child object identifier (unused).
 * @param dw_event_thread Thread that generated the event (unused).
 * @param dwms_event_time Time the event occurred (unused).
 */
void CALLBACK desktop_switch_hook_proc(HWINEVENTHOOK /*h_win_event_hook*/, DWORD event, HWND /*hwnd*/, LONG /*id_object*/, LONG /*id_child*/, DWORD /*dw_event_thread*/, DWORD /*dwms_event_time*/) {
  if (event == EVENT_SYSTEM_DESKTOPSWITCH) {
    BOOST_LOG(info) << "Desktop switch detected!";

    bool secure_desktop_active = platf::dxgi::is_secure_desktop_active();
    BOOST_LOG(info) << "Desktop switch - Secure desktop: " << (secure_desktop_active ? "YES" : "NO");

    if (secure_desktop_active && !g_secure_desktop_detected) {
      BOOST_LOG(info) << "Secure desktop detected - sending notification to main process";
      g_secure_desktop_detected = true;

      // Send notification to main process
      if (auto pipe = g_communication_pipe_weak.lock()) {
        if (pipe->is_connected()) {
          uint8_t msg = SECURE_DESKTOP_MSG;
          pipe->send(std::span<const uint8_t>(&msg, 1));
          BOOST_LOG(info) << "Sent secure desktop notification to main process (0x02)";
        }
      }
    } else if (!secure_desktop_active && g_secure_desktop_detected) {
      BOOST_LOG(info) << "Returned to normal desktop";
      g_secure_desktop_detected = false;
    }
  }
}

/**
 * @brief Helper function to get the system temp directory for log files.
 *
 * Constructs a path to a log file in the system temporary directory.
 * If the system temp path cannot be obtained, falls back to the current directory.
 * Handles Unicode to UTF-8 conversion for the path string.
 *
 * @return Path to the log file as a UTF-8 string.
 */
std::string get_temp_log_path() {
  std::wstring temp_path(MAX_PATH, L'\0');
  if (auto len = GetTempPathW(MAX_PATH, temp_path.data()); len == 0 || len > MAX_PATH) {
    // fallback to current directory if temp path fails
    return "sunshine_wgc_helper.log";
  }
  temp_path.resize(wcslen(temp_path.data()));  // Remove null padding
  std::wstring wlog = temp_path + L"sunshine_wgc_helper.log";
  // Convert to UTF-8
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, wlog.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string log_file(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, wlog.c_str(), -1, &log_file[0], size_needed, nullptr, nullptr);
  // Remove trailing null
  if (!log_file.empty() && log_file.back() == '\0') {
    log_file.pop_back();
  }
  return log_file;
}

/**
 * @brief Helper function to handle IPC messages from the main process.
 *
 * Processes incoming messages from the main Sunshine process via named pipe:
 *
 * - Configuration messages: Receives and stores config_data_t structure with display settings
 *
 * @param message The received message bytes from the named pipe.
 *
 */
void handle_ipc_message(std::span<const uint8_t> message) {
  // Handle config data message
  if (message.size() == sizeof(platf::dxgi::config_data_t) && !g_config_received) {
    memcpy(&g_config, message.data(), sizeof(platf::dxgi::config_data_t));
    g_config_received = true;
    // If log_level in config differs from current, update log filter
    if (INITIAL_LOG_LEVEL != g_config.log_level) {
      // Update log filter to new log level
      boost::log::core::get()->set_filter(
        severity >= g_config.log_level
      );
      BOOST_LOG(info) << "Log level updated from config: " << g_config.log_level;
    }
    BOOST_LOG(info) << "Received config data: hdr: " << g_config.dynamic_range
                    << ", display: '" << winrt::to_string(g_config.display_name) << "'"
                    << ", adapter LUID: " << std::hex << g_config.adapter_luid.HighPart
                    << ":" << g_config.adapter_luid.LowPart << std::dec;
  }
}

/**
 * @brief Helper function to setup the communication pipe with the main process.
 *
 * Configures the AsyncNamedPipe with callback functions for message handling:
 * - on_message: Delegates to handle_ipc_message() for processing received data
 * - on_error: Handles pipe communication errors (currently empty handler)
 *
 * @param pipe Reference to the AsyncNamedPipe to configure.
 *
 * @return true if the pipe was successfully configured and started, false otherwise.
 */
bool setup_pipe_callbacks(AsyncNamedPipe &pipe) {
  auto on_message = [](std::span<const uint8_t> message) {
    handle_ipc_message(message);
  };

  auto on_error = [](std::string_view /*err*/) {
    // Error handler, intentionally left empty or log as needed
  };

  return pipe.start(on_message, on_error);
}

/**
 * @brief Helper function to process window messages for desktop hooks.
 *
 * @param shutdown_requested Reference to shutdown flag that may be set if WM_QUIT is received.
 * @return true if messages were processed, false if shutdown was requested.
 */
bool process_window_messages(bool &shutdown_requested) {
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
    if (msg.message == WM_QUIT) {
      shutdown_requested = true;
      return false;
    }
  }
  return true;
}

/**
 * @brief Helper function to setup desktop switch hook for secure desktop detection.
 *
 * @return true if the hook was successfully installed, false otherwise.
 */
bool setup_desktop_switch_hook() {
  BOOST_LOG(info) << "Setting up desktop switch hook...";

  HWINEVENTHOOK raw_hook = SetWinEventHook(
    EVENT_SYSTEM_DESKTOPSWITCH,
    EVENT_SYSTEM_DESKTOPSWITCH,
    nullptr,
    desktop_switch_hook_proc,
    0,
    0,
    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
  );

  if (!raw_hook) {
    BOOST_LOG(error) << "Failed to set up desktop switch hook: " << GetLastError();
    return false;
  }

  g_desktop_switch_hook.reset(raw_hook);
  BOOST_LOG(info) << "Desktop switch hook installed successfully";
  return true;
}

/**
 * @brief Main application entry point for the Windows Graphics Capture helper process.
 *
 * This standalone executable serves as a helper process for Sunshine's Windows Graphics
 * Capture functionality. The main function performs these key operations:
 *
 * 1. **System Initialization**: Sets up logging, DPI awareness, thread priority, and MMCSS
 * 2. **IPC Setup**: Establishes named pipe communication with the main Sunshine process
 * 3. **D3D11 Device Creation**: Creates hardware-accelerated D3D11 device and WinRT interop
 * 4. **Monitor Selection**: Identifies and configures the target monitor for capture
 * 5. **Shared Resource Creation**: Sets up shared D3D11 texture for inter-process frame sharing
 * 6. **WGC Setup**: Initializes Windows Graphics Capture frame pool and capture session
 * 7. **Desktop Monitoring**: Sets up hooks to detect secure desktop transitions
 * 8. **Main Loop**: Processes window messages and handles capture events until shutdown
 *
 * @param argc Number of command line arguments. Expects at least 2 arguments.
 * @param argv Array of command line argument strings. argv[1] should contain the pipe name GUID.
 * @return 0 on successful completion, 1 on initialization failure.
 */
int main(int argc, char *argv[]) {
  // Set up default config and log level
  auto log_deinit = logging::init(2, get_temp_log_path());

  // Check command line arguments for pipe names
  if (argc < 3) {
    BOOST_LOG(error) << "Usage: " << argv[0] << " <pipe_name_guid> <frame_queue_pipe_name_guid>";
    return 1;
  }

  std::string pipe_name = argv[1];
  std::string frame_queue_pipe_name = argv[2];
  BOOST_LOG(info) << "Using pipe name: " << pipe_name;
  BOOST_LOG(info) << "Using frame queue pipe name: " << frame_queue_pipe_name;

  // Initialize system settings (DPI awareness, thread priority, MMCSS)
  SystemInitializer system_initializer;
  if (!system_initializer.initialize_all()) {
    BOOST_LOG(error) << "System initialization failed, exiting...";
    return 1;
  }

  // Debug: Verify system settings
  BOOST_LOG(info) << "DPI awareness set: " << (system_initializer.is_dpi_awareness_set() ? "YES" : "NO");
  BOOST_LOG(info) << "Thread priority set: " << (system_initializer.is_thread_priority_set() ? "YES" : "NO");
  BOOST_LOG(info) << "GPU scheduling priority set: " << (system_initializer.is_gpu_priority_set() ? "YES" : "NO");
  BOOST_LOG(info) << "MMCSS characteristics set: " << (system_initializer.is_mmcss_characteristics_set() ? "YES" : "NO");

  BOOST_LOG(info) << "Starting Windows Graphics Capture helper process...";

  // Create named pipe for communication with main process using provided pipe name
  AnonymousPipeFactory pipe_factory;

  auto comm_pipe = pipe_factory.create_client(pipe_name);
  auto frame_queue_pipe = pipe_factory.create_client(frame_queue_pipe_name);
  auto pipe_shared = std::make_shared<AsyncNamedPipe>(std::move(comm_pipe));
  g_communication_pipe_weak = pipe_shared;  // Store weak reference for desktop hook callback

  if (!setup_pipe_callbacks(*pipe_shared)) {
    BOOST_LOG(error) << "Failed to start communication pipe";
    return 1;
  }

  constexpr int max_wait_ms = 5000;
  constexpr int poll_interval_ms = 10;
  int waited_ms = 0;
  while (!g_config_received && waited_ms < max_wait_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    waited_ms += poll_interval_ms;
  }

  // Create D3D11 device and context using the same adapter as the main process
  D3D11DeviceManager d3d11_manager;
  if (!d3d11_manager.initialize_all(g_config.adapter_luid)) {
    BOOST_LOG(error) << "D3D11 device initialization failed, exiting...";
    return 1;
  }

  // Monitor management
  DisplayManager display_manager;
  if (!display_manager.select_monitor(g_config)) {
    BOOST_LOG(error) << "Monitor selection failed, exiting...";
    return 1;
  }

  if (!display_manager.get_monitor_info()) {
    BOOST_LOG(error) << "Failed to get monitor info, exiting...";
    return 1;
  }

  // Create GraphicsCaptureItem for monitor using interop
  GraphicsCaptureItem item = nullptr;
  if (!display_manager.create_graphics_capture_item(item)) {
    return 1;
  }

  // Calculate final resolution based on config and monitor info
  display_manager.configure_capture_resolution(item);

  // Choose format based on config.dynamic_range
  DXGI_FORMAT capture_format = DXGI_FORMAT_B8G8R8A8_UNORM;
  if (g_config_received && g_config.dynamic_range) {
    capture_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  }

  // Create shared resource manager for texture, keyed mutex, and metadata
  SharedResourceManager shared_resource_manager;
  if (!shared_resource_manager.initialize_all(d3d11_manager.get_device(), display_manager.get_width(), display_manager.get_height(), capture_format)) {
    return 1;
  }

  // Send shared handle data via named pipe to main process
  platf::dxgi::shared_handle_data_t handle_data = shared_resource_manager.get_shared_handle_data();
  BOOST_LOG(info) << "Prepared shared handle message - Size: " << sizeof(handle_data) << " bytes, Handle: 0x" << std::hex << reinterpret_cast<uintptr_t>(handle_data.texture_handle) << std::dec;
  std::span<const uint8_t> handle_message(reinterpret_cast<const uint8_t *>(&handle_data), sizeof(handle_data));

  // Wait for connection and send the handle data
  BOOST_LOG(info) << "Waiting for main process to connect...";
  while (!pipe_shared->is_connected()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  BOOST_LOG(info) << "Connected! Sending duplicated handle data...";
  pipe_shared->send(handle_message);
  BOOST_LOG(info) << "Duplicated handle data sent successfully to main process";

  // Create dependencies for capture manager
  WgcCaptureDependencies deps {
    d3d11_manager.get_winrt_device(),
    item,
    shared_resource_manager,
    d3d11_manager.get_context(),
    *frame_queue_pipe
  };

  // Create WGC capture manager
  WgcCaptureManager wgc_capture_manager {capture_format, display_manager.get_width(), display_manager.get_height(), std::move(deps)};
  if (!wgc_capture_manager.create_or_adjust_frame_pool(1)) {
    BOOST_LOG(error) << "Failed to create frame pool";
    return 1;
  }

  if (!wgc_capture_manager.create_capture_session()) {
    BOOST_LOG(error) << "Failed to create capture session";
    return 1;
  }

  // Set up desktop switch hook for secure desktop detection
  setup_desktop_switch_hook();

  wgc_capture_manager.start_capture();

  // Main message loop
  bool shutdown_requested = false;
  while (pipe_shared->is_connected() && !shutdown_requested) {
    // Process window messages and check for shutdown
    if (!process_window_messages(shutdown_requested)) {
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Reduced from 5ms for lower IPC jitter
  }

  BOOST_LOG(info) << "Main process disconnected, shutting down...";

  pipe_shared->stop();

  // Flush logs before exit
  boost::log::core::get()->flush();

  BOOST_LOG(info) << "WGC Helper process terminated";

  return 0;
}
