// sunshine_wgc_helper.cpp
// Windows Graphics Capture helper process for Sunshine
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

/**
 * @brief Get the current QueryPerformanceCounter value.
 * @return The current QPC counter value as a 64-bit integer.
 */
// Function to get QPC counter (similar to main process)
inline int64_t qpc_counter() {
  LARGE_INTEGER counter;
  QueryPerformanceCounter(&counter);
  return counter.QuadPart;
}

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::System;
using namespace std::literals;
using namespace platf::dxgi;
using namespace platf::dxgi;

// Global config data received from main process
const int INITIAL_LOG_LEVEL = 2;
platf::dxgi::ConfigData g_config = {0, 0, 0, 0, 0, L""};
bool g_config_received = false;

// Global variables for frame metadata and rate limiting
safe_handle g_metadata_mapping = nullptr;
platf::dxgi::FrameMetadata *g_frame_metadata = nullptr;
uint32_t g_frame_sequence = 0;

// Global communication pipe for sending session closed notifications
AsyncNamedPipe *g_communication_pipe = nullptr;

// Global variables for desktop switch detection
safe_winevent_hook g_desktop_switch_hook = nullptr;
bool g_secure_desktop_detected = false;

// System initialization class to handle DPI, threading, and MMCSS setup
class SystemInitializer {
private:
  safe_mmcss_handle mmcss_handle = nullptr;
  bool dpi_awareness_set = false;
  bool thread_priority_set = false;
  bool mmcss_characteristics_set = false;

public:
  /**
   * @brief Initializes the DPI awareness for the current process to prevent display scaling issues.
   *
   * This function attempts to set the process DPI awareness using the most modern available API:
   *
   * - First, it tries to use SetProcessDpiAwarenessContext (Windows 10 Creators Update/1703 and later)
   *   with DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2.
   *
   * - If unavailable, it falls back to SetProcessDpiAwareness with PROCESS_PER_MONITOR_DPI_AWARE.
   *
   * - Logs a warning if unable to set DPI awareness, which may result in display scaling issues.
   *
   * @return true if DPI awareness was successfully set, false otherwise.
   */
  bool initializeDpiAwareness() {
    // Set DPI awareness to prevent zoomed display issues
    // Try the newer API first (Windows 10 1703+), fallback to older API
    typedef BOOL(WINAPI * SetProcessDpiAwarenessContextFunc)(DPI_AWARENESS_CONTEXT);

    if (HMODULE user32 = GetModuleHandleA("user32.dll")) {
      auto setDpiContextFunc = (SetProcessDpiAwarenessContextFunc) GetProcAddress(user32, "SetProcessDpiAwarenessContext");
      if (setDpiContextFunc) {
        if (setDpiContextFunc((DPI_AWARENESS_CONTEXT) -4)) {  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
          dpi_awareness_set = true;
          return true;
        }
      } else if (FAILED(SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE))) {
        BOOST_LOG(warning) << "Failed to set DPI awareness, display scaling issues may occur";
        return false;
      } else {
        dpi_awareness_set = true;
        return true;
      }
    } else if (FAILED(SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE))) {
      BOOST_LOG(warning) << "Failed to set DPI awareness, display scaling issues may occur";
      return false;
    } else {
      dpi_awareness_set = true;
      return true;
    }

    // Fallback case - should not reach here
    BOOST_LOG(warning) << "Failed to set DPI awareness, display scaling issues may occur";
    return false;
  }

  /**
   * @brief Attempts to increase the current thread's priority to the highest level.
   *
   * This function sets the priority of the calling thread to THREAD_PRIORITY_HIGHEST
   * using the Windows API. If the operation fails, it logs an error message with the
   * corresponding error code and returns false. On success, it marks the thread
   * priority as set and returns true.
   *
   * @return true if the thread priority was successfully set; false otherwise.
   */
  bool initializeThreadPriority() {
    // Increase thread priority to minimize scheduling delay
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
      BOOST_LOG(error) << "Failed to set thread priority: " << GetLastError();
      return false;
    }

    thread_priority_set = true;
    return true;
  }

  /**
   * @brief Initializes MMCSS (Multimedia Class Scheduler Service) characteristics for the current thread.
   *
   * This function attempts to set the thread's MMCSS characteristics to "Pro Audio" for optimal real-time
   * audio processing. If setting "Pro Audio" fails, it falls back to "Games" as an alternative. The function
   * logs an error if both attempts fail and returns false. On success, it stores the MMCSS handle and marks
   * the characteristics as set.
   *
   * @return true if MMCSS characteristics were successfully set; false otherwise.
   */
  bool initializeMmcssCharacteristics() {
    // Set MMCSS for real-time scheduling - try "Pro Audio" for lower latency
    DWORD taskIdx = 0;
    HANDLE raw_handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIdx);
    if (!raw_handle) {
      // Fallback to "Games" if "Pro Audio" fails
      raw_handle = AvSetMmThreadCharacteristicsW(L"Games", &taskIdx);
      if (!raw_handle) {
        BOOST_LOG(error) << "Failed to set MMCSS characteristics: " << GetLastError();
        return false;
      }
    }
    mmcss_handle.reset(raw_handle);
    mmcss_characteristics_set = true;
    return true;
  }

  /**
   * @brief Initializes the Windows Runtime (WinRT) apartment for multi-threaded operations.
   *
   * This function sets up the WinRT environment to use a multi-threaded apartment model,
   * which is required for certain WinRT APIs that expect this threading context.
   *
   * @return Always returns true to indicate the initialization was performed.
   */
  bool initializeWinRtApartment() const {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    return true;
  }

  /**
   * @brief Initializes all required system settings for the application.
   *
   * This function sequentially initializes DPI awareness, thread priority,
   * MMCSS characteristics, and WinRT apartment state. If any initialization
   * step fails, the function returns false.
   *
   * @return true if all initialization steps succeed, false otherwise.
   */
  bool initializeAll() {
    bool success = true;
    success &= initializeDpiAwareness();
    success &= initializeThreadPriority();
    success &= initializeMmcssCharacteristics();
    success &= initializeWinRtApartment();
    return success;
  }

  /**
   * @brief Cleans up system initialization state.
   *
   * Resets the MMCSS handle and marks MMCSS characteristics as unset.
   * Most cleanup is handled automatically by RAII.
   */
  void cleanup() noexcept {
    // RAII handles cleanup automatically
    mmcss_handle.reset();
    mmcss_characteristics_set = false;
  }

  /**
   * @brief Checks if DPI awareness was set successfully.
   * @return true if DPI awareness is set, false otherwise.
   */
  bool isDpiAwarenessSet() const {
    return dpi_awareness_set;
  }

  /**
   * @brief Checks if thread priority was set successfully.
   * @return true if thread priority is set, false otherwise.
   */
  bool isThreadPrioritySet() const {
    return thread_priority_set;
  }

  /**
   * @brief Checks if MMCSS characteristics were set successfully.
   * @return true if MMCSS characteristics are set, false otherwise.
   */
  bool isMmcssCharacteristicsSet() const {
    return mmcss_characteristics_set;
  }

  /**
   * @brief Destructor for SystemInitializer.
   *
   * Calls cleanup to ensure any necessary cleanup is performed when the object is destroyed.
   * All resources are managed using RAII, so explicit cleanup is minimal.
   */
  ~SystemInitializer() noexcept {
    cleanup();
  }
};

