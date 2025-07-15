// sunshine_wgc_helper.cpp
// Windows Graphics Capture helper process for Sunshine

#define WIN32_LEAN_AND_MEAN
#include "src/platform/windows/wgc/shared_memory.h"

#include <avrt.h>  // For MMCSS
#include <d3d11.h>
#include <dxgi1_2.h>
#include <inspectable.h>  // For IInspectable
#include <psapi.h>  // For GetModuleBaseName
#include <shellscalingapi.h>  // For DPI awareness
#include <tlhelp32.h>  // For process enumeration
#include <windows.h>
#include <winrt/base.h>
#include <winrt/windows.foundation.h>
#include <winrt/windows.foundation.metadata.h>
#include <winrt/windows.graphics.directx.direct3d11.h>
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
  virtual HRESULT __stdcall GetInterface(REFIID id, void **object) = 0;
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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>  // For std::wofstream
#include <iomanip>  // for std::fixed, std::setprecision
#include <iostream>
#include <mutex>
#include <queue>
#include <shlobj.h>  // For SHGetFolderPathW and CSIDL_DESKTOPDIRECTORY
#include <string>
#include <thread>
#include <windows.graphics.capture.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>  // For ApiInformation
#include <winrt/windows.graphics.capture.h>

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

// Structure for shared handle data sent via named pipe
struct SharedHandleData {
  HANDLE textureHandle;
  UINT width;
  UINT height;
};

// Structure for frame metadata shared between processes
struct FrameMetadata {
  uint64_t qpc_timestamp;  // QPC timestamp when frame was captured
  uint32_t frame_sequence;  // Sequential frame number
  uint32_t suppressed_frames;  // Number of frames suppressed since last signal
};

// Structure for config data received from main process
struct ConfigData {
  UINT width;
  UINT height;
  int framerate;
  int dynamicRange;
  wchar_t displayName[32];  // Display device name (e.g., "\\.\\DISPLAY1")
};

// Global config data received from main process
ConfigData g_config = {0, 0, 0, 0, L""};
bool g_config_received = false;

// Global variables for frame metadata and rate limiting
HANDLE g_metadata_mapping = nullptr;
FrameMetadata *g_frame_metadata = nullptr;
uint32_t g_frame_sequence = 0;

// Global communication pipe for sending session closed notifications
AsyncNamedPipe *g_communication_pipe = nullptr;

// Global variables for desktop switch detection
HWINEVENTHOOK g_desktop_switch_hook = nullptr;
bool g_secure_desktop_detected = false;

// Async logging system to avoid blocking frame processing
struct LogMessage {
  std::wstring message;
  std::chrono::steady_clock::time_point timestamp;
};

class AsyncLogger {
private:
  std::queue<LogMessage> log_queue;
  std::mutex queue_mutex;
  std::condition_variable cv;
  std::atomic<bool> should_stop {false};
  std::thread logger_thread;
  std::wofstream *log_file = nullptr;

public:
  void start(std::wofstream *file) {
    log_file = file;
    logger_thread = std::thread([this]() {
      while (!should_stop) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        cv.wait(lock, [this]() {
          return !log_queue.empty() || should_stop;
        });

        while (!log_queue.empty()) {
          LogMessage msg = log_queue.front();
          log_queue.pop();
          lock.unlock();

          // Write to log file or console
          if (log_file && log_file->is_open()) {
            *log_file << msg.message << std::flush;
          } else {
            std::wcout << msg.message << std::flush;
          }

          lock.lock();
        }
      }
    });
  }

  void log(const std::wstring &message) {
    if (should_stop) {
      return;
    }

    std::lock_guard<std::mutex> lock(queue_mutex);
    log_queue.push({message, std::chrono::steady_clock::now()});
    cv.notify_one();
  }

  void stop() {
    should_stop = true;
    cv.notify_all();
    if (logger_thread.joinable()) {
      logger_thread.join();
    }
  }
};

AsyncLogger g_async_logger;

// System initialization class to handle DPI, threading, and MMCSS setup
class SystemInitializer {
private:
  HANDLE mmcss_handle = nullptr;
  bool dpi_awareness_set = false;
  bool thread_priority_set = false;
  bool mmcss_characteristics_set = false;

public:
  bool initializeDpiAwareness() {
    // Set DPI awareness to prevent zoomed display issues
    // Try the newer API first (Windows 10 1703+), fallback to older API
    typedef BOOL(WINAPI * SetProcessDpiAwarenessContextFunc)(DPI_AWARENESS_CONTEXT);
    typedef HRESULT(WINAPI * SetProcessDpiAwarenessFunc)(PROCESS_DPI_AWARENESS);

    HMODULE user32 = GetModuleHandleA("user32.dll");
    bool dpi_set = false;
    if (user32) {
      auto setDpiContextFunc = (SetProcessDpiAwarenessContextFunc) GetProcAddress(user32, "SetProcessDpiAwarenessContext");
      if (setDpiContextFunc) {
        dpi_set = setDpiContextFunc((DPI_AWARENESS_CONTEXT) -4);  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
      }
    }

    if (!dpi_set) {
      // Fallback for older Windows versions
      if (FAILED(SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE))) {
        std::wcerr << L"[WGC Helper] Warning: Failed to set DPI awareness, display scaling issues may occur" << std::endl;
        return false;
      }
    }
    
