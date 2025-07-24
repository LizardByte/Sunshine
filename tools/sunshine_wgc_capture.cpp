/**
 * @file tools/sunshine_wgc_capture.cpp
 * @brief Windows Graphics Capture helper process for Sunshine.
 *
 * This standalone executable provides Windows Graphics Capture functionality
 * for the main Sunshine streaming process. It runs as a separate process to
 * isolate WGC operations and handle secure desktop scenarios. The helper
 * communicates with the main process via named pipes and shared D3D11 textures.
 */

#define WIN32_LEAN_AND_MEAN
#include "src/logging.h"
#include "src/platform/windows/wgc/misc_utils.h"
#include "src/platform/windows/wgc/shared_memory.h"
#include "src/utility.h"  // For RAII utilities

#include <atomic>
#include <boost/format.hpp>
#include <chrono>
#include <condition_variable>
#include <d3d11.h>
#include <deque>
#include <dxgi1_2.h>
#include <inspectable.h>  // For IInspectable
#include <iomanip>  // for std::fixed, std::setprecision
#include <iomanip>
#include <iostream>
#include <mutex>
#include <psapi.h>  // For GetModuleBaseName
#include <queue>
#include <shellscalingapi.h>  // For DPI awareness
#include <shlobj.h>  // For SHGetFolderPathW and CSIDL_DESKTOPDIRECTORY
#include <string>
#include <thread>
#include <tlhelp32.h>  // For process enumeration
#include <windows.graphics.capture.interop.h>
#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>  // For ApiInformation
#include <winrt/windows.Graphics.Capture.h>
#include <winrt/windows.Graphics.Directx.Direct3d11.h>
#include <winrt/Windows.System.h>

// Gross hack to work around MINGW-packages#22160
#define ____FIReference_1_boolean_INTERFACE_DEFINED__

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
using namespace winrt::Windows::System;
using namespace std::literals;
using namespace platf::dxgi;

/**
 * @brief Initial log level for the helper process.
 */
const int INITIAL_LOG_LEVEL = 2;

/**
 * @brief Global configuration data received from the main process.
 */
platf::dxgi::config_data_t g_config = {0, 0, L""};

/**
 * @brief Flag indicating whether configuration data has been received from main process.
 */
bool g_config_received = false;

/**
 * @brief Global handle for frame metadata shared memory mapping.
 */
safe_handle g_metadata_mapping = nullptr;

/**
 * @brief Global communication pipe for sending session closed notifications.
 */
AsyncNamedPipe *g_communication_pipe = nullptr;

/**
 * @brief Global Windows event hook for desktop switch detection.
 */
safe_winevent_hook g_desktop_switch_hook = nullptr;

/**
 * @brief Flag indicating if a secure desktop has been detected.
 */