// D3D11 device management class to handle device creation and WinRT interop
class D3D11DeviceManager {
private:
  safe_com_ptr<ID3D11Device> device = nullptr;
  safe_com_ptr<ID3D11DeviceContext> context = nullptr;
  D3D_FEATURE_LEVEL feature_level;
  winrt::com_ptr<IDXGIDevice> dxgi_device;
  winrt::com_ptr<::IDirect3DDevice> interop_device;
  IDirect3DDevice winrt_device = nullptr;

public:
  bool createDevice() {
    ID3D11Device *raw_device = nullptr;
    ID3D11DeviceContext *raw_context = nullptr;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &raw_device, &feature_level, &raw_context);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to create D3D11 device";
      return false;
    }

    device.reset(raw_device);
    context.reset(raw_context);
    return true;
  }

  bool createWinRtInterop() {
    if (!device) {
      return false;
    }

    HRESULT hr = device->QueryInterface(__uuidof(IDXGIDevice), dxgi_device.put_void());
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to get DXGI device";
      return false;
    }

    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.get(), reinterpret_cast<::IInspectable **>(winrt::put_abi(interop_device)));
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to create interop device";
      return false;
    }

    winrt_device = interop_device.as<IDirect3DDevice>();
    return true;
  }

  /**
   * @brief Initializes the D3D11 device and WinRT interop device.
   *
   * Calls createDevice() to create a D3D11 device and context, then createWinRtInterop() to create the WinRT interop device.
   *
   * @return true if both device and interop device are successfully created, false otherwise.
   * @sideeffect Allocates and stores device, context, and WinRT device handles.
   */
  bool initializeAll() {
    return createDevice() && createWinRtInterop();
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
    context.reset();
    device.reset();
  }

  /**
   * @brief Gets the underlying D3D11 device pointer.
   *
   * @return Pointer to the managed ID3D11Device, or nullptr if not initialized.
   */
  ID3D11Device *getDevice() {
    return device.get();
  }

  /**
   * @brief Gets the underlying D3D11 device context pointer.
   *
   * @return Pointer to the managed ID3D11DeviceContext, or nullptr if not initialized.
   */
  ID3D11DeviceContext *getContext() {
    return context.get();
  }

  /**
   * @brief Gets the WinRT IDirect3DDevice for Windows Graphics Capture interop.
   *
   * @return The WinRT IDirect3DDevice, or nullptr if not initialized.
   */
  IDirect3DDevice getWinRtDevice() const {
    return winrt_device;
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

// Monitor and display management class to handle monitor enumeration and selection
class DisplayManager {
private:
  HMONITOR selected_monitor = nullptr;
  MONITORINFO monitor_info = {sizeof(MONITORINFO)};
  UINT fallback_width = 0;
  UINT fallback_height = 0;
  UINT final_width = 0;
  UINT final_height = 0;

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
  bool selectMonitor(const platf::dxgi::ConfigData &config) {
    if (config.displayName[0] != L'\0') {
      // Enumerate monitors to find one matching displayName
      struct EnumData {
        const wchar_t *targetName;
        HMONITOR foundMonitor;
      };

      EnumData enumData = {config.displayName, nullptr};

      auto enumProc = +[](HMONITOR hMon, HDC, RECT *, LPARAM lParam) {
        auto *data = static_cast<EnumData *>(reinterpret_cast<void *>(lParam));
        if (MONITORINFOEXW mInfo = {sizeof(MONITORINFOEXW)}; GetMonitorInfoW(hMon, &mInfo) && wcsncmp(mInfo.szDevice, data->targetName, 32) == 0) {
          data->foundMonitor = hMon;
          return FALSE;  // Stop enumeration
        }
        return TRUE;
      };
      EnumDisplayMonitors(nullptr, nullptr, enumProc, reinterpret_cast<LPARAM>(&enumData));
      selected_monitor = enumData.foundMonitor;
      if (!selected_monitor) {
        BOOST_LOG(warning) << "Could not find monitor with name '" << winrt::to_string(config.displayName) << "', falling back to primary.";
      }
    }

    if (!selected_monitor) {
      selected_monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
      if (!selected_monitor) {
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
  bool getMonitorInfo() {
    if (!selected_monitor) {
      return false;
    }

    if (!GetMonitorInfo(selected_monitor, &monitor_info)) {
      BOOST_LOG(error) << "Failed to get monitor info";
      return false;
    }

    fallback_width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
    fallback_height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
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
  bool createGraphicsCaptureItem(GraphicsCaptureItem &item) {
    if (!selected_monitor) {
      return false;
    }

    auto activationFactory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    HRESULT hr = activationFactory->CreateForMonitor(selected_monitor, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item));
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
  void calculateFinalResolution(const platf::dxgi::ConfigData &config, bool config_received, const GraphicsCaptureItem &item) {
    // Get actual WGC item size to ensure we capture full desktop (fixes zoomed display issue)
    auto item_size = item.Size();
    final_height = item_size.Height;
    final_width = item_size.Width;
  }

  HMONITOR getSelectedMonitor() const {
    return selected_monitor;
  }

  UINT getFinalWidth() const {
    return final_width;
  }

  UINT getFinalHeight() const {
    return final_height;
  }

  UINT getFallbackWidth() const {
    return fallback_width;
  }

  UINT getFallbackHeight() const {
    return fallback_height;
  }
};

// Shared resource management class to handle texture, memory mapping, and events
class SharedResourceManager {
private:
  safe_com_ptr<ID3D11Texture2D> shared_texture = nullptr;
  safe_com_ptr<IDXGIKeyedMutex> keyed_mutex = nullptr;
  HANDLE shared_handle = nullptr;
  safe_handle metadata_mapping = nullptr;
  safe_memory_view frame_metadata_view = nullptr;
  FrameMetadata *frame_metadata = nullptr;
  safe_handle frame_event = nullptr;
  UINT width = 0;
  UINT height = 0;

public:
  // Rule of 5: delete copy operations and move constructor/assignment
  SharedResourceManager() = default;
  SharedResourceManager(const SharedResourceManager &) = delete;
  SharedResourceManager &operator=(const SharedResourceManager &) = delete;
  SharedResourceManager(SharedResourceManager &&) = delete;
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
  bool createSharedTexture(ID3D11Device *device, UINT texture_width, UINT texture_height, DXGI_FORMAT format) {
    width = texture_width;
    height = texture_height;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = 0;
    texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    ID3D11Texture2D *raw_texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, &raw_texture);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to create shared texture";
      return false;
    }
    shared_texture.reset(raw_texture);
    return true;
  }

  /**
   * @brief Acquires a keyed mutex interface from the shared texture for synchronization.
   * @return true if the keyed mutex was successfully acquired; false otherwise.
   */
  bool createKeyedMutex() {
    if (!shared_texture) {
      return false;
    }

    IDXGIKeyedMutex *raw_mutex = nullptr;
    HRESULT hr = shared_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **) (&raw_mutex));
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to get keyed mutex";
      return false;
    }
    keyed_mutex.reset(raw_mutex);
    return true;
  }

  /**
   * @brief Obtains a shared handle for the texture to allow sharing with other processes.
   * @return true if the shared handle was successfully obtained; false otherwise.
   */
  bool createSharedHandle() {
    if (!shared_texture) {
      return false;
    }

    IDXGIResource *dxgiResource = nullptr;
    HRESULT hr = shared_texture->QueryInterface(__uuidof(IDXGIResource), (void **) (&dxgiResource));
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to query DXGI resource interface";
      return false;
    }

    hr = dxgiResource->GetSharedHandle(&shared_handle);
    dxgiResource->Release();
    if (FAILED(hr) || !shared_handle) {
      BOOST_LOG(error) << "Failed to get shared handle";
      return false;
    }

    BOOST_LOG(info) << "Created shared texture: " << width << "x" << height
                    << ", handle: " << std::hex << reinterpret_cast<std::uintptr_t>(shared_handle) << std::dec;
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
  bool initializeAll(ID3D11Device *device, UINT texture_width, UINT texture_height, DXGI_FORMAT format) {
    return createSharedTexture(device, texture_width, texture_height, format) &&
           createKeyedMutex() &&
           createSharedHandle();
  }

  /**
   * @brief Gets the shared handle data (handle, width, height) for inter-process sharing.
   * @return SharedHandleData struct containing the handle and texture dimensions.
   */
  platf::dxgi::SharedHandleData getSharedHandleData() const {
    return {shared_handle, width, height};
  }

  /**
   * @brief Cleans up all managed shared resources. Resets all internal handles and pointers. Safe to call multiple times.
   */
  void cleanup() noexcept {
    // RAII handles cleanup automatically
    frame_metadata = nullptr;
    frame_metadata_view.reset();
    metadata_mapping.reset();
    frame_event.reset();
    keyed_mutex.reset();
    shared_texture.reset();
  }

  /**
   * @brief Gets the underlying shared D3D11 texture pointer.
   * @return Pointer to the managed ID3D11Texture2D, or nullptr if not initialized.
   */
  ID3D11Texture2D *getSharedTexture() {
    return shared_texture.get();
  }

  /**
   * @brief Gets the keyed mutex interface for the shared texture.
   * @return Pointer to the managed IDXGIKeyedMutex, or nullptr if not initialized.
   */
  IDXGIKeyedMutex *getKeyedMutex() {
    return keyed_mutex.get();
  }

  /**
   * @brief Gets the frame event handle for synchronization.
   * @return The frame event HANDLE, or nullptr if not initialized.
   */
  HANDLE getFrameEvent() {
    return frame_event.get();
  }

  /**
   * @brief Gets the pointer to the frame metadata structure.
   * @return Pointer to FrameMetadata, or nullptr if not initialized.
   */
  platf::dxgi::FrameMetadata *getFrameMetadata() const {
    return frame_metadata;
  }

  /**
   * @brief Destructor for SharedResourceManager.\
   * Calls cleanup() to release all managed resources. All resources are managed using RAII.
   */
  ~SharedResourceManager() noexcept {
    cleanup();
  }
};