    dpi_awareness_set = true;
    return true;
  }

  bool initializeThreadPriority() {
    // Increase thread priority to minimize scheduling delay
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
      std::wcerr << L"[WGC Helper] Failed to set thread priority: " << GetLastError() << std::endl;
      return false;
    }
    
    thread_priority_set = true;
    return true;
  }

  bool initializeMmcssCharacteristics() {
    // Set MMCSS for real-time scheduling - try "Pro Audio" for lower latency
    DWORD taskIdx = 0;
    mmcss_handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIdx);
    if (!mmcss_handle) {
      // Fallback to "Games" if "Pro Audio" fails
      mmcss_handle = AvSetMmThreadCharacteristicsW(L"Games", &taskIdx);
      if (!mmcss_handle) {
        std::wcerr << L"[WGC Helper] Failed to set MMCSS characteristics: " << GetLastError() << std::endl;
        return false;
      }
    }
    
    mmcss_characteristics_set = true;
    return true;
  }

  bool initializeWinRtApartment() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    return true;
  }

  bool initializeAll() {
    bool success = true;
    success &= initializeDpiAwareness();
    success &= initializeThreadPriority();
    success &= initializeMmcssCharacteristics();
    success &= initializeWinRtApartment();
    return success;
  }

  void cleanup() {
    if (mmcss_handle && mmcss_characteristics_set) {
      AvRevertMmThreadCharacteristics(mmcss_handle);
      mmcss_handle = nullptr;
      mmcss_characteristics_set = false;
    }
  }

  bool isDpiAwarenessSet() const { return dpi_awareness_set; }
  bool isThreadPrioritySet() const { return thread_priority_set; }
  bool isMmcssCharacteristicsSet() const { return mmcss_characteristics_set; }

  ~SystemInitializer() {
    cleanup();
  }
};

// Log file management class to handle redirection and file creation
class LogFileManager {
private:
  std::wstring log_file_path;
  std::wofstream log_file;
  bool file_logging_enabled = false;

public:
  bool setupLogFile() {
    // Debug: First output to console to see if we get here
    std::wcout << L"[WGC Helper] Setting up log file redirection..." << std::endl;

    // Try multiple locations in order of preference
    std::vector<std::wstring> logPaths;

    // 1. Desktop (if accessible)
    wchar_t desktopPath[MAX_PATH] = {0};
    HRESULT hr_desktop = SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath);
    std::wcout << L"[WGC Helper] Desktop path result: " << hr_desktop << L", path: " << (SUCCEEDED(hr_desktop) ? desktopPath : L"FAILED") << std::endl;
    if (SUCCEEDED(hr_desktop)) {
      std::wstring desktopLog = desktopPath;
      if (desktopLog.back() != L'\\') {
        desktopLog += L'\\';
      }
      desktopLog += L"sunshine_wgc_helper.log";
      logPaths.push_back(desktopLog);
      std::wcout << L"[WGC Helper] Added desktop path: " << desktopLog << std::endl;
    }

    // 2. User's temp directory
    wchar_t tempPath[MAX_PATH] = {0};
    DWORD tempResult = GetTempPathW(MAX_PATH, tempPath);
    std::wcout << L"[WGC Helper] Temp path result: " << tempResult << L", path: " << (tempResult > 0 ? tempPath : L"FAILED") << std::endl;
    if (tempResult > 0) {
      std::wstring tempLog = tempPath;
      if (tempLog.back() != L'\\') {
        tempLog += L'\\';
      }
      tempLog += L"sunshine_wgc_helper.log";
      logPaths.push_back(tempLog);
      std::wcout << L"[WGC Helper] Added temp path: " << tempLog << std::endl;
    }

    // 3. Current working directory
    logPaths.push_back(L"sunshine_wgc_helper.log");
    std::wcout << L"[WGC Helper] Added current dir path: sunshine_wgc_helper.log" << std::endl;

    // 4. System temp directory as last resort
    logPaths.push_back(L"C:\\Windows\\Temp\\sunshine_wgc_helper.log");
    std::wcout << L"[WGC Helper] Added system temp path: C:\\Windows\\Temp\\sunshine_wgc_helper.log" << std::endl;

    // Try each path until one works
    for (const auto &path : logPaths) {
      std::wcout << L"[WGC Helper] Trying log path: " << path << std::endl;
      log_file.open(path.c_str(), std::ios::out | std::ios::trunc);
      if (log_file.is_open()) {
        log_file_path = path;
        std::wcout << L"[WGC Helper] Successfully opened log file: " << log_file_path << std::endl;
        std::wcout.rdbuf(log_file.rdbuf());
        std::wcerr.rdbuf(log_file.rdbuf());
        file_logging_enabled = true;
        break;
      } else {
        std::wcout << L"[WGC Helper] Failed to open: " << path << std::endl;
      }
    }

