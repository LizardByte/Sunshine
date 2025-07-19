#include "src/platform/windows/wgc/wgc_logger.h"

// Define the global logger instance for the WGC helper
boost::log::sources::severity_logger<severity_level> g_logger;
// sunshine_wgc_helper.cpp
// Windows Graphics Capture helper process for Sunshine

#define WIN32_LEAN_AND_MEAN
#include "src/platform/windows/wgc/shared_memory.h"
#include "src/platform/windows/wgc/misc_utils.h"

#include "src/platform/windows/wgc/wgc_logger.h"

// Additional includes for log formatting
#include <boost/format.hpp>
#include <chrono>
#include <iomanip>

using namespace std::literals;

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


// Global config data received from main process
platf::dxgi::ConfigData g_config = {0, 0, 0, 0, 0, L""};
bool g_config_received = false;

// Global variables for frame metadata and rate limiting
HANDLE g_metadata_mapping = nullptr;
platf::dxgi::FrameMetadata *g_frame_metadata = nullptr;
uint32_t g_frame_sequence = 0;

// Global communication pipe for sending session closed notifications
AsyncNamedPipe *g_communication_pipe = nullptr;

// Global variables for desktop switch detection
HWINEVENTHOOK g_desktop_switch_hook = nullptr;
bool g_secure_desktop_detected = false;

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
          BOOST_LOG(warning) << "Failed to set DPI awareness, display scaling issues may occur";
          return false;
        }
      }
    dpi_awareness_set = true;
    return true;
  }

  bool initializeThreadPriority() {
    // Increase thread priority to minimize scheduling delay
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
      BOOST_LOG(error) << "Failed to set thread priority: " << GetLastError();
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
          BOOST_LOG(error) << "Failed to set MMCSS characteristics: " << GetLastError();
          return false;
        }
      }    mmcss_characteristics_set = true;
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
      BOOST_LOG(error) << "Failed to create D3D11 device";
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
      BOOST_LOG(error) << "Failed to get DXGI device";
      return false;
    }
    
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.get(), reinterpret_cast<::IInspectable **>(interop_device.put_void()));
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to create interop device";
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
  bool selectMonitor(const platf::dxgi::ConfigData& config) {
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

  bool createGraphicsCaptureItem(GraphicsCaptureItem& item) {
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

  void calculateFinalResolution(const platf::dxgi::ConfigData& config, bool config_received, GraphicsCaptureItem& item) {
    // Get actual WGC item size to ensure we capture full desktop (fixes zoomed display issue)
    auto item_size = item.Size();
    UINT wgc_width = static_cast<UINT>(item_size.Width);
    UINT wgc_height = static_cast<UINT>(item_size.Height);

    BOOST_LOG(info) << "WGC item reports size: " << wgc_width << "x" << wgc_height;
    BOOST_LOG(info) << "Monitor logical size: " << fallback_width << "x" << fallback_height;
    BOOST_LOG(info) << "Config requested size: " << (config_received ? config.width : 0) << "x" << (config_received ? config.height : 0);

    if (config_received && config.width > 0 && config.height > 0) {
      final_width = config.width;
      final_height = config.height;
      BOOST_LOG(info) << "Using config resolution: " << final_width << "x" << final_height;
    } else {
      final_width = fallback_width;
      final_height = fallback_height;
      BOOST_LOG(info) << "No valid config resolution received, falling back to monitor: " << final_width << "x" << final_height;
    }

    // Use physical size from WGC to avoid DPI scaling issues
    // This ensures we capture the full desktop without cropping/zooming
    if (wgc_width > 0 && wgc_height > 0) {
      // Check if there's a significant difference (indicating DPI scaling)
      bool scaling_detected = (abs(static_cast<int>(wgc_width) - static_cast<int>(fallback_width)) > 100) ||
                              (abs(static_cast<int>(wgc_height) - static_cast<int>(fallback_height)) > 100);

      if (scaling_detected) {
        BOOST_LOG(info) << "DPI scaling detected - using WGC physical size to avoid zoom issues";
      }

      final_width = wgc_width;
      final_height = wgc_height;
      BOOST_LOG(info) << "Final resolution (physical): " << final_width << "x" << final_height;
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
  platf::dxgi::FrameMetadata *frame_metadata = nullptr;
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
      BOOST_LOG(error) << "Failed to create shared texture";
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
      BOOST_LOG(error) << "Failed to get keyed mutex";
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
      BOOST_LOG(error) << "Failed to get shared handle";
      return false;
    }
    
    BOOST_LOG(info) << "Created shared texture: " << width << "x" << height 
                   << ", handle: " << std::hex << reinterpret_cast<uintptr_t>(shared_handle) << std::dec;
    return true;
  }

  bool createFrameMetadataMapping() {
    metadata_mapping = CreateFileMappingW(
      INVALID_HANDLE_VALUE,
      nullptr,
      PAGE_READWRITE,
      0,
      sizeof(platf::dxgi::FrameMetadata),
      L"Local\\SunshineWGCMetadata"
    );
    if (!metadata_mapping) {
      BOOST_LOG(error) << "Failed to create metadata mapping: " << GetLastError();
      return false;
    }

    frame_metadata = static_cast<platf::dxgi::FrameMetadata *>(MapViewOfFile(
      metadata_mapping,
      FILE_MAP_ALL_ACCESS,
      0,
      0,
      sizeof(platf::dxgi::FrameMetadata)
    ));
    if (!frame_metadata) {
      BOOST_LOG(error) << "Failed to map metadata view: " << GetLastError();
      CloseHandle(metadata_mapping);
      metadata_mapping = nullptr;
      return false;
    }

    // Initialize metadata
    memset(frame_metadata, 0, sizeof(platf::dxgi::FrameMetadata));
    BOOST_LOG(info) << "Created frame metadata shared memory";
    return true;
  }

  bool createFrameEvent() {
    frame_event = CreateEventW(nullptr, FALSE, FALSE, L"Local\\SunshineWGCFrame");
    if (!frame_event) {
      BOOST_LOG(error) << "Failed to create frame event";
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

  platf::dxgi::SharedHandleData getSharedHandleData() const {
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
  platf::dxgi::FrameMetadata* getFrameMetadata() const { return frame_metadata; }

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

        BOOST_LOG(debug) << "delivered " << std::fixed << std::setprecision(1) << fps << " fps (target: " << (g_config_received ? g_config.framerate : 60) << ")";
      }
      lastQpc = frame_qpc;
    }

    // Log performance stats periodically
    if ((g_frame_sequence % 1500) == 0) {
      BOOST_LOG(debug) << "Frame " << g_frame_sequence << " processed without suppression";
    }
  }

  void logFrameTiming(uint64_t timestamp_frame_arrived, uint64_t timestamp_after_copy, uint64_t timestamp_after_set_event) {
    // Log high-precision timing deltas every 300 frames
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

      BOOST_LOG(debug) << "Frame timing - Arrived->Copy: " << std::fixed << std::setprecision(1) << arrived_to_copy_us << "μs, Copy->Signal: " << copy_to_signal_us << "μs, Total: " << total_frame_us << "μs";
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
    BOOST_LOG(info) << "Successfully set the MinUpdateInterval (120fps+)";
  }
  return true;
}

void startCapture() {
  if (capture_session) {
    capture_session.StartCapture();
    BOOST_LOG(info) << "Helper process started. Capturing frames using WGC...";
  }
}  void cleanup() {
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

// Desktop switch event hook procedure
void CALLBACK DesktopSwitchHookProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
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


// Helper function to get the system temp directory
std::string get_temp_log_path() {
  wchar_t tempPath[MAX_PATH] = {0};
  DWORD len = GetTempPathW(MAX_PATH, tempPath);
  if (len == 0 || len > MAX_PATH) {
    // fallback to current directory if temp path fails
    return "sunshine_wgc_helper.log";
  }
  std::wstring wlog = std::wstring(tempPath) + L"sunshine_wgc_helper.log";
  // Convert to UTF-8
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, wlog.c_str(), -1, NULL, 0, NULL, NULL);
  std::string log_file(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, wlog.c_str(), -1, &log_file[0], size_needed, NULL, NULL);
  // Remove trailing null
  if (!log_file.empty() && log_file.back() == '\0') log_file.pop_back();
  return log_file;
}

struct WgcHelperConfig {
  severity_level min_log_level = info;  // Default to info level
  std::string log_file;
  bool help_requested = false;
  bool console_output = false;
  int log_level; // New: log level from main process
  DWORD parent_pid = 0; // Parent process ID for pipe naming
  // Note: pipe_name and event_name are now generated from parent PID
};

WgcHelperConfig parse_args(int argc, char* argv[]) {
  WgcHelperConfig config;
  config.log_file = get_temp_log_path(); // Default to temp dir
  config.log_level = info; // Default log level
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      config.help_requested = true;
    } else if (arg == "--trace" || arg == "-t") {
      config.min_log_level = trace;
    } else if (arg == "--verbose" || arg == "-v") {
      config.min_log_level = debug;
    } else if (arg == "--debug" || arg == "-d") {
      config.min_log_level = debug;
    } else if (arg == "--info" || arg == "-i") {
      config.min_log_level = info;
    } else if (arg == "--warning" || arg == "-w") {
      config.min_log_level = warning;
    } else if (arg == "--error" || arg == "-e") {
      config.min_log_level = error;
    } else if (arg == "--fatal" || arg == "-f") {
      config.min_log_level = fatal;
    } else if (arg == "--log-file" && i + 1 < argc) {
      config.log_file = argv[++i];
    } else if (arg == "--console") {
      config.console_output = true;
    } else if (arg == "--parent-pid" && i + 1 < argc) {
      config.parent_pid = static_cast<DWORD>(std::stoul(argv[++i]));
    }
    // Note: --pipe-name and --event-name arguments are no longer needed
    // as they are now generated from parent process ID
  }
  return config;
}