// WGC capture management class to handle frame pool, capture session, and frame processing
class WgcCaptureManager {
private:
  Direct3D11CaptureFramePool frame_pool = nullptr;
  GraphicsCaptureSession capture_session = nullptr;
  winrt::event_token frame_arrived_token {};
  SharedResourceManager *resource_manager = nullptr;
  ID3D11DeviceContext *d3d_context = nullptr;
  AsyncNamedPipe *communication_pipe = nullptr;  // Add pipe communication

  // Dynamic frame buffer management
  uint32_t current_buffer_size = 1;
  static constexpr uint32_t MAX_BUFFER_SIZE = 4;
  
  // Advanced buffer management tracking
  std::deque<std::chrono::steady_clock::time_point> drop_timestamps;
  std::atomic<int> outstanding_frames{0};
  std::atomic<int> peak_outstanding{0};
  std::chrono::steady_clock::time_point last_quiet_start = std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point last_buffer_check = std::chrono::steady_clock::now();
  IDirect3DDevice winrt_device_cached = nullptr;
  DXGI_FORMAT capture_format_cached = DXGI_FORMAT_UNKNOWN;
  UINT width_cached = 0;
  UINT height_cached = 0;
  GraphicsCaptureItem capture_item_cached = nullptr;

  // Frame processing state
  static std::chrono::steady_clock::time_point last_delivery_time;
  static bool first_frame;
  static uint32_t delivery_count;
  static std::chrono::milliseconds total_delivery_time;

public:
  // Rule of 5: delete copy operations and move constructor/assignment
  /**
   * @brief Default constructor for WgcCaptureManager.
   */
  WgcCaptureManager() = default;
  WgcCaptureManager(const WgcCaptureManager &) = delete;
  WgcCaptureManager &operator=(const WgcCaptureManager &) = delete;
  WgcCaptureManager(WgcCaptureManager &&) = delete;
  WgcCaptureManager &operator=(WgcCaptureManager &&) = delete;