    if (!log_file.is_open()) {
      // If all locations fail, continue without file logging
      // At least output to console will still work
      std::wcerr << L"[WGC Helper] Warning: Could not create log file at any location, using console output only" << std::endl;
      log_file_path = L"(console only)";
      file_logging_enabled = false;
    }

    std::wcout << L"[WGC Helper] Final log file path: " << log_file_path << std::endl;
    return true;
  }

  void startAsyncLogger() {
    g_async_logger.start(file_logging_enabled ? &log_file : nullptr);
  }

  void stopAsyncLogger() {
    g_async_logger.stop();
  }

  bool isFileLoggingEnabled() const { return file_logging_enabled; }
  const std::wstring& getLogFilePath() const { return log_file_path; }
  std::wofstream* getLogFile() { return file_logging_enabled ? &log_file : nullptr; }
};

// D3D11 device management class to handle device creation and WinRT interop
class D3D11DeviceManager {
private:
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  D3D_FEATURE_LEVEL feature_level;
  winrt::com_ptr<IDXGIDevice> dxgi_device;
  winrt::com_ptr<::IDirect3DDevice> interop_device;
  IDirect3DDevice winrt_device = nullptr;

public:
  bool createDevice() {
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &feature_level, &context);
    if (FAILED(hr)) {
      std::wcerr << L"[WGC Helper] Failed to create D3D11 device" << std::endl;
      return false;
    }
    return true;
  }

  bool createWinRtInterop() {
    if (!device) {
      return false;
    }

    HRESULT hr = device->QueryInterface(__uuidof(IDXGIDevice), dxgi_device.put_void());
    if (FAILED(hr)) {
      std::wcerr << L"[WGC Helper] Failed to get DXGI device" << std::endl;
      return false;
    }
    
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.get(), reinterpret_cast<::IInspectable **>(interop_device.put_void()));
    if (FAILED(hr)) {
      std::wcerr << L"[WGC Helper] Failed to create interop device" << std::endl;
      return false;
    }
    
    winrt_device = interop_device.as<IDirect3DDevice>();
    return true;
  }

  bool initializeAll() {
    return createDevice() && createWinRtInterop();
  }

  void cleanup() {
    if (context) {
      context->Release();
      context = nullptr;
    }
    if (device) {
      device->Release();
      device = nullptr;
    }
  }

  ID3D11Device* getDevice() const { return device; }
  ID3D11DeviceContext* getContext() const { return context; }
  IDirect3DDevice getWinRtDevice() const { return winrt_device; }

  ~D3D11DeviceManager() {
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
  bool selectMonitor(const ConfigData& config) {
    if (config.displayName[0] != L'\0') {
      // Enumerate monitors to find one matching displayName
      struct EnumData {
        const wchar_t *targetName;
        HMONITOR foundMonitor;
      } enumData = {config.displayName, nullptr};

      auto enumProc = [](HMONITOR hMon, HDC, LPRECT, LPARAM lParam) -> BOOL {
        EnumData *data = reinterpret_cast<EnumData *>(lParam);
        MONITORINFOEXW info = {sizeof(MONITORINFOEXW)};
        if (GetMonitorInfoW(hMon, &info)) {
          if (wcsncmp(info.szDevice, data->targetName, 32) == 0) {
            data->foundMonitor = hMon;
            return FALSE;  // Stop enumeration
          }
        }
        return TRUE;
      };
      EnumDisplayMonitors(nullptr, nullptr, enumProc, reinterpret_cast<LPARAM>(&enumData));
      selected_monitor = enumData.foundMonitor;
      if (!selected_monitor) {
        std::wcerr << L"[WGC Helper] Could not find monitor with name '" << config.displayName << L"', falling back to primary." << std::endl;
      }
    }
    
    if (!selected_monitor) {
      selected_monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
      if (!selected_monitor) {
        std::wcerr << L"[WGC Helper] Failed to get primary monitor" << std::endl;
        return false;
      }
    }
    return true;
  }

  bool getMonitorInfo() {
    if (!selected_monitor) {
      return false;
    }
    
    if (!GetMonitorInfo(selected_monitor, &monitor_info)) {
      std::wcerr << L"[WGC Helper] Failed to get monitor info" << std::endl;
      return false;
    }
    
    fallback_width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
    fallback_height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
    return true;
  }

  bool createGraphicsCaptureItem(GraphicsCaptureItem& item) {
    if (!selected_monitor) {
      return false;
    }
    
    auto activationFactory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    HRESULT hr = activationFactory->CreateForMonitor(selected_monitor, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item));
    if (FAILED(hr)) {
      std::wcerr << L"[WGC Helper] Failed to create GraphicsCaptureItem for monitor: " << hr << std::endl;
      return false;
    }
    return true;
  }

  void calculateFinalResolution(const ConfigData& config, bool config_received, GraphicsCaptureItem& item) {
    // Get actual WGC item size to ensure we capture full desktop (fixes zoomed display issue)
    auto item_size = item.Size();
    UINT wgc_width = static_cast<UINT>(item_size.Width);
    UINT wgc_height = static_cast<UINT>(item_size.Height);

    std::wcout << L"[WGC Helper] WGC item reports size: " << wgc_width << L"x" << wgc_height << std::endl;
    std::wcout << L"[WGC Helper] Monitor logical size: " << fallback_width << L"x" << fallback_height << std::endl;
    std::wcout << L"[WGC Helper] Config requested size: " << (config_received ? config.width : 0) << L"x" << (config_received ? config.height : 0) << std::endl;

    if (config_received && config.width > 0 && config.height > 0) {
      final_width = config.width;
      final_height = config.height;
      std::wcout << L"[WGC Helper] Using config resolution: " << final_width << L"x" << final_height << std::endl;
    } else {
      final_width = fallback_width;
      final_height = fallback_height;
      std::wcout << L"[WGC Helper] No valid config resolution received, falling back to monitor: " << final_width << L"x" << final_height << std::endl;
    }

    // Use physical size from WGC to avoid DPI scaling issues
    // This ensures we capture the full desktop without cropping/zooming
    if (wgc_width > 0 && wgc_height > 0) {
      // Check if there's a significant difference (indicating DPI scaling)
      bool scaling_detected = (abs(static_cast<int>(wgc_width) - static_cast<int>(fallback_width)) > 100) ||
                              (abs(static_cast<int>(wgc_height) - static_cast<int>(fallback_height)) > 100);

      if (scaling_detected) {
        std::wcout << L"[WGC Helper] DPI scaling detected - using WGC physical size to avoid zoom issues" << std::endl;
      }

      final_width = wgc_width;
      final_height = wgc_height;
      std::wcout << L"[WGC Helper] Final resolution (physical): " << final_width << L"x" << final_height << std::endl;
    }
  }

  HMONITOR getSelectedMonitor() const { return selected_monitor; }
  UINT getFinalWidth() const { return final_width; }
  UINT getFinalHeight() const { return final_height; }
  UINT getFallbackWidth() const { return fallback_width; }
  UINT getFallbackHeight() const { return fallback_height; }
};

