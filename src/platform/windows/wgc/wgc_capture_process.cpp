// sunshine_wgc_helper.cpp
// Windows Graphics Capture helper process for Sunshine

#define WIN32_LEAN_AND_MEAN
#include "shared_memory.h"

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

#include <chrono>
#include <fstream>  // For std::wofstream
#include <iomanip>  // for std::fixed, std::setprecision
#include <iostream>
#include <shlobj.h>  // For SHGetFolderPathW and CSIDL_DESKTOPDIRECTORY
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
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
  std::atomic<bool> should_stop{false};
  std::thread logger_thread;
  std::wofstream* log_file = nullptr;

public:
  void start(std::wofstream* file) {
    log_file = file;
    logger_thread = std::thread([this]() {
      while (!should_stop) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        cv.wait(lock, [this]() { return !log_queue.empty() || should_stop; });
        
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

  void log(const std::wstring& message) {
    if (should_stop) return;
    
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
    }
  }

  // Increase thread priority to minimize scheduling delay
  if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
    std::wcerr << L"[WGC Helper] Failed to set thread priority: " << GetLastError() << std::endl;
  }

  // Set MMCSS for real-time scheduling - try "Pro Audio" for lower latency
  DWORD taskIdx = 0;
  HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIdx);
  if (!mmcss) {
    // Fallback to "Games" if "Pro Audio" fails
    mmcss = AvSetMmThreadCharacteristicsW(L"Games", &taskIdx);
    if (!mmcss) {
      std::wcerr << L"[WGC Helper] Failed to set MMCSS characteristics: " << GetLastError() << std::endl;
    }
  }

  winrt::init_apartment(winrt::apartment_type::multi_threaded);

  // Redirect wcout and wcerr to a log file with fallback locations
  std::wstring logFilePath;
  std::wofstream logFile;

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
    logFile.open(path.c_str(), std::ios::out | std::ios::trunc);
    if (logFile.is_open()) {
      logFilePath = path;
      std::wcout << L"[WGC Helper] Successfully opened log file: " << logFilePath << std::endl;
      std::wcout.rdbuf(logFile.rdbuf());
      std::wcerr.rdbuf(logFile.rdbuf());
      break;
    } else {
      std::wcout << L"[WGC Helper] Failed to open: " << path << std::endl;
    }
  }

  if (!logFile.is_open()) {
    // If all locations fail, continue without file logging
    // At least output to console will still work
    std::wcerr << L"[WGC Helper] Warning: Could not create log file at any location, using console output only" << std::endl;
    logFilePath = L"(console only)";
  }

  std::wcout << L"[WGC Helper] Final log file path: " << logFilePath << std::endl;

  // Start async logger
  g_async_logger.start(logFile.is_open() ? &logFile : nullptr);

  std::wcout << L"[WGC Helper] Starting Windows Graphics Capture helper process..." << std::endl;

  // Create named pipe for communication with main process
  AsyncNamedPipe communicationPipe(L"\\\\.\\pipe\\SunshineWGCHelper", true);
  g_communication_pipe = &communicationPipe;  // Store global reference for session.Closed handler

  auto onMessage = [&](const std::vector<uint8_t> &message) {
    std::wcout << L"[WGC Helper] Received message from main process, size: " << message.size() << std::endl;
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
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  D3D_FEATURE_LEVEL featureLevel;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &featureLevel, &context);
  if (FAILED(hr)) {
    std::wcerr << L"[WGC Helper] Failed to create D3D11 device" << std::endl;
    return 1;
  }

  // Wrap D3D11 device to WinRT IDirect3DDevice
  winrt::com_ptr<IDXGIDevice> dxgiDevice;
  hr = device->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.put_void());
  if (FAILED(hr)) {
    std::wcerr << L"[WGC Helper] Failed to get DXGI device" << std::endl;
    device->Release();
    context->Release();
    return 1;
  }
  winrt::com_ptr<::IDirect3DDevice> interopDevice;
  hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), reinterpret_cast<::IInspectable **>(interopDevice.put_void()));
  if (FAILED(hr)) {
    std::wcerr << L"[WGC Helper] Failed to create interop device" << std::endl;
    device->Release();
    context->Release();
    return 1;
  }
  auto winrtDevice = interopDevice.as<IDirect3DDevice>();

  // Select monitor by display name if provided, else use primary
  HMONITOR monitor = nullptr;
  MONITORINFO monitorInfo = {sizeof(MONITORINFO)};
  if (g_config_received && g_config.displayName[0] != L'\0') {
    // Enumerate monitors to find one matching displayName
    struct EnumData {
      const wchar_t *targetName;
      HMONITOR foundMonitor;
    } enumData = {g_config.displayName, nullptr};

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
    monitor = enumData.foundMonitor;
    if (!monitor) {
      std::wcerr << L"[WGC Helper] Could not find monitor with name '" << g_config.displayName << L"', falling back to primary." << std::endl;
    }
  }
  if (!monitor) {
    monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    if (!monitor) {
      std::wcerr << L"[WGC Helper] Failed to get primary monitor" << std::endl;
      device->Release();
      context->Release();
      return 1;
    }
  }

  // Get monitor info for fallback size
  if (!GetMonitorInfo(monitor, &monitorInfo)) {
    std::wcerr << L"[WGC Helper] Failed to get monitor info" << std::endl;
    device->Release();
    context->Release();
    return 1;
  }
  UINT fallbackWidth = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
  UINT fallbackHeight = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;

  // Check for config data from main process
  std::wcout << L"[WGC Helper] Checking for config data from main process..." << std::endl;
  int config_wait_count = 0;
  while (!g_config_received && config_wait_count < 50) {  // Increase to 5 seconds for reliability
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    config_wait_count++;
  }

  UINT width, height;
  if (g_config_received && g_config.width > 0 && g_config.height > 0) {
    width = g_config.width;
    height = g_config.height;
    std::wcout << L"[WGC Helper] Using config resolution: " << width << L"x" << height << std::endl;
  } else {
    width = fallbackWidth;
    height = fallbackHeight;
    std::wcout << L"[WGC Helper] No valid config resolution received, falling back to monitor: " << width << L"x" << height << std::endl;
  }

  // Create GraphicsCaptureItem for monitor using interop
  auto activationFactory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
  GraphicsCaptureItem item = nullptr;
  hr = activationFactory->CreateForMonitor(monitor, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item));
  if (FAILED(hr)) {
    std::wcerr << L"[WGC Helper] Failed to create GraphicsCaptureItem for monitor: " << hr << std::endl;
    device->Release();
    context->Release();
    return 1;
  }

  // Get actual WGC item size to ensure we capture full desktop (fixes zoomed display issue)
  auto item_size = item.Size();
  UINT wgc_width = static_cast<UINT>(item_size.Width);
  UINT wgc_height = static_cast<UINT>(item_size.Height);

  std::wcout << L"[WGC Helper] WGC item reports size: " << wgc_width << L"x" << wgc_height << std::endl;
  std::wcout << L"[WGC Helper] Monitor logical size: " << fallbackWidth << L"x" << fallbackHeight << std::endl;
  std::wcout << L"[WGC Helper] Config requested size: " << (g_config_received ? g_config.width : 0) << L"x" << (g_config_received ? g_config.height : 0) << std::endl;

  // Use physical size from WGC to avoid DPI scaling issues
  // This ensures we capture the full desktop without cropping/zooming
  if (wgc_width > 0 && wgc_height > 0) {
    // Check if there's a significant difference (indicating DPI scaling)
    bool scaling_detected = (abs(static_cast<int>(wgc_width) - static_cast<int>(fallbackWidth)) > 100) ||
                            (abs(static_cast<int>(wgc_height) - static_cast<int>(fallbackHeight)) > 100);

    if (scaling_detected) {
      std::wcout << L"[WGC Helper] DPI scaling detected - using WGC physical size to avoid zoom issues" << std::endl;
    }

    width = wgc_width;
    height = wgc_height;
    std::wcout << L"[WGC Helper] Final resolution (physical): " << width << L"x" << height << std::endl;
  }

  // Choose format based on config.dynamicRange
  DXGI_FORMAT captureFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
  if (g_config_received && g_config.dynamicRange) {
    captureFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
  }

  // Create shared texture with keyed mutex (using config/fallback size)
  D3D11_TEXTURE2D_DESC texDesc = {};
  texDesc.Width = width;
  texDesc.Height = height;
  texDesc.MipLevels = 1;
  texDesc.ArraySize = 1;
  texDesc.Format = captureFormat;
  texDesc.SampleDesc.Count = 1;
  texDesc.Usage = D3D11_USAGE_DEFAULT;
  texDesc.BindFlags = 0;
  texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
  ID3D11Texture2D *sharedTexture = nullptr;
  hr = device->CreateTexture2D(&texDesc, nullptr, &sharedTexture);
  if (FAILED(hr)) {
    std::wcerr << L"[WGC Helper] Failed to create shared texture" << std::endl;
    device->Release();
    context->Release();
    return 1;
  }

  // Get keyed mutex
  IDXGIKeyedMutex *keyedMutex = nullptr;
  hr = sharedTexture->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&keyedMutex));
  if (FAILED(hr)) {
    std::wcerr << L"[WGC Helper] Failed to get keyed mutex" << std::endl;
    sharedTexture->Release();
    device->Release();
    context->Release();
    return 1;
  }

  // Get shared handle
  IDXGIResource *dxgiResource = nullptr;
  hr = sharedTexture->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void **>(&dxgiResource));
  HANDLE sharedHandle = nullptr;
  hr = dxgiResource->GetSharedHandle(&sharedHandle);
  dxgiResource->Release();
  if (FAILED(hr) || !sharedHandle) {
    std::wcerr << L"[WGC Helper] Failed to get shared handle" << std::endl;
    keyedMutex->Release();
    sharedTexture->Release();
    device->Release();
    context->Release();
    return 1;
  }

  std::wcout << L"[WGC Helper] Created shared texture: " << width << L"x" << height
             << L", handle: " << std::hex << reinterpret_cast<uintptr_t>(sharedHandle) << std::dec << std::endl;

  // Create shared memory for frame metadata
  g_metadata_mapping = CreateFileMappingW(
    INVALID_HANDLE_VALUE,
    nullptr,
    PAGE_READWRITE,
    0,
    sizeof(FrameMetadata),
    L"Local\\SunshineWGCMetadata"
  );
  if (!g_metadata_mapping) {
    std::wcerr << L"[WGC Helper] Failed to create metadata mapping: " << GetLastError() << std::endl;
    keyedMutex->Release();
    sharedTexture->Release();
    device->Release();
    context->Release();
    return 1;
  }

  g_frame_metadata = static_cast<FrameMetadata *>(MapViewOfFile(
    g_metadata_mapping,
    FILE_MAP_ALL_ACCESS,
    0,
    0,
    sizeof(FrameMetadata)
  ));
  if (!g_frame_metadata) {
    std::wcerr << L"[WGC Helper] Failed to map metadata view: " << GetLastError() << std::endl;
    CloseHandle(g_metadata_mapping);
    keyedMutex->Release();
    sharedTexture->Release();
    device->Release();
    context->Release();
    return 1;
  }

  // Initialize metadata
  memset(g_frame_metadata, 0, sizeof(FrameMetadata));
  std::wcout << L"[WGC Helper] Created frame metadata shared memory" << std::endl;

  // Send shared handle data via named pipe to main process
  SharedHandleData handleData = {sharedHandle, width, height};
  std::vector<uint8_t> handleMessage(sizeof(SharedHandleData));
  memcpy(handleMessage.data(), &handleData, sizeof(SharedHandleData));

  // Wait for connection and send the handle data
  std::wcout << L"[WGC Helper] Waiting for main process to connect..." << std::endl;
  while (!communicationPipe.isConnected()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::wcout << L"[WGC Helper] Connected! Sending handle data..." << std::endl;
  communicationPipe.asyncSend(handleMessage);

  // Create event to signal new frame
  HANDLE frameEvent = CreateEventW(nullptr, FALSE, FALSE, L"Local\\SunshineWGCFrame");
  if (!frameEvent) {
    std::wcerr << L"[WGC Helper] Failed to create frame event" << std::endl;
    keyedMutex->Release();
    sharedTexture->Release();
    device->Release();
    context->Release();
    return 1;
  }

  // Create frame pool with larger buffer for high framerates
  const uint32_t kPoolFrames = 2;
  auto framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
    winrtDevice,
    (captureFormat == DXGI_FORMAT_R16G16B16A16_FLOAT) ? DirectXPixelFormat::R16G16B16A16Float : DirectXPixelFormat::B8G8R8A8UIntNormalized,
    kPoolFrames,
    SizeInt32 {static_cast<int32_t>(width), static_cast<int32_t>(height)}
  );

  static auto last_delivery_time = std::chrono::steady_clock::time_point {};
  static bool first_frame = true;
  static uint32_t delivery_count = 0;
  static std::chrono::milliseconds total_delivery_time {0};

  // Attach frame arrived event - process all frames without suppression
  auto token = framePool.FrameArrived([&last_delivery_time, &first_frame, &delivery_count, &total_delivery_time, &hr, keyedMutex, context, sharedTexture, frameEvent](Direct3D11CaptureFramePool const &sender, winrt::Windows::Foundation::IInspectable const &) {
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
          total_delivery_time = std::chrono::milliseconds{0};
          delivery_count = 0;
        }
      } else {
        first_frame = false;
      }
      last_delivery_time = now;

      try {
        winrt::com_ptr<IDirect3DDxgiInterfaceAccess> interfaceAccess;
        hr = surface.as<::IUnknown>()->QueryInterface(__uuidof(IDirect3DDxgiInterfaceAccess), reinterpret_cast<void **>(interfaceAccess.put()));
        if (SUCCEEDED(hr)) {
          winrt::com_ptr<ID3D11Texture2D> frameTexture;
          hr = interfaceAccess->GetInterface(__uuidof(ID3D11Texture2D), frameTexture.put_void());
          if (SUCCEEDED(hr)) {
            hr = keyedMutex->AcquireSync(0, INFINITE);
            if (SUCCEEDED(hr)) {
              context->CopyResource(sharedTexture, frameTexture.get());

              // Timestamp #2: Immediately after CopyResource completes
              uint64_t timestamp_after_copy = qpc_counter();

              // Update frame metadata before releasing mutex
              if (g_frame_metadata) {
                g_frame_metadata->qpc_timestamp = frame_qpc;
                g_frame_metadata->frame_sequence = ++g_frame_sequence;
                g_frame_metadata->suppressed_frames = 0;  // No suppression - always 0

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

              keyedMutex->ReleaseSync(1);

              // Timestamp #3: Immediately after SetEvent is called
              uint64_t timestamp_after_set_event = qpc_counter();
              SetEvent(frameEvent);

              // Log high-precision timing deltas every 300 frames (async)
              static uint32_t timing_log_counter = 0;
              if ((++timing_log_counter % 300) == 0) {
                static uint64_t qpc_freq_timing = 0;
                if (qpc_freq_timing == 0) {
                  LARGE_INTEGER freq;
                  QueryPerformanceFrequency(&freq);
                  qpc_freq_timing = freq.QuadPart;
                }

                double arrived_to_copy_us = (double)(timestamp_after_copy - timestamp_frame_arrived) * 1000000.0 / qpc_freq_timing;
                double copy_to_signal_us = (double)(timestamp_after_set_event - timestamp_after_copy) * 1000000.0 / qpc_freq_timing;
                double total_frame_us = (double)(timestamp_after_set_event - timestamp_frame_arrived) * 1000000.0 / qpc_freq_timing;

                std::wstringstream ss;
                ss << L"[WGC Helper] Frame timing - "
                   << L"Arrived->Copy: " << std::fixed << std::setprecision(1) << arrived_to_copy_us << L"μs, "
                   << L"Copy->Signal: " << copy_to_signal_us << L"μs, "
                   << L"Total: " << total_frame_us << L"μs" << std::endl;
                g_async_logger.log(ss.str());
              }
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
  });

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

  // Start capture
  auto session = framePool.CreateCaptureSession(item);
  // Disable border/highlight
  session.IsBorderRequired(false);

  if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"MinUpdateInterval")) {
    session.MinUpdateInterval(winrt::Windows::Foundation::TimeSpan {10000});
    std::wcout << L"[WGC Helper] Successfully set the MinUpdateInterval (120fps+)" << std::endl;
  }

  session.StartCapture();

  std::wcout << L"[WGC Helper] Helper process started. Capturing frames using WGC..." << std::endl;

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

    std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Reduced from 5ms for lower IPC jitter
  }

  std::wcout << L"[WGC Helper] Main process disconnected, shutting down..." << std::endl;

  // Stop async logger
  g_async_logger.stop();

  // Cleanup
  if (mmcss) {
    AvRevertMmThreadCharacteristics(mmcss);
  }
  if (g_desktop_switch_hook) {
    UnhookWinEvent(g_desktop_switch_hook);
    g_desktop_switch_hook = nullptr;
  }
  session.Close();
  framePool.FrameArrived(token);
  framePool.Close();
  CloseHandle(frameEvent);
  communicationPipe.stop();

  // Cleanup metadata mapping
  if (g_frame_metadata) {
    UnmapViewOfFile(g_frame_metadata);
    g_frame_metadata = nullptr;
  }
  if (g_metadata_mapping) {
    CloseHandle(g_metadata_mapping);
    g_metadata_mapping = nullptr;
  }

  keyedMutex->Release();
  sharedTexture->Release();
  context->Release();
  device->Release();
  // logFile will be closed automatically when it goes out of scope
  return 0;
}