  /**
   * @brief Creates a Direct3D11CaptureFramePool for frame capture.
   * @param winrt_device The WinRT Direct3D device to use for capture.
   * @param capture_format The DXGI format for captured frames.
   * @param width The width of the capture area in pixels.
   * @param height The height of the capture area in pixels.
   * @returns true if the frame pool was created successfully, false otherwise.
   */
  bool createFramePool(IDirect3DDevice winrt_device, DXGI_FORMAT capture_format, UINT width, UINT height) {
    // Cache parameters for dynamic buffer adjustment
    winrt_device_cached = winrt_device;
    capture_format_cached = capture_format;
    width_cached = width;
    height_cached = height;

    return recreateFramePool(current_buffer_size);
  }

  /**
   * @brief Recreates the frame pool with a new buffer size for dynamic adjustment.
   * @param buffer_size The number of frames to buffer (1 or 2).
   * @returns true if the frame pool was recreated successfully, false otherwise.
   */
  bool recreateFramePool(uint32_t buffer_size) {
    if (!winrt_device_cached || capture_format_cached == DXGI_FORMAT_UNKNOWN) {
      return false;
    }

    if (frame_pool) {
      // Use the proper Recreate method instead of closing and re-creating
      try {
        frame_pool.Recreate(
          winrt_device_cached,
          (capture_format_cached == DXGI_FORMAT_R16G16B16A16_FLOAT) ? DirectXPixelFormat::R16G16B16A16Float : DirectXPixelFormat::B8G8R8A8UIntNormalized,
          buffer_size,
          SizeInt32 {static_cast<int32_t>(width_cached), static_cast<int32_t>(height_cached)}
        );
        
        current_buffer_size = buffer_size;
        BOOST_LOG(info) << "Frame pool recreated with buffer size: " << buffer_size;
        return true;
      } catch (const winrt::hresult_error &ex) {
        BOOST_LOG(error) << "Failed to recreate frame pool: " << ex.code() << " - " << winrt::to_string(ex.message());
        return false;
      }
    } else {
      // Initial creation case - create new frame pool
      frame_pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
        winrt_device_cached,
        (capture_format_cached == DXGI_FORMAT_R16G16B16A16_FLOAT) ? DirectXPixelFormat::R16G16B16A16Float : DirectXPixelFormat::B8G8R8A8UIntNormalized,
        buffer_size,
        SizeInt32 {static_cast<int32_t>(width_cached), static_cast<int32_t>(height_cached)}
      );

      if (frame_pool) {
        current_buffer_size = buffer_size;
        BOOST_LOG(info) << "Frame pool created with buffer size: " << buffer_size;
        return true;
      }
    }
    