// Shared resource management class to handle texture, memory mapping, and events
class SharedResourceManager {
private:
  ID3D11Texture2D *shared_texture = nullptr;
  IDXGIKeyedMutex *keyed_mutex = nullptr;
  HANDLE shared_handle = nullptr;
  HANDLE metadata_mapping = nullptr;
  FrameMetadata *frame_metadata = nullptr;
  HANDLE frame_event = nullptr;
  UINT width = 0;
  UINT height = 0;

public:
  bool createSharedTexture(ID3D11Device* device, UINT texture_width, UINT texture_height, DXGI_FORMAT format) {
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
    
    HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, &shared_texture);
    if (FAILED(hr)) {
      std::wcerr << L"[WGC Helper] Failed to create shared texture" << std::endl;
      return false;
    }
    return true;
  }

  bool createKeyedMutex() {
    if (!shared_texture) {
      return false;
    }
    
    HRESULT hr = shared_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&keyed_mutex));
    if (FAILED(hr)) {
      std::wcerr << L"[WGC Helper] Failed to get keyed mutex" << std::endl;
      return false;
    }
    return true;
  }

  bool createSharedHandle() {
    if (!shared_texture) {
      return false;
    }
    
    IDXGIResource *dxgiResource = nullptr;
    HRESULT hr = shared_texture->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void **>(&dxgiResource));
    hr = dxgiResource->GetSharedHandle(&shared_handle);
    dxgiResource->Release();
    if (FAILED(hr) || !shared_handle) {
      std::wcerr << L"[WGC Helper] Failed to get shared handle" << std::endl;
      return false;
    }
    
    std::wcout << L"[WGC Helper] Created shared texture: " << width << L"x" << height
               << L", handle: " << std::hex << reinterpret_cast<uintptr_t>(shared_handle) << std::dec << std::endl;
    return true;
  }

  bool createFrameMetadataMapping() {
    metadata_mapping = CreateFileMappingW(
      INVALID_HANDLE_VALUE,
      nullptr,
      PAGE_READWRITE,
      0,
      sizeof(FrameMetadata),
      L"Local\\SunshineWGCMetadata"
    );
    if (!metadata_mapping) {
      std::wcerr << L"[WGC Helper] Failed to create metadata mapping: " << GetLastError() << std::endl;
      return false;
    }

    frame_metadata = static_cast<FrameMetadata *>(MapViewOfFile(
      metadata_mapping,
      FILE_MAP_ALL_ACCESS,
      0,
      0,
      sizeof(FrameMetadata)
    ));
    if (!frame_metadata) {
      std::wcerr << L"[WGC Helper] Failed to map metadata view: " << GetLastError() << std::endl;
      CloseHandle(metadata_mapping);
      metadata_mapping = nullptr;
      return false;
    }

    // Initialize metadata
    memset(frame_metadata, 0, sizeof(FrameMetadata));
    std::wcout << L"[WGC Helper] Created frame metadata shared memory" << std::endl;
    return true;
  }

  bool createFrameEvent() {
    frame_event = CreateEventW(nullptr, FALSE, FALSE, L"Local\\SunshineWGCFrame");
    if (!frame_event) {
      std::wcerr << L"[WGC Helper] Failed to create frame event" << std::endl;
      return false;
    }
    return true;
  }

  bool initializeAll(ID3D11Device* device, UINT texture_width, UINT texture_height, DXGI_FORMAT format) {
    return createSharedTexture(device, texture_width, texture_height, format) &&
           createKeyedMutex() &&
           createSharedHandle() &&
           createFrameMetadataMapping() &&
           createFrameEvent();
  }

  SharedHandleData getSharedHandleData() const {
    return {shared_handle, width, height};
  }

  void cleanup() {
    if (frame_metadata) {
      UnmapViewOfFile(frame_metadata);
      frame_metadata = nullptr;
    }
    if (metadata_mapping) {
      CloseHandle(metadata_mapping);
      metadata_mapping = nullptr;
    }
    if (frame_event) {
      CloseHandle(frame_event);
      frame_event = nullptr;
    }
    if (keyed_mutex) {
      keyed_mutex->Release();
      keyed_mutex = nullptr;
    }
    if (shared_texture) {
      shared_texture->Release();
      shared_texture = nullptr;
    }
  }

  ID3D11Texture2D* getSharedTexture() const { return shared_texture; }
  IDXGIKeyedMutex* getKeyedMutex() const { return keyed_mutex; }
  HANDLE getFrameEvent() const { return frame_event; }
  FrameMetadata* getFrameMetadata() const { return frame_metadata; }

  ~SharedResourceManager() {
    cleanup();
  }
};