void print_help() {
  std::cout << "Sunshine WGC Helper - Windows Graphics Capture helper process\n"
            << "\nUsage: sunshine_wgc_capture [options]\n"
            << "\nOptions:\n"
            << "  --help, -h        Show this help message\n"
            << "  --trace, -t       Set trace logging level\n"
            << "  --verbose, -v     Set debug logging level\n"
            << "  --debug, -d       Set debug logging level\n"
            << "  --info, -i        Set info logging level [default]\n"
            << "  --warning, -w     Set warning logging level\n"
            << "  --error, -e       Set error logging level\n"
            << "  --fatal, -f       Set fatal logging level\n"
            << "  --log-file FILE   Set log file path (default: sunshine_wgc_helper.log)\n"
            << "  --console         Also output logs to console\n"
            << "  --parent-pid PID  Set parent process ID for pipe naming\n"
            << "\nNote: Parent PID is automatically passed by the main process\n"
            << std::endl;
}

// Custom formatter to match main process logging format
void wgc_log_formatter(const boost::log::record_view &view, boost::log::formatting_ostream &os) {
  constexpr const char *message = "Message";
  constexpr const char *severity = "Severity";

  auto log_level = view.attribute_values()[severity].extract<severity_level>().get();

  std::string_view log_type;
  switch (log_level) {
    case trace:
      log_type = "Verbose: "sv;
      break;
    case debug:
      log_type = "Debug: "sv;
      break;
    case info:
      log_type = "Info: "sv;
      break;
    case warning:
      log_type = "Warning: "sv;
      break;
    case error:
      log_type = "Error: "sv;
      break;
    case fatal:
      log_type = "Fatal: "sv;
      break;
  };

  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    now - std::chrono::time_point_cast<std::chrono::seconds>(now)
  );

  auto t = std::chrono::system_clock::to_time_t(now);
  auto lt = *std::localtime(&t);

  os << "["sv << std::put_time(&lt, "%Y-%m-%d %H:%M:%S.") << boost::format("%03u") % ms.count() << "]: "sv
     << log_type << view.attribute_values()[message].extract<std::string>();
}