bool g_secure_desktop_detected = false;

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
    typedef BOOL(WINAPI * set_process_dpi_awareness_context_func)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = GetModuleHandleA("user32.dll")) {
      auto set_dpi_context_func = (set_process_dpi_awareness_context_func) GetProcAddress(user32, "SetProcessDpiAwarenessContext");
      if (set_dpi_context_func) {
        if (set_dpi_context_func((DPI_AWARENESS_CONTEXT) -4)) {
          _dpi_awareness_set = true;
          return true;
        }
      }
    }
    if (FAILED(SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE))) {
      BOOST_LOG(warning) << "Failed to set DPI awareness, display scaling issues may occur";
      return false;
    } else {
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
    _mmcss_handle.reset(raw_handle);
    _mmcss_characteristics_set = true;
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
   * - MMCSS characteristics setup
   * - WinRT apartment initialization
   *
   * @return true if all initialization steps succeeded, false if any failed.
   */
  bool initialize_all() {
    bool success = true;
    success &= initialize_dpi_awareness();
    success &= initialize_thread_priority();
    success &= initialize_mmcss_characteristics();
    success &= initialize_winrt_apartment();
    return success;
  }

  /**
   * @brief Cleans up MMCSS resources.
   *
   * Resets the MMCSS handle and updates internal state flags.
   * Safe to call multiple times.
   */
  void cleanup() noexcept {
    _mmcss_handle.reset();
    _mmcss_characteristics_set = false;
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
   * @brief Destructor for SystemInitializer.
   *
   * Calls cleanup() to release MMCSS resources.
   */
  ~SystemInitializer() noexcept {
    cleanup();
  }
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
  safe_com_ptr<ID3D11Device> _device = nullptr;  ///< D3D11 device for graphics operations
  safe_com_ptr<ID3D11DeviceContext> _context = nullptr;  ///< D3D11 device context for rendering
  D3D_FEATURE_LEVEL _feature_level;  ///< D3D feature level supported by the device
  winrt::com_ptr<IDXGIDevice> _dxgi_device;  ///< DXGI device interface for WinRT interop
  winrt::com_ptr<::IDirect3DDevice> _interop_device;  ///< Intermediate interop device
  IDirect3DDevice _winrt_device = nullptr;  ///< WinRT Direct3D device for capture integration

public:
  /**
   * @brief Creates a D3D11 device and context for graphics operations.
   *
   * Creates a hardware-accelerated D3D11 device using default settings.
   * The device is used for texture operations and WinRT interop.
   *
   * @return true if device creation succeeded, false otherwise.
   */
  bool create_device() {
    ID3D11Device *raw_device = nullptr;
    ID3D11DeviceContext *raw_context = nullptr;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &raw_device, &_feature_level, &raw_context);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to create D3D11 device";
      return false;
    }

    _device.reset(raw_device);
    _context.reset(raw_context);
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

    HRESULT hr = _device->QueryInterface(__uuidof(IDXGIDevice), _dxgi_device.put_void());
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to get DXGI device";
      return false;
    }

    hr = CreateDirect3D11DeviceFromDXGIDevice(_dxgi_device.get(), reinterpret_cast<::IInspectable **>(winrt::put_abi(_interop_device)));
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
   * Calls create_device() to create a D3D11 device and context, then create_winrt_interop() to create the WinRT interop device.
   *
   * @return true if both device and interop device are successfully created, false otherwise.
   * @sideeffect Allocates and stores device, context, and WinRT device handles.
   */
  bool initialize_all() {
    return create_device() && create_winrt_interop();
  }

  /**
   * @brief Cleans up D3D11 device and context resources.
   *
   * Resets the device and context smart pointers. Most cleanup is handled by RAII.
   * Safe to call multiple times.
   * @sideeffect Releases device and context COM objects.
   */
  void cleanup() noexcept {
    // RAII handles cleanup automatically
    _context.reset();
    _device.reset();
  }

  /**
   * @brief Gets the underlying D3D11 device pointer.
   *
   * @return Pointer to the managed ID3D11Device, or nullptr if not initialized.
   */
  ID3D11Device *get_device() {
    return _device.get();
  }

  /**
   * @brief Gets the underlying D3D11 device context pointer.
   *
   * @return Pointer to the managed ID3D11DeviceContext, or nullptr if not initialized.
   */
  ID3D11DeviceContext *get_context() {
    return _context.get();
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
   * Calls cleanup() to release device and context resources.
   */
  ~D3D11DeviceManager() noexcept {
    cleanup();
  }
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
  UINT _fallback_width = 0;  ///< Fallback width if configuration fails
  UINT _fallback_height = 0;  ///< Fallback height if configuration fails
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

      auto enum_proc = +[](HMONITOR h_mon, HDC, RECT *, LPARAM l_param) {
        auto *data = static_cast<EnumData *>(reinterpret_cast<void *>(l_param));
        if (MONITORINFOEXW m_info = {sizeof(MONITORINFOEXW)}; GetMonitorInfoW(h_mon, &m_info) && wcsncmp(m_info.szDevice, data->target_name, 32) == 0) {
          data->found_monitor = h_mon;
          return FALSE;  // Stop enumeration
        }
        return TRUE;
      };
      EnumDisplayMonitors(nullptr, nullptr, enum_proc, reinterpret_cast<LPARAM>(&enum_data));
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
  safe_com_ptr<ID3D11Texture2D> _shared_texture = nullptr;  ///< Shared D3D11 texture for frame data
  safe_com_ptr<IDXGIKeyedMutex> _keyed_mutex = nullptr;  ///< Keyed mutex for synchronization
  HANDLE _shared_handle = nullptr;  ///< Handle for cross-process sharing
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
   *
   */
  bool create_shared_texture(ID3D11Device *device, UINT texture_width, UINT texture_height, DXGI_FORMAT format) {
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
    tex_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    ID3D11Texture2D *raw_texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&tex_desc, nullptr, &raw_texture);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to create shared texture";
      return false;
    }
    _shared_texture.reset(raw_texture);
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

    IDXGIKeyedMutex *raw_mutex = nullptr;
    HRESULT hr = _shared_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **) (&raw_mutex));
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to get keyed mutex";
      return false;
    }
    _keyed_mutex.reset(raw_mutex);
    return true;
  }

  /**
   * @brief Obtains a shared handle for the texture to allow sharing with other processes.
   * @return true if the shared handle was successfully obtained; false otherwise.
   */
  bool create_shared_handle() {
    if (!_shared_texture) {
      return false;
    }

    IDXGIResource *dxgi_resource = nullptr;
    HRESULT hr = _shared_texture->QueryInterface(__uuidof(IDXGIResource), (void **) (&dxgi_resource));
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to query DXGI resource interface";
      return false;
    }

    hr = dxgi_resource->GetSharedHandle(&_shared_handle);
    dxgi_resource->Release();
    if (FAILED(hr) || !_shared_handle) {
      BOOST_LOG(error) << "Failed to get shared handle";
      return false;
    }

    BOOST_LOG(info) << "Created shared texture: " << _width << "x" << _height
                    << ", handle: " << std::hex << reinterpret_cast<std::uintptr_t>(_shared_handle) << std::dec;
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
   *
   */
  bool initialize_all(ID3D11Device *device, UINT texture_width, UINT texture_height, DXGI_FORMAT format) {
    return create_shared_texture(device, texture_width, texture_height, format) &&
           create_keyed_mutex() &&
           create_shared_handle();
  }

  /**
   * @brief Gets the shared handle data (handle, width, height) for inter-process sharing.
   * @return shared_handle_data_t struct containing the handle and texture dimensions.
   */
  platf::dxgi::shared_handle_data_t get_shared_handle_data() const {
    return {_shared_handle, _width, _height};
  }

  /**
   * @brief Cleans up all managed shared resources. Resets all internal handles and pointers. Safe to call multiple times.
   */
  void cleanup() noexcept {
    // RAII handles cleanup automatically
    _keyed_mutex.reset();
    _shared_texture.reset();
  }

  /**
   * @brief Gets the underlying shared D3D11 texture pointer.
   * @return Pointer to the managed ID3D11Texture2D, or nullptr if not initialized.
   */
  ID3D11Texture2D *get_shared_texture() {
    return _shared_texture.get();
  }

  /**
   * @brief Gets the keyed mutex interface for the shared texture.
   * @return Pointer to the managed IDXGIKeyedMutex, or nullptr if not initialized.
   */
  IDXGIKeyedMutex *get_keyed_mutex() {
    return _keyed_mutex.get();
  }

  /**
   * @brief Destructor for SharedResourceManager.\
   * Calls cleanup() to release all managed resources. All resources are managed using RAII.
   */
  ~SharedResourceManager() noexcept {
    cleanup();
  }
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
class WgcCaptureManager {
private:
  Direct3D11CaptureFramePool _frame_pool = nullptr;  ///< WinRT frame pool for capture operations
  GraphicsCaptureSession _capture_session = nullptr;  ///< WinRT capture session for monitor/window capture
  winrt::event_token _frame_arrived_token {};  ///< Event token for frame arrival notifications
  SharedResourceManager *_resource_manager = nullptr;  ///< Pointer to shared resource manager
  ID3D11DeviceContext *_d3d_context = nullptr;  ///< D3D11 context for texture operations
  AsyncNamedPipe *_pipe = nullptr;  ///< Communication pipe with main process