// WGC capture management class to handle frame pool, capture session, and frame processing
class WgcCaptureManager {
private:
  Direct3D11CaptureFramePool frame_pool = nullptr;
  GraphicsCaptureSession capture_session = nullptr;
  winrt::event_token frame_arrived_token {};
  SharedResourceManager* resource_manager = nullptr;
  ID3D11DeviceContext* d3d_context = nullptr;
  
  // Frame processing state
  static std::chrono::steady_clock::time_point last_delivery_time;
  static bool first_frame;
  static uint32_t delivery_count;
  static std::chrono::milliseconds total_delivery_time;

public:
  bool createFramePool(IDirect3DDevice winrt_device, DXGI_FORMAT capture_format, UINT width, UINT height) {
    const uint32_t kPoolFrames = 2;
    frame_pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
      winrt_device,
      (capture_format == DXGI_FORMAT_R16G16B16A16_FLOAT) ? DirectXPixelFormat::R16G16B16A16Float : DirectXPixelFormat::B8G8R8A8UIntNormalized,
      kPoolFrames,
      SizeInt32 {static_cast<int32_t>(width), static_cast<int32_t>(height)}
    );
    return frame_pool != nullptr;
  }

  void attachFrameArrivedHandler(SharedResourceManager* res_mgr, ID3D11DeviceContext* context) {
    resource_manager = res_mgr;
    d3d_context = context;
    
    frame_arrived_token = frame_pool.FrameArrived([this](Direct3D11CaptureFramePool const &sender, winrt::Windows::Foundation::IInspectable const &) {
      processFrame(sender);
    });
  }

  void processFrame(Direct3D11CaptureFramePool const &sender) {
    // Timestamp #1: Very beginning of FrameArrived handler
    uint64_t timestamp_frame_arrived = qpc_counter();

    auto frame = sender.TryGetNextFrame();
    if (frame) {
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

        // Queue timing log every 300 frames (non-blocking)
        if (delivery_count % 300 == 0) {
          auto avg_delivery_ms = total_delivery_time.count() / delivery_count;
          auto expected_ms = (g_config_received && g_config.framerate > 0) ? (1000 / g_config.framerate) : 16;

          std::wstringstream ss;
          ss << L"[WGC Helper] Frame delivery timing - "
             << L"Avg interval: " << avg_delivery_ms << L"ms, "
             << L"Expected: " << expected_ms << L"ms, "
             << L"Last: " << delivery_interval.count() << L"ms" << std::endl;
          g_async_logger.log(ss.str());

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
        // Queue error log (non-blocking)
        std::wstringstream ss;
        ss << L"[WGC Helper] WinRT error in frame processing: " << ex.code()
           << L" - " << winrt::to_string(ex.message()).c_str() << std::endl;
        g_async_logger.log(ss.str());
      }
      surface.Close();
      frame.Close();
    }
  }

  void processSurfaceToTexture(winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface surface, 
                               uint64_t timestamp_frame_arrived, uint64_t frame_qpc) {
    if (!resource_manager || !d3d_context) {
      return;
    }

    winrt::com_ptr<IDirect3DDxgiInterfaceAccess> interfaceAccess;
    HRESULT hr = surface.as<::IUnknown>()->QueryInterface(__uuidof(IDirect3DDxgiInterfaceAccess), reinterpret_cast<void **>(interfaceAccess.put()));
    if (SUCCEEDED(hr)) {
      winrt::com_ptr<ID3D11Texture2D> frameTexture;
      hr = interfaceAccess->GetInterface(__uuidof(ID3D11Texture2D), frameTexture.put_void());
      if (SUCCEEDED(hr)) {
        hr = resource_manager->getKeyedMutex()->AcquireSync(0, INFINITE);
        if (SUCCEEDED(hr)) {
          d3d_context->CopyResource(resource_manager->getSharedTexture(), frameTexture.get());

          // Timestamp #2: Immediately after CopyResource completes
          uint64_t timestamp_after_copy = qpc_counter();

          updateFrameMetadata(frame_qpc);

          resource_manager->getKeyedMutex()->ReleaseSync(1);

          // Timestamp #3: Immediately after SetEvent is called
          uint64_t timestamp_after_set_event = qpc_counter();
          SetEvent(resource_manager->getFrameEvent());

          logFrameTiming(timestamp_frame_arrived, timestamp_after_copy, timestamp_after_set_event);
        } else {
          // Queue error log (non-blocking)
          std::wstringstream ss;
          ss << L"[WGC Helper] Failed to acquire keyed mutex: " << hr << std::endl;
          g_async_logger.log(ss.str());
        }
      } else {
        // Queue error log (non-blocking)
        std::wstringstream ss;
        ss << L"[WGC Helper] Failed to get ID3D11Texture2D from interface: " << hr << std::endl;
        g_async_logger.log(ss.str());
      }
    } else {
      // Queue error log (non-blocking)
      std::wstringstream ss;
      ss << L"[WGC Helper] Failed to query IDirect3DDxgiInterfaceAccess: " << hr << std::endl;
      g_async_logger.log(ss.str());
    }
  }

  void updateFrameMetadata(uint64_t frame_qpc) {
    if (!resource_manager || !resource_manager->getFrameMetadata()) {
      return;
    }

    auto metadata = resource_manager->getFrameMetadata();
    metadata->qpc_timestamp = frame_qpc;
    metadata->frame_sequence = ++g_frame_sequence;
    metadata->suppressed_frames = 0;  // No suppression - always 0

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
        double fps = 600.0 * qpc_freq / double(frame_qpc - lastQpc);

        std::wstringstream ss;
        ss << L"[WGC Helper] delivered " << std::fixed << std::setprecision(1) << fps << L" fps (target: "
           << (g_config_received ? g_config.framerate : 60) << L")" << std::endl;
        g_async_logger.log(ss.str());
      }
      lastQpc = frame_qpc;
    }

    // Log performance stats periodically (async)
    if ((g_frame_sequence % 1500) == 0) {
      std::wstringstream ss;
      ss << L"[WGC Helper] Frame " << g_frame_sequence
         << L" processed without suppression" << std::endl;
      g_async_logger.log(ss.str());
    }
  }

  void logFrameTiming(uint64_t timestamp_frame_arrived, uint64_t timestamp_after_copy, uint64_t timestamp_after_set_event) {
    // Log high-precision timing deltas every 300 frames (async)
    static uint32_t timing_log_counter = 0;
    if ((++timing_log_counter % 300) == 0) {
      static uint64_t qpc_freq_timing = 0;
      if (qpc_freq_timing == 0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        qpc_freq_timing = freq.QuadPart;
      }

      double arrived_to_copy_us = (double) (timestamp_after_copy - timestamp_frame_arrived) * 1000000.0 / qpc_freq_timing;
      double copy_to_signal_us = (double) (timestamp_after_set_event - timestamp_after_copy) * 1000000.0 / qpc_freq_timing;
      double total_frame_us = (double) (timestamp_after_set_event - timestamp_frame_arrived) * 1000000.0 / qpc_freq_timing;

      std::wstringstream ss;
      ss << L"[WGC Helper] Frame timing - "
         << L"Arrived->Copy: " << std::fixed << std::setprecision(1) << arrived_to_copy_us << L"μs, "
         << L"Copy->Signal: " << copy_to_signal_us << L"μs, "
         << L"Total: " << total_frame_us << L"μs" << std::endl;
      g_async_logger.log(ss.str());
    }
  }

  bool createCaptureSession(GraphicsCaptureItem item) {
    if (!frame_pool) {
      return false;
    }
    
    capture_session = frame_pool.CreateCaptureSession(item);
    capture_session.IsBorderRequired(false);

    if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"MinUpdateInterval")) {
      capture_session.MinUpdateInterval(winrt::Windows::Foundation::TimeSpan {10000});
      std::wcout << L"[WGC Helper] Successfully set the MinUpdateInterval (120fps+)" << std::endl;
    }
    return true;
  }

  void startCapture() {
    if (capture_session) {
      capture_session.StartCapture();
      std::wcout << L"[WGC Helper] Helper process started. Capturing frames using WGC..." << std::endl;
    }
  }

  void cleanup() {
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

  ~WgcCaptureManager() {
    cleanup();
  }
};