    return false;
  }

  /**
   * @brief Adjusts frame buffer size dynamically based on dropped frame events and peak occupancy.
   * 
   * Uses a sliding window approach to track frame drops and buffer utilization:
   * - Increases buffer count if ≥2 drops occur within any 5-second window
   * - Decreases buffer count if no drops AND peak occupancy ≤ (bufferCount-1) for 30 seconds
   */
  void adjustFrameBufferDynamically() {
    auto now = std::chrono::steady_clock::now();
    
    // Check every 1 second for buffer adjustments
    if (auto time_since_last_check = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_buffer_check); 
        time_since_last_check.count() < 1000) {
      return;
    }

    last_buffer_check = now;
    
    // 1) Prune old drop timestamps (older than 5 seconds)
    while (!drop_timestamps.empty() && 
           now - drop_timestamps.front() > std::chrono::seconds(5)) {
      drop_timestamps.pop_front();
    }
    
    // 2) Check if we should increase buffer count (≥2 drops in last 5 seconds)
    if (drop_timestamps.size() >= 2 && current_buffer_size < MAX_BUFFER_SIZE) {
      uint32_t new_buffer_size = current_buffer_size + 1;
      BOOST_LOG(info) << "Detected " << drop_timestamps.size() << " frame drops in 5s window, increasing buffer from " 
                      << current_buffer_size << " to " << new_buffer_size;
      recreateFramePool(new_buffer_size);
      drop_timestamps.clear(); // Reset after adjustment
      peak_outstanding = 0;    // Reset peak tracking
      last_quiet_start = now;  // Reset quiet timer
      return;
    }
    
    // 3) Check if we should decrease buffer count (sustained quiet period)
    bool is_quiet = drop_timestamps.empty() && 
                   peak_outstanding.load() <= static_cast<int>(current_buffer_size) - 1;
    
    if (is_quiet) {
      // Check if we've been quiet for 30 seconds
      if (now - last_quiet_start >= std::chrono::seconds(30) && current_buffer_size > 1) {
        uint32_t new_buffer_size = current_buffer_size - 1;
        BOOST_LOG(info) << "Sustained quiet period (30s) with peak occupancy " << peak_outstanding.load() 
                        << " ≤ " << (current_buffer_size - 1) << ", decreasing buffer from " 
                        << current_buffer_size << " to " << new_buffer_size;
        recreateFramePool(new_buffer_size);
        peak_outstanding = 0;    // Reset peak tracking
        last_quiet_start = now;  // Reset quiet timer
      }
    } else {
      // Reset quiet timer if we're not in a quiet state
      last_quiet_start = now;
    }
  }

  /**
   * @brief Attaches the frame arrived event handler for frame processing.
   * @param res_mgr Pointer to the shared resource manager.
   * @param context Pointer to the D3D11 device context.
   * @param pipe Pointer to the async named pipe for IPC.
   */
  void attachFrameArrivedHandler(SharedResourceManager *res_mgr, ID3D11DeviceContext *context, AsyncNamedPipe *pipe) {
    resource_manager = res_mgr;
    d3d_context = context;
    communication_pipe = pipe;

    frame_arrived_token = frame_pool.FrameArrived([this](Direct3D11CaptureFramePool const &sender, winrt::Windows::Foundation::IInspectable const &) {
      // Track outstanding frames for buffer occupancy monitoring
      ++outstanding_frames;
      peak_outstanding = std::max(peak_outstanding.load(), outstanding_frames.load());
      
      processFrame(sender);
    });
  }

  /**
   * @brief Processes a frame when the frame arrived event is triggered.
   * @param sender The frame pool that triggered the event.
   */
  void processFrame(Direct3D11CaptureFramePool const &sender) {
    // Timestamp #1: Very beginning of FrameArrived handler
    uint64_t timestamp_frame_arrived = qpc_counter();

    if (auto frame = sender.TryGetNextFrame(); frame) {
      // Frame successfully retrieved
      auto surface = frame.Surface();

      // Capture QPC timestamp as close to frame processing as possible
      uint64_t frame_qpc = qpc_counter();

      // Track timing for async logging
      auto now = std::chrono::steady_clock::now();

      // Only calculate timing after the first frame
      if (!first_frame) {
        auto delivery_interval = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_delivery_time);
        total_delivery_time += delivery_interval;
        delivery_count++;

        // Log timing every 300 frames
        if (delivery_count % 300 == 0) {
          auto avg_delivery_ms = total_delivery_time.count() / delivery_count;
          auto expected_ms = (g_config_received && g_config.framerate > 0) ? (1000 / g_config.framerate) : 16;

          BOOST_LOG(debug) << "Frame delivery timing - Avg interval: " << avg_delivery_ms << "ms, Expected: " << expected_ms << "ms, Last: " << delivery_interval.count() << "ms";

          // Reset counters for next measurement window
          total_delivery_time = std::chrono::milliseconds {0};
          delivery_count = 0;
        }
      } else {
        first_frame = false;
      }
      last_delivery_time = now;

      try {
        processSurfaceToTexture(surface, timestamp_frame_arrived, frame_qpc);
      } catch (const winrt::hresult_error &ex) {
        // Log error
        BOOST_LOG(error) << "WinRT error in frame processing: " << ex.code() << " - " << winrt::to_string(ex.message());
      }
      surface.Close();
      frame.Close();
    } else {
      // Frame drop detected - record timestamp for sliding window analysis
      auto now = std::chrono::steady_clock::now();
      drop_timestamps.push_back(now);
      
      BOOST_LOG(debug) << "Frame drop detected (total drops in 5s window: " << drop_timestamps.size() << ")";
    }

    // Decrement outstanding frame count (always called whether frame retrieved or not)
    --outstanding_frames;

    // Check if we need to adjust frame buffer size
    adjustFrameBufferDynamically();
  }

  /**
   * @brief Copies the captured surface to the shared texture and notifies the main process.
   * @param surface The captured D3D11 surface.
   * @param timestamp_frame_arrived QPC timestamp when frame arrived.
   * @param frame_qpc QPC timestamp for the frame.
   */
  void processSurfaceToTexture(winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface surface, uint64_t timestamp_frame_arrived, uint64_t frame_qpc) {
    if (!resource_manager || !d3d_context || !communication_pipe) {
      return;
    }

    winrt::com_ptr<IDirect3DDxgiInterfaceAccess> interfaceAccess;
    HRESULT hr = surface.as<::IUnknown>()->QueryInterface(__uuidof(IDirect3DDxgiInterfaceAccess), winrt::put_abi(interfaceAccess));
    if (SUCCEEDED(hr)) {
      winrt::com_ptr<ID3D11Texture2D> frameTexture;
      hr = interfaceAccess->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<::IUnknown **>(frameTexture.put_void()));
      if (SUCCEEDED(hr)) {
        hr = resource_manager->getKeyedMutex()->AcquireSync(0, INFINITE);
        if (SUCCEEDED(hr)) {
          d3d_context->CopyResource(resource_manager->getSharedTexture(), frameTexture.get());

          // Timestamp #2: Immediately after CopyResource completes
          uint64_t timestamp_after_copy = qpc_counter();

          // Create frame metadata and send via pipe instead of shared memory
          platf::dxgi::FrameMetadata frame_metadata = {};
          frame_metadata.qpc_timestamp = frame_qpc;
          frame_metadata.frame_sequence = ++g_frame_sequence;
          frame_metadata.suppressed_frames = 0;  // No suppression - always 0

          resource_manager->getKeyedMutex()->ReleaseSync(1);

          // Timestamp #3: Immediately after pipe send is called
          uint64_t timestamp_after_send = qpc_counter();

          // Send frame notification via pipe instead of SetEvent
          sendFrameNotification(frame_metadata);

          logFrameTiming(timestamp_frame_arrived, timestamp_after_copy, timestamp_after_send);
          logPerformanceStats(frame_qpc);
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
   * @brief Sends a frame notification to the main process via the communication pipe.
   * @param metadata The frame metadata to send.
   */
  void sendFrameNotification(const platf::dxgi::FrameMetadata &metadata) {
    if (!communication_pipe || !communication_pipe->isConnected()) {
      return;
    }

    platf::dxgi::FrameNotification notification = {};
    notification.metadata = metadata;
    notification.message_type = 0x03;  // Frame ready message type

    std::vector<uint8_t> message(sizeof(platf::dxgi::FrameNotification));
    memcpy(message.data(), &notification, sizeof(platf::dxgi::FrameNotification));

    communication_pipe->asyncSend(message);
  }

  /**
   * @brief Logs performance statistics such as instantaneous FPS and frame count.
   * @param frame_qpc QPC timestamp for the current frame.
   */
  void logPerformanceStats(uint64_t frame_qpc) const {
    // Performance telemetry: emit helper-side instantaneous fps (async)
    static uint64_t lastQpc = 0;
    static uint64_t qpc_freq = 0;
    if (qpc_freq == 0) {
      LARGE_INTEGER freq;
      QueryPerformanceFrequency(&freq);
      qpc_freq = freq.QuadPart;
    }
    if ((g_frame_sequence % 600) == 0) {  // every ~5s at 120fps
      if (lastQpc != 0) {
        double fps = 600.0 * static_cast<double>(qpc_freq) / static_cast<double>(frame_qpc - lastQpc);
        BOOST_LOG(debug) << "delivered " << std::fixed << std::setprecision(1) << fps << " fps (target: " << (g_config_received ? g_config.framerate : 60) << ")";
      }
      lastQpc = frame_qpc;
    }

    // Log performance stats periodically
    if ((g_frame_sequence % 1500) == 0) {
      BOOST_LOG(debug) << "Frame " << g_frame_sequence << " processed without suppression";
    }
  }

  /**
   * @brief Logs high-precision timing deltas for frame processing steps.
   * @param timestamp_frame_arrived QPC timestamp when frame arrived.
   * @param timestamp_after_copy QPC timestamp after frame copy.
   * @param timestamp_after_send QPC timestamp after notification send.
   */
  void logFrameTiming(uint64_t timestamp_frame_arrived, uint64_t timestamp_after_copy, uint64_t timestamp_after_send) const {
    // Log high-precision timing deltas every 300 frames
    static uint32_t timing_log_counter = 0;
    if ((++timing_log_counter % 300) == 0) {
      static uint64_t qpc_freq_timing = 0;
      if (qpc_freq_timing == 0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        qpc_freq_timing = freq.QuadPart;
      }

      double arrived_to_copy_us = static_cast<double>(timestamp_after_copy - timestamp_frame_arrived) * 1000000.0 / static_cast<double>(qpc_freq_timing);
      double copy_to_send_us = static_cast<double>(timestamp_after_send - timestamp_after_copy) * 1000000.0 / static_cast<double>(qpc_freq_timing);
      double total_frame_us = static_cast<double>(timestamp_after_send - timestamp_frame_arrived) * 1000000.0 / static_cast<double>(qpc_freq_timing);

      BOOST_LOG(debug) << "Frame timing - Arrived->Copy: " << std::fixed << std::setprecision(1) << arrived_to_copy_us << "μs, Copy->Send: " << copy_to_send_us << "μs, Total: " << total_frame_us << "μs";
    }
  }

  /**
   * @brief Creates a capture session for the specified capture item.
   * @param item The GraphicsCaptureItem to capture.
   * @returns true if the session was created successfully, false otherwise.
   */
  bool createCaptureSession(GraphicsCaptureItem item) {
    if (!frame_pool) {
      return false;
    }

    // Cache the item for reference
    capture_item_cached = item;
    
    capture_session = frame_pool.CreateCaptureSession(capture_item_cached);
    capture_session.IsBorderRequired(false);

    if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"MinUpdateInterval")) {
      capture_session.MinUpdateInterval(winrt::Windows::Foundation::TimeSpan {10000});
      BOOST_LOG(info) << "Successfully set the MinUpdateInterval (120fps+)";
    }
    
    return true;
  }

  /**
   * @brief Starts the capture session if available.
   */
  void startCapture() const {
    if (capture_session) {
      capture_session.StartCapture();
      BOOST_LOG(info) << "Helper process started. Capturing frames using WGC...";
    }
  }

  /**
   * @brief Cleans up the capture session and frame pool resources.
   */
  void cleanup() noexcept {
    if (capture_session) {
      capture_session.Close();
      capture_session = nullptr;
    }
    if (frame_pool && frame_arrived_token.value != 0) {
      frame_pool.FrameArrived(frame_arrived_token);
      frame_pool.Close();
      frame_pool = nullptr;
    }
  }

  /**
   * @brief Destructor for WgcCaptureManager. Calls cleanup().
   */
  ~WgcCaptureManager() noexcept {
    cleanup();
  }
};