// Initialize standalone logging system
bool init_logging(severity_level min_level, const std::string& log_file, bool console_output) {
  try {
    // Create file sink directly
    typedef boost::log::sinks::synchronous_sink<boost::log::sinks::text_file_backend> file_sink_t;
    auto file_sink = boost::make_shared<file_sink_t>(
      boost::log::keywords::file_name = log_file,
      boost::log::keywords::rotation_size = 10 * 1024 * 1024,  // 10MB
      boost::log::keywords::auto_flush = true
    );
    
    // Set file sink filter and formatter
    file_sink->set_filter(boost::log::expressions::attr<severity_level>("Severity") >= min_level);
    file_sink->set_formatter(&wgc_log_formatter);
    
    // Add file sink to logging core
    boost::log::core::get()->add_sink(file_sink);
    
    // Set up console sink if requested
    if (console_output) {
      typedef boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend> console_sink_t;
      auto console_sink = boost::make_shared<console_sink_t>();
      console_sink->locked_backend()->add_stream(boost::shared_ptr<std::ostream>(&std::cout, [](std::ostream*){}));
      
      console_sink->set_filter(boost::log::expressions::attr<severity_level>("Severity") >= min_level);
      console_sink->set_formatter(&wgc_log_formatter);
      boost::log::core::get()->add_sink(console_sink);
    }
    
    // Set global filter
    boost::log::core::get()->set_filter(
      boost::log::expressions::attr<severity_level>("Severity") >= min_level
    );
    
    // Add common attributes
    boost::log::add_common_attributes();
    
    return true;
  } catch (const std::exception& e) {
    std::cerr << "Failed to initialize logging: " << e.what() << std::endl;
    return false;
  }
}