// Initialize static members
std::chrono::steady_clock::time_point WgcCaptureManager::last_delivery_time = std::chrono::steady_clock::time_point {};
bool WgcCaptureManager::first_frame = true;
uint32_t WgcCaptureManager::delivery_count = 0;
std::chrono::milliseconds WgcCaptureManager::total_delivery_time {0};

// Function to check if a process with the given name is running
bool IsProcessRunning(const std::wstring &processName) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return false;
  }

  PROCESSENTRY32W processEntry = {};
  processEntry.dwSize = sizeof(processEntry);

  bool found = false;
  if (Process32FirstW(snapshot, &processEntry)) {
    do {
      if (_wcsicmp(processEntry.szExeFile, processName.c_str()) == 0) {
        found = true;
        break;
      }
    } while (Process32NextW(snapshot, &processEntry));
  }

  CloseHandle(snapshot);
  return found;
}

// Function to check if we're on the secure desktop
bool IsSecureDesktop() {
  // Check for UAC (consent.exe)
  if (IsProcessRunning(L"consent.exe")) {
    return true;
  }

  // Check for login screen by looking for winlogon.exe with specific conditions
  // or check the current desktop name
  HDESK currentDesktop = GetThreadDesktop(GetCurrentThreadId());
  if (currentDesktop) {
    wchar_t desktopName[256] = {0};
    DWORD needed = 0;
    if (GetUserObjectInformationW(currentDesktop, UOI_NAME, desktopName, sizeof(desktopName), &needed)) {
      // Secure desktop typically has names like "Winlogon" or "SAD" (Secure Attention Desktop)
      if (_wcsicmp(desktopName, L"Winlogon") == 0 || _wcsicmp(desktopName, L"SAD") == 0) {
        return true;
      }
    }
  }

  return false;
}