// Initialize static members
std::chrono::steady_clock::time_point WgcCaptureManager::last_delivery_time = std::chrono::steady_clock::time_point {};
bool WgcCaptureManager::first_frame = true;
uint32_t WgcCaptureManager::delivery_count = 0;
std::chrono::milliseconds WgcCaptureManager::total_delivery_time {0};

/**
 * @brief Callback procedure for desktop switch events.
 * @details Handles EVENT_SYSTEM_DESKTOPSWITCH to detect secure desktop transitions and notify the main process.
 */
// Desktop switch event hook procedure
void CALLBACK DesktopSwitchHookProc(HWINEVENTHOOK /*hWinEventHook*/, DWORD event, HWND /*hwnd*/, LONG /*idObject*/, LONG /*idChild*/, DWORD /*dwEventThread*/, DWORD /*dwmsEventTime*/) {
  if (event == EVENT_SYSTEM_DESKTOPSWITCH) {
    BOOST_LOG(info) << "Desktop switch detected!";

    // Small delay to let the system settle
    Sleep(100);

    bool isSecure = platf::wgc::is_secure_desktop_active();
    BOOST_LOG(info) << "Desktop switch - Secure desktop: " << (isSecure ? "YES" : "NO");

    if (isSecure && !g_secure_desktop_detected) {
      BOOST_LOG(info) << "Secure desktop detected - sending notification to main process";
      g_secure_desktop_detected = true;

      // Send notification to main process
      if (g_communication_pipe && g_communication_pipe->isConnected()) {
        std::vector<uint8_t> sessionClosedMessage = {0x02};  // 0x02 marker for secure desktop detected
        g_communication_pipe->asyncSend(sessionClosedMessage);
        BOOST_LOG(info) << "Sent secure desktop notification to main process (0x02)";
      }
    } else if (!isSecure && g_secure_desktop_detected) {
      BOOST_LOG(info) << "Returned to normal desktop";
      g_secure_desktop_detected = false;
    }
  }
}