  uint32_t _current_buffer_size = 1;  ///< Current frame buffer size for dynamic adjustment
  static constexpr uint32_t MAX_BUFFER_SIZE = 4;  ///< Maximum allowed buffer size

  std::deque<std::chrono::steady_clock::time_point> _drop_timestamps;  ///< Timestamps of recent frame drops for analysis
  std::atomic<int> _outstanding_frames {0};  ///< Number of frames currently being processed
  std::atomic<int> _peak_outstanding {0};  ///< Peak number of outstanding frames (for monitoring)
  std::chrono::steady_clock::time_point _last_quiet_start = std::chrono::steady_clock::now();  ///< Last time frame processing became quiet
  std::chrono::steady_clock::time_point _last_buffer_check = std::chrono::steady_clock::now();  ///< Last time buffer size was checked
  IDirect3DDevice _winrt_device = nullptr;  ///< WinRT Direct3D device for capture operations
  DXGI_FORMAT _capture_format = DXGI_FORMAT_UNKNOWN;  ///< DXGI format for captured frames
  UINT _height = 0;  ///< Capture height in pixels
  UINT _width = 0;  ///< Capture width in pixels
  GraphicsCaptureItem _graphics_item = nullptr;  ///< WinRT graphics capture item (monitor/window)

  static std::chrono::steady_clock::time_point _last_delivery_time;  ///< Last frame delivery timestamp (static for all instances)
  static bool _first_frame;  ///< Flag indicating if this is the first frame (static for all instances)
  static uint32_t _delivery_count;  ///< Total number of frames delivered (static for all instances)
  static std::chrono::milliseconds _total_delivery_time;  ///< Cumulative delivery time (static for all instances)

public:
  /**
   * @brief Constructor for WgcCaptureManager.
   *
   * Initializes the capture manager with the required WinRT device, capture format,
   * dimensions, and graphics capture item. Sets up internal state for frame processing.
   *
   * @param winrt_device WinRT Direct3D device for capture operations.
   * @param capture_format DXGI format for captured frames.
   * @param width Capture width in pixels.
   * @param height Capture height in pixels.
   * @param item Graphics capture item representing the monitor or window to capture.
   */
  WgcCaptureManager(IDirect3DDevice winrt_device, DXGI_FORMAT capture_format, UINT width, UINT height, GraphicsCaptureItem item) {
    _winrt_device = winrt_device;
    _capture_format = capture_format;
    _width = width;
    _height = height;
    _graphics_item = item;
  }