// Desktop switch event hook procedure
void CALLBACK DesktopSwitchHookProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
  if (event == EVENT_SYSTEM_DESKTOPSWITCH) {
    std::wcout << L"[WGC Helper] Desktop switch detected!" << std::endl;

    // Small delay to let the system settle
    Sleep(100);

    bool isSecure = IsSecureDesktop();
    std::wcout << L"[WGC Helper] Desktop switch - Secure desktop: " << (isSecure ? L"YES" : L"NO") << std::endl;

    if (isSecure && !g_secure_desktop_detected) {
      std::wcout << L"[WGC Helper] Secure desktop detected - sending notification to main process" << std::endl;
      g_secure_desktop_detected = true;

      // Send notification to main process
      if (g_communication_pipe && g_communication_pipe->isConnected()) {
        std::vector<uint8_t> sessionClosedMessage = {0x01};  // Simple marker for session closed
        g_communication_pipe->asyncSend(sessionClosedMessage);
        std::wcout << L"[WGC Helper] Sent secure desktop notification to main process" << std::endl;
      }
    } else if (!isSecure && g_secure_desktop_detected) {
      std::wcout << L"[WGC Helper] Returned to normal desktop" << std::endl;
      g_secure_desktop_detected = false;
    }
  }
}

#include <fstream>