/**
 * @brief Helper function to get the system temp directory for log files.
 * @return Path to a log file in the system temp directory, or current directory as fallback.
 */
// Helper function to get the system temp directory
std::string get_temp_log_path() {
  wchar_t tempPath[MAX_PATH] = {0};
  if (auto len = GetTempPathW(MAX_PATH, tempPath); len == 0 || len > MAX_PATH) {
    // fallback to current directory if temp path fails
    return "sunshine_wgc_helper.log";
  }
  std::wstring wlog = std::wstring(tempPath) + L"sunshine_wgc_helper.log";
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
 * @param message The received message bytes.
 * @param last_msg_time Reference to track the last message timestamp for heartbeat monitoring.
 */
// Helper function to handle config messages
void handleIPCMessage(const std::vector<uint8_t> &message, std::chrono::steady_clock::time_point &last_msg_time) {
  // Heartbeat message: single byte 0x01
  if (message.size() == 1 && message[0] == 0x01) {
    last_msg_time = std::chrono::steady_clock::now();
    return;
  }
  // Handle config data message
  if (message.size() == sizeof(platf::dxgi::ConfigData) && !g_config_received) {
    memcpy(&g_config, message.data(), sizeof(platf::dxgi::ConfigData));
    g_config_received = true;
    // If log_level in config differs from current, update log filter
    if (INITIAL_LOG_LEVEL != g_config.log_level) {
      // Update log filter to new log level
      boost::log::core::get()->set_filter(
        severity >= g_config.log_level
      );
      BOOST_LOG(info) << "Log level updated from config: " << g_config.log_level;
    }
    BOOST_LOG(info) << "Received config data: " << g_config.width << "x" << g_config.height
                    << ", fps: " << g_config.framerate << ", hdr: " << g_config.dynamicRange
                    << ", display: '" << winrt::to_string(g_config.displayName) << "'";
  }
}

/**
 * @brief Helper function to setup the communication pipe with the main process.
 * @param communicationPipe Reference to the AsyncNamedPipe to configure.
 * @param last_msg_time Reference to track heartbeat timing.
 * @return `true` if the pipe was successfully set up and started, `false` otherwise.
 */
// Helper function to setup communication pipe
bool setupCommunicationPipe(AsyncNamedPipe &communicationPipe, std::chrono::steady_clock::time_point &last_msg_time) {
  auto onMessage = [&last_msg_time](const std::vector<uint8_t> &message) {
    handleIPCMessage(message, last_msg_time);
  };

  auto onError = [](std::string_view /*err*/) {
    // Error handler, intentionally left empty or log as needed
  };

  return communicationPipe.start(onMessage, onError);
}

int main(int argc, char *argv[]) {
  // Set up default config and log level
  auto log_deinit = logging::init(2, get_temp_log_path());

  // Heartbeat mechanism: track last heartbeat time
  auto last_msg_time = std::chrono::steady_clock::now();

  // Initialize system settings (DPI awareness, thread priority, MMCSS)
  SystemInitializer systemInitializer;
  if (!systemInitializer.initializeAll()) {
    BOOST_LOG(error) << "System initialization failed, exiting...";
    return 1;
  }

  // Debug: Verify system settings
  BOOST_LOG(info) << "System initialization successful";
  BOOST_LOG(debug) << "DPI awareness set: " << (systemInitializer.isDpiAwarenessSet() ? "YES" : "NO");
  BOOST_LOG(debug) << "Thread priority set: " << (systemInitializer.isThreadPrioritySet() ? "YES" : "NO");
  BOOST_LOG(debug) << "MMCSS characteristics set: " << (systemInitializer.isMmcssCharacteristicsSet() ? "YES" : "NO");

  BOOST_LOG(info) << "Starting Windows Graphics Capture helper process...";

  // Create named pipe for communication with main process
  AnonymousPipeConnector pipeFactory;

  auto commPipe = pipeFactory.create_client("SunshineWGCPipe", "SunshineWGCEvent");
  AsyncNamedPipe communicationPipe(std::move(commPipe));
  g_communication_pipe = &communicationPipe;  // Store global reference for session.Closed handler

  if (!setupCommunicationPipe(communicationPipe, last_msg_time)) {
    BOOST_LOG(error) << "Failed to start communication pipe";
    return 1;
  }

  // Create D3D11 device and context
  D3D11DeviceManager d3d11Manager;
  if (!d3d11Manager.initializeAll()) {
    BOOST_LOG(error) << "D3D11 device initialization failed, exiting...";
    return 1;
  }

  // Monitor management
  DisplayManager displayManager;
  if (!displayManager.selectMonitor(g_config)) {
    BOOST_LOG(error) << "Monitor selection failed, exiting...";
    return 1;
  }

  if (!displayManager.getMonitorInfo()) {
    BOOST_LOG(error) << "Failed to get monitor info, exiting...";
    return 1;
  }

  // Create GraphicsCaptureItem for monitor using interop
  GraphicsCaptureItem item = nullptr;
  if (!displayManager.createGraphicsCaptureItem(item)) {
    d3d11Manager.cleanup();
    return 1;
  }

  // Calculate final resolution based on config and monitor info
  displayManager.calculateFinalResolution(g_config, g_config_received, item);

  // Choose format based on config.dynamicRange
  DXGI_FORMAT captureFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
  if (g_config_received && g_config.dynamicRange) {
    captureFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
  }

  // Create shared resource manager for texture, keyed mutex, and metadata
  SharedResourceManager sharedResourceManager;
  if (!sharedResourceManager.initializeAll(d3d11Manager.getDevice(), displayManager.getFinalWidth(), displayManager.getFinalHeight(), captureFormat)) {
    return 1;
  }

  // Send shared handle data via named pipe to main process
  platf::dxgi::SharedHandleData handleData = sharedResourceManager.getSharedHandleData();
  std::vector<uint8_t> handleMessage(sizeof(platf::dxgi::SharedHandleData));
  memcpy(handleMessage.data(), &handleData, sizeof(platf::dxgi::SharedHandleData));

  // Wait for connection and send the handle data
  BOOST_LOG(info) << "Waiting for main process to connect...";
  while (!communicationPipe.isConnected()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  BOOST_LOG(info) << "Connected! Sending handle data...";
  communicationPipe.asyncSend(handleMessage);

  // Create WGC capture manager
  WgcCaptureManager wgcManager;
  if (!wgcManager.createFramePool(d3d11Manager.getWinRtDevice(), captureFormat, displayManager.getFinalWidth(), displayManager.getFinalHeight())) {
    BOOST_LOG(error) << "Failed to create frame pool";
    return 1;
  }

  wgcManager.attachFrameArrivedHandler(&sharedResourceManager, d3d11Manager.getContext(), &communicationPipe);

  if (!wgcManager.createCaptureSession(item)) {
    BOOST_LOG(error) << "Failed to create capture session";
    return 1;
  }

  // Set up desktop switch hook for secure desktop detection
  BOOST_LOG(info) << "Setting up desktop switch hook...";
  if (HWINEVENTHOOK raw_hook = SetWinEventHook(
    EVENT_SYSTEM_DESKTOPSWITCH,
    EVENT_SYSTEM_DESKTOPSWITCH,
    nullptr,
    DesktopSwitchHookProc,
    0,
    0,
    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
  ); !raw_hook) {
    BOOST_LOG(error) << "Failed to set up desktop switch hook: " << GetLastError();
  } else {
    g_desktop_switch_hook.reset(raw_hook);
    BOOST_LOG(info) << "Desktop switch hook installed successfully";
  }

  wgcManager.startCapture();

  // Keep running until main process disconnects
  // We need to pump messages for the desktop switch hook to work
  // Reduced polling interval to 1ms for more responsive IPC and lower jitter
  MSG msg;
  while (communicationPipe.isConnected()) {
    // Process any pending messages for the hook
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    // Heartbeat timeout check

    auto now = std::chrono::steady_clock::now();
    if (auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_msg_time); elapsed.count() > 5) {
      BOOST_LOG(warning) << "No heartbeat received from main process for 5 seconds, exiting...";
      _exit(1);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Reduced from 5ms for lower IPC jitter
  }

  BOOST_LOG(info) << "Main process disconnected, shutting down...";

  // Cleanup is handled automatically by RAII destructors
  wgcManager.cleanup();
  communicationPipe.stop();

  // Flush logs before exit
  boost::log::core::get()->flush();

  BOOST_LOG(info) << "WGC Helper process terminated";

  // Cleanup managed by destructors
  return 0;
}