  /**
   * @brief Destructor for WgcCaptureManager.
   *
   * Automatically calls cleanup() to release all managed resources.
   */
  ~WgcCaptureManager() noexcept {
    cleanup();
  }

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
    if (!_winrt_device || _capture_format == DXGI_FORMAT_UNKNOWN) {
      return false;
    }

    if (_frame_pool) {
      // Use the proper Recreate method instead of closing and re-creating
      try {
        _frame_pool.Recreate(
          _winrt_device,
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
        _winrt_device,
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
   * @brief Attaches the frame arrived event handler for frame processing.
   * @param res_mgr Pointer to the shared resource manager.
   * @param context Pointer to the D3D11 device context.
   * @param pipe Pointer to the async named pipe for IPC.
   */
  void attach_frame_arrived_handler(SharedResourceManager *res_mgr, ID3D11DeviceContext *context, AsyncNamedPipe *pipe) {
    _resource_manager = res_mgr;
    _d3d_context = context;
    _pipe = pipe;

    _frame_arrived_token = _frame_pool.FrameArrived([this](Direct3D11CaptureFramePool const &sender, winrt::Windows::Foundation::IInspectable const &) {
      // Track outstanding frames for buffer occupancy monitoring
      ++_outstanding_frames;
      _peak_outstanding = std::max(_peak_outstanding.load(), _outstanding_frames.load());

      process_frame(sender);
    });
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
        process_surface_to_texture(surface);
      } catch (const winrt::hresult_error &ex) {
        // Log error
        BOOST_LOG(error) << "WinRT error in frame processing: " << ex.code() << " - " << winrt::to_string(ex.message());
      }
      surface.Close();
      frame.Close();
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

  /**
   * @brief Copies the captured surface to the shared texture and notifies the main process.
   * @param surface The captured D3D11 surface.
   */
  void process_surface_to_texture(winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface surface) {
    if (!_resource_manager || !_d3d_context || !_pipe) {
      return;
    }

    winrt::com_ptr<IDirect3DDxgiInterfaceAccess> interface_access;
    HRESULT hr = surface.as<::IUnknown>()->QueryInterface(__uuidof(IDirect3DDxgiInterfaceAccess), winrt::put_abi(interface_access));
    if (SUCCEEDED(hr)) {
      winrt::com_ptr<ID3D11Texture2D> frame_texture;
      hr = interface_access->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<::IUnknown **>(frame_texture.put_void()));
      if (SUCCEEDED(hr)) {
        hr = _resource_manager->get_keyed_mutex()->AcquireSync(0, INFINITE);
        if (SUCCEEDED(hr)) {
          _d3d_context->CopyResource(_resource_manager->get_shared_texture(), frame_texture.get());

          // Create frame metadata and send via pipe instead of shared memory
          _resource_manager->get_keyed_mutex()->ReleaseSync(1);
          _pipe->send({FRAME_READY_MSG});
        } else {
          // Log error
          BOOST_LOG(error) << "Failed to acquire keyed mutex: " << hr;
        }
      } else {
        // Log error
        BOOST_LOG(error) << "Failed to get ID3D11Texture2D from interface: " << hr;
      }
    } else {
      // Log error
      BOOST_LOG(error) << "Failed to query IDirect3DDxgiInterfaceAccess: " << hr;
    }
  }

  /**
   * @brief Creates a capture session for the specified capture item.
   * @returns true if the session was created successfully, false otherwise.
   */
  bool create_capture_session() {
    if (!_frame_pool) {
      return false;
    }

    _capture_session = _frame_pool.CreateCaptureSession(_graphics_item);
    _capture_session.IsBorderRequired(false);

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

  /**
   * @brief Cleans up the capture session and frame pool resources.
   */
  void cleanup() noexcept {
    if (_capture_session) {
      _capture_session.Close();
      _capture_session = nullptr;
    }
    if (_frame_pool && _frame_arrived_token.value != 0) {
      _frame_pool.FrameArrived(_frame_arrived_token);
      _frame_pool.Close();
      _frame_pool = nullptr;
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
    while (!_drop_timestamps.empty() &&
           now - _drop_timestamps.front() > std::chrono::seconds(5)) {
      _drop_timestamps.pop_front();
    }

    // 2) Check if we should increase buffer count (≥2 drops in last 5 seconds)
    if (_drop_timestamps.size() >= 2 && _current_buffer_size < MAX_BUFFER_SIZE) {
      uint32_t new_buffer_size = _current_buffer_size + 1;
      BOOST_LOG(info) << "Detected " << _drop_timestamps.size() << " frame drops in 5s window, increasing buffer from "
                      << _current_buffer_size << " to " << new_buffer_size;
      create_or_adjust_frame_pool(new_buffer_size);
      _drop_timestamps.clear();  // Reset after adjustment
      _peak_outstanding = 0;  // Reset peak tracking
      _last_quiet_start = now;  // Reset quiet timer
      return;
    }

    // 3) Check if we should decrease buffer count (sustained quiet period)
    bool is_quiet = _drop_timestamps.empty() &&
                    _peak_outstanding.load() <= static_cast<int>(_current_buffer_size) - 1;

    if (is_quiet) {
      // Check if we've been quiet for 30 seconds
      if (now - _last_quiet_start >= std::chrono::seconds(30) && _current_buffer_size > 1) {
        uint32_t new_buffer_size = _current_buffer_size - 1;
        BOOST_LOG(info) << "Sustained quiet period (30s) with peak occupancy " << _peak_outstanding.load()
                        << " ≤ " << (_current_buffer_size - 1) << ", decreasing buffer from "
                        << _current_buffer_size << " to " << new_buffer_size;
        create_or_adjust_frame_pool(new_buffer_size);
        _peak_outstanding = 0;  // Reset peak tracking
        _last_quiet_start = now;  // Reset quiet timer
      }
    } else {
      // Reset quiet timer if we're not in a quiet state
      _last_quiet_start = now;
    }
  }
};

/**
 * @brief Static member initialization for WgcCaptureManager frame processing statistics.
 */
std::chrono::steady_clock::time_point WgcCaptureManager::_last_delivery_time = std::chrono::steady_clock::time_point {};
bool WgcCaptureManager::_first_frame = true;
uint32_t WgcCaptureManager::_delivery_count = 0;
std::chrono::milliseconds WgcCaptureManager::_total_delivery_time {0};

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

    bool secure_desktop_active = platf::wgc::is_secure_desktop_active();
    BOOST_LOG(info) << "Desktop switch - Secure desktop: " << (secure_desktop_active ? "YES" : "NO");

    if (secure_desktop_active && !g_secure_desktop_detected) {
      BOOST_LOG(info) << "Secure desktop detected - sending notification to main process";
      g_secure_desktop_detected = true;

      // Send notification to main process
      if (g_communication_pipe && g_communication_pipe->is_connected()) {
        g_communication_pipe->send({SECURE_DESKTOP_MSG});
        BOOST_LOG(info) << "Sent secure desktop notification to main process (0x02)";
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
  wchar_t temp_path[MAX_PATH] = {0};
  if (auto len = GetTempPathW(MAX_PATH, temp_path); len == 0 || len > MAX_PATH) {
    // fallback to current directory if temp path fails
    return "sunshine_wgc_helper.log";
  }
  std::wstring wlog = std::wstring(temp_path) + L"sunshine_wgc_helper.log";
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
 * - Heartbeat messages (0x01): Updates the last message timestamp to prevent timeout
 * - Configuration messages: Receives and stores config_data_t structure with display settings
 *
 * @param message The received message bytes from the named pipe.
 * @param last_msg_time Reference to track the last message timestamp for heartbeat monitoring.
 */
void handle_ipc_message(const std::vector<uint8_t> &message, std::chrono::steady_clock::time_point &last_msg_time) {
  // Heartbeat message: single byte 0x01
  if (message.size() == 1 && message[0] == 0x01) {
    last_msg_time = std::chrono::steady_clock::now();
    return;
  }
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
                    << ", display: '" << winrt::to_string(g_config.display_name) << "'";
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
 * @param last_msg_time Reference to track heartbeat timing for timeout detection.
 * @return true if the pipe was successfully configured and started, false otherwise.
 */
bool setup_pipe_callbacks(AsyncNamedPipe &pipe, std::chrono::steady_clock::time_point &last_msg_time) {
  auto on_message = [&last_msg_time](const std::vector<uint8_t> &message) {
    handle_ipc_message(message, last_msg_time);
  };

  auto on_error = [](std::string_view /*err*/) {
    // Error handler, intentionally left empty or log as needed
  };

  return pipe.start(on_message, on_error);
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
 * @param argc Number of command line arguments (unused).
 * @param argv Array of command line argument strings (unused).
 * @return 0 on successful completion, 1 on initialization failure.
 */
int main(int argc, char *argv[]) {
  // Set up default config and log level
  auto log_deinit = logging::init(2, get_temp_log_path());

  // Heartbeat mechanism: track last heartbeat time
  auto last_msg_time = std::chrono::steady_clock::now();

  // Initialize system settings (DPI awareness, thread priority, MMCSS)
  SystemInitializer system_initializer;
  if (!system_initializer.initialize_all()) {
    BOOST_LOG(error) << "System initialization failed, exiting...";
    return 1;
  }

  // Debug: Verify system settings
  BOOST_LOG(info) << "System initialization successful";
  BOOST_LOG(debug) << "DPI awareness set: " << (system_initializer.is_dpi_awareness_set() ? "YES" : "NO");
  BOOST_LOG(debug) << "Thread priority set: " << (system_initializer.is_thread_priority_set() ? "YES" : "NO");
  BOOST_LOG(debug) << "MMCSS characteristics set: " << (system_initializer.is_mmcss_characteristics_set() ? "YES" : "NO");

  BOOST_LOG(info) << "Starting Windows Graphics Capture helper process...";

  // Create named pipe for communication with main process
  AnonymousPipeFactory pipe_factory;

  auto comm_pipe = pipe_factory.create_client("SunshineWGCPipe");
  AsyncNamedPipe pipe(std::move(comm_pipe));
  g_communication_pipe = &pipe;  // Store global reference for session.Closed handler

  if (!setup_pipe_callbacks(pipe, last_msg_time)) {
    BOOST_LOG(error) << "Failed to start communication pipe";
    return 1;
  }

  // Create D3D11 device and context
  D3D11DeviceManager d3d11_manager;
  if (!d3d11_manager.initialize_all()) {
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
    d3d11_manager.cleanup();
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
  std::vector<uint8_t> handle_message(sizeof(platf::dxgi::shared_handle_data_t));
  memcpy(handle_message.data(), &handle_data, sizeof(platf::dxgi::shared_handle_data_t));

  // Wait for connection and send the handle data
  BOOST_LOG(info) << "Waiting for main process to connect...";
  while (!pipe.is_connected()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  BOOST_LOG(info) << "Connected! Sending handle data...";
  pipe.send(handle_message);

  // Create WGC capture manager
  WgcCaptureManager wgc_capture_manager {d3d11_manager.get_winrt_device(), capture_format, display_manager.get_width(), display_manager.get_height(), item};
  if (!wgc_capture_manager.create_or_adjust_frame_pool(1)) {
    BOOST_LOG(error) << "Failed to create frame pool";
    return 1;
  }

  wgc_capture_manager.attach_frame_arrived_handler(&shared_resource_manager, d3d11_manager.get_context(), &pipe);

  if (!wgc_capture_manager.create_capture_session()) {
    BOOST_LOG(error) << "Failed to create capture session";
    return 1;
  }

  // Set up desktop switch hook for secure desktop detection
  BOOST_LOG(info) << "Setting up desktop switch hook...";
  if (HWINEVENTHOOK raw_hook = SetWinEventHook(
        EVENT_SYSTEM_DESKTOPSWITCH,
        EVENT_SYSTEM_DESKTOPSWITCH,
        nullptr,
        desktop_switch_hook_proc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
      );
      !raw_hook) {
    BOOST_LOG(error) << "Failed to set up desktop switch hook: " << GetLastError();
  } else {
    g_desktop_switch_hook.reset(raw_hook);
    BOOST_LOG(info) << "Desktop switch hook installed successfully";
  }

  wgc_capture_manager.start_capture();

  // Controlled shutdown flag
  bool shutdown_requested = false;
  MSG msg;
  while (pipe.is_connected() && !shutdown_requested) {
    // Process any pending messages for the hook
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      // If WM_QUIT is received, exit loop
      if (msg.message == WM_QUIT) {
        shutdown_requested = true;
      }
    }

    // Heartbeat timeout check
    auto now = std::chrono::steady_clock::now();
    if (!shutdown_requested && std::chrono::duration_cast<std::chrono::seconds>(now - last_msg_time).count() > 5) {
      BOOST_LOG(warning) << "No heartbeat received from main process for 5 seconds, requesting controlled shutdown...";
      shutdown_requested = true;
      PostQuitMessage(0);
      continue;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Reduced from 5ms for lower IPC jitter
  }

  BOOST_LOG(info) << "Main process disconnected, shutting down...";

  // Cleanup is handled automatically by RAII destructors
  wgc_capture_manager.cleanup();
  pipe.stop();

  // Flush logs before exit
  boost::log::core::get()->flush();

  BOOST_LOG(info) << "WGC Helper process terminated";

  // Cleanup managed by destructors
  return 0;
}