int main(int argc, char* argv[]) {
  // Parse command line arguments
  auto config = parse_args(argc, argv);

  g_config.log_level = config.log_level; // Set log level from parsed args
  if (config.help_requested) {
    print_help();
    return 0;
  }

  // Initialize logging at startup with info level (or user-specified log_file/console_output)
  severity_level initial_level = info;
  if (!init_logging(initial_level, config.log_file, config.console_output)) {
    std::cerr << "Failed to initialize logging system" << std::endl;
    return 1;
  }

  // Log startup information
  BOOST_LOG(info) << "Sunshine WGC Helper starting - Log level: " << initial_level
                  << ", Log file: " << config.log_file;

  // Heartbeat mechanism: track last heartbeat time
  auto last_heartbeat = std::chrono::steady_clock::now();

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
  SecuredPipeFactory factory;

  auto commPipe = factory.create("SunshineWGCPipe", "SunshineWGCEvent", false, false);
  AsyncNamedPipe communicationPipe(std::move(commPipe));
    g_communication_pipe = &communicationPipe;  // Store global reference for session.Closed handler

  auto onMessage = [&](const std::vector<uint8_t> &message) {
    // Heartbeat message: single byte 0x01
    if (message.size() == 1 && message[0] == 0x01) {
      last_heartbeat = std::chrono::steady_clock::now();
      return;
    }
    // Handle config data message
    if (message.size() == sizeof(platf::dxgi::ConfigData) && !g_config_received) {
      memcpy(&g_config, message.data(), sizeof(platf::dxgi::ConfigData));
      g_config_received = true;
      // If log_level in config differs from current, update log filter
      if (g_config.log_level != initial_level) {
        boost::log::core::get()->set_filter(
          boost::log::expressions::attr<severity_level>("Severity") >= static_cast<severity_level>(g_config.log_level)
        );
        BOOST_LOG(info) << "Log level updated from config: " << g_config.log_level;
      }
      BOOST_LOG(info) << "Received config data: " << g_config.width << "x" << g_config.height
                     << ", fps: " << g_config.framerate << ", hdr: " << g_config.dynamicRange
                     << ", display: '" << winrt::to_string(g_config.displayName) << "'";
    }
  };

  auto onError = [&](const std::string &err) {
  auto config = parse_args(argc, argv);
  };

  if (!communicationPipe.start(onMessage, onError)) {
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

  wgcManager.attachFrameArrivedHandler(&sharedResourceManager, d3d11Manager.getContext());

  if (!wgcManager.createCaptureSession(item)) {
    BOOST_LOG(error) << "Failed to create capture session";
    return 1;
  }

  // Set up desktop switch hook for secure desktop detection
  BOOST_LOG(info) << "Setting up desktop switch hook...";
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
    BOOST_LOG(error) << "Failed to set up desktop switch hook: " << GetLastError();
  } else {
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
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat);
    if (elapsed.count() > 5) {
      BOOST_LOG(warning) << "No heartbeat received from main process for 5 seconds, exiting...";
      _exit(1);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Reduced from 5ms for lower IPC jitter
  }

  BOOST_LOG(info) << "Main process disconnected, shutting down...";

  // Cleanup
  if (g_desktop_switch_hook) {
    UnhookWinEvent(g_desktop_switch_hook);
    g_desktop_switch_hook = nullptr;
  }
  
  wgcManager.cleanup();
  communicationPipe.stop();

  // Flush logs before exit
  boost::log::core::get()->flush();
  
  BOOST_LOG(info) << "WGC Helper process terminated";

  // Cleanup managed by destructors
  return 0;
}