int main() {
  // Heartbeat mechanism: track last heartbeat time
  auto last_heartbeat = std::chrono::steady_clock::now();

  // Initialize system settings (DPI awareness, thread priority, MMCSS)
  SystemInitializer systemInitializer;
  if (!systemInitializer.initializeAll()) {
    std::wcerr << L"[WGC Helper] System initialization failed, exiting..." << std::endl;
    return 1;
  }

  // Debug: Verify system settings
  std::wcout << L"[WGC Helper] System initialization successful" << std::endl;
  std::wcout << L"[WGC Helper] DPI awareness set: " << (systemInitializer.isDpiAwarenessSet() ? L"YES" : L"NO") << std::endl;
  std::wcout << L"[WGC Helper] Thread priority set: " << (systemInitializer.isThreadPrioritySet() ? L"YES" : L"NO") << std::endl;
  std::wcout << L"[WGC Helper] MMCSS characteristics set: " << (systemInitializer.isMmcssCharacteristicsSet() ? L"YES" : L"NO") << std::endl;

  // Log file manager for handling log file setup and async logger
  LogFileManager logFileManager;
  if (!logFileManager.setupLogFile()) {
    std::wcerr << L"[WGC Helper] Log file setup failed, exiting..." << std::endl;
    return 1;
  }

  // Start async logger
  logFileManager.startAsyncLogger();

  std::wcout << L"[WGC Helper] Starting Windows Graphics Capture helper process..." << std::endl;

  // Create named pipe for communication with main process
  AsyncNamedPipe communicationPipe(L"\\\\.\\pipe\\SunshineWGCHelper", true);
  g_communication_pipe = &communicationPipe;  // Store global reference for session.Closed handler

  auto onMessage = [&](const std::vector<uint8_t> &message) {
    // Heartbeat message: single byte 0x01
    if (message.size() == 1 && message[0] == 0x01) {
      last_heartbeat = std::chrono::steady_clock::now();
      // Optionally log heartbeat receipt
      // std::wcout << L"[WGC Helper] Heartbeat received" << std::endl;
      return;
    }
    // Handle config data message
    if (message.size() == sizeof(ConfigData) && !g_config_received) {
      memcpy(&g_config, message.data(), sizeof(ConfigData));
      g_config_received = true;
      std::wcout << L"[WGC Helper] Received config data: " << g_config.width << L"x" << g_config.height
                 << L", fps: " << g_config.framerate << L", hdr: " << g_config.dynamicRange
                 << L", display: '" << g_config.displayName << L"'" << std::endl;
    }
  };

  auto onError = [&](const std::string &error) {
    std::wcout << L"[WGC Helper] Pipe error: " << error.c_str() << std::endl;
  };

  if (!communicationPipe.start(onMessage, onError)) {
    std::wcerr << L"[WGC Helper] Failed to start communication pipe" << std::endl;
    return 1;
  }

  // Create D3D11 device and context
  D3D11DeviceManager d3d11Manager;
  if (!d3d11Manager.initializeAll()) {
    std::wcerr << L"[WGC Helper] D3D11 device initialization failed, exiting..." << std::endl;
    return 1;
  }

  // Monitor management
  DisplayManager displayManager;
  if (!displayManager.selectMonitor(g_config)) {
    std::wcerr << L"[WGC Helper] Monitor selection failed, exiting..." << std::endl;
    return 1;
  }

  if (!displayManager.getMonitorInfo()) {
    std::wcerr << L"[WGC Helper] Failed to get monitor info, exiting..." << std::endl;
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
  SharedHandleData handleData = sharedResourceManager.getSharedHandleData();
  std::vector<uint8_t> handleMessage(sizeof(SharedHandleData));
  memcpy(handleMessage.data(), &handleData, sizeof(SharedHandleData));

  // Wait for connection and send the handle data
  std::wcout << L"[WGC Helper] Waiting for main process to connect..." << std::endl;
  while (!communicationPipe.isConnected()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::wcout << L"[WGC Helper] Connected! Sending handle data..." << std::endl;
  communicationPipe.asyncSend(handleMessage);

  // Create WGC capture manager
  WgcCaptureManager wgcManager;
  if (!wgcManager.createFramePool(d3d11Manager.getWinRtDevice(), captureFormat, displayManager.getFinalWidth(), displayManager.getFinalHeight())) {
    std::wcerr << L"[WGC Helper] Failed to create frame pool" << std::endl;
    return 1;
  }

  wgcManager.attachFrameArrivedHandler(&sharedResourceManager, d3d11Manager.getContext());

  if (!wgcManager.createCaptureSession(item)) {
    std::wcerr << L"[WGC Helper] Failed to create capture session" << std::endl;
    return 1;
  }

  // Set up desktop switch hook for secure desktop detection
  std::wcout << L"[WGC Helper] Setting up desktop switch hook..." << std::endl;
  g_desktop_switch_hook = SetWinEventHook(
    EVENT_SYSTEM_DESKTOPSWITCH,
    EVENT_SYSTEM_DESKTOPSWITCH,
    nullptr,
    DesktopSwitchHookProc,
    0,
    0,
    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
  );

  if (!g_desktop_switch_hook) {
    std::wcerr << L"[WGC Helper] Failed to set up desktop switch hook: " << GetLastError() << std::endl;
  } else {
    std::wcout << L"[WGC Helper] Desktop switch hook installed successfully" << std::endl;
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
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat);
    if (elapsed.count() > 5) {
      std::wcout << L"[WGC Helper] No heartbeat received from main process for 5 seconds, exiting..." << std::endl;
      _exit(1);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Reduced from 5ms for lower IPC jitter
  }

  std::wcout << L"[WGC Helper] Main process disconnected, shutting down..." << std::endl;

  // Stop async logger
  logFileManager.stopAsyncLogger();

  // Cleanup
  if (g_desktop_switch_hook) {
    UnhookWinEvent(g_desktop_switch_hook);
    g_desktop_switch_hook = nullptr;
  }
  
  wgcManager.cleanup();
  communicationPipe.stop();

  // Cleanup managed by destructors
  return 0;
}
