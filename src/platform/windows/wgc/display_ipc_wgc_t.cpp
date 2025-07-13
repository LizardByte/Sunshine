#include "process_handler.h"
#include "shared_memory.h"
#include "helpers.h"  // for is_secure_desktop_active
#include "src/logging.h"
#include "src/platform/windows/display.h"
#include "src/platform/windows/misc.h"  // for qpc_counter
#include "src/config.h"  // for config::video.capture
#include <avrt.h> // For MMCSS

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <tlhelp32.h>  // for process enumeration

// WinRT includes for secure desktop detection
#include <winrt/base.h>
#include <winrt/windows.foundation.h>
#include <winrt/windows.graphics.capture.h>
#include <winrt/windows.graphics.directx.direct3d11.h>
#include <windows.graphics.capture.interop.h>

namespace platf::dxgi {

  // Structure for shared handle data received via named pipe
  struct SharedHandleData {
    HANDLE textureHandle;
    UINT width;
    UINT height;
  };

  // Structure for frame metadata shared between processes
  struct FrameMetadata {
    uint64_t qpc_timestamp;         // QPC timestamp when frame was captured
    uint32_t frame_sequence;        // Sequential frame number
    uint32_t suppressed_frames;     // Number of frames suppressed since last signal
  };

  // Structure for config data sent to helper process
  struct ConfigData {
    UINT width;
    UINT height;
    int framerate;
    int dynamicRange;
    wchar_t displayName[32]; // Display device name (e.g., "\\.\\DISPLAY1")
  };

  // Accessor functions for the swap flag (kept for compatibility but may not be used)
  bool is_secure_desktop_swap_requested() {
    return false; // Always return false since we use mail events now
  }

  void reset_secure_desktop_swap_flag() {
    // No-op since we use mail events now
  }

  // Implementation of platf::dxgi::display_wgc_ipc_vram_t methods

  display_wgc_ipc_vram_t::display_wgc_ipc_vram_t() = default;

  display_wgc_ipc_vram_t::~display_wgc_ipc_vram_t() {
    cleanup();
  }

  int display_wgc_ipc_vram_t::init(const ::video::config_t &config, const std::string &display_name) {
    _process_helper = std::make_unique<ProcessHandler>();
    // Save the config data for later use
    _config = config;
    _display_name = display_name;
    // Initialize the base class first
    if (display_base_t::init(config, display_name)) {
      return -1;
    }
    
    return 0;
  }

  capture_e display_wgc_ipc_vram_t::snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) {
    // Check if secure desktop swap was triggered by helper process
    if (_should_swap_to_dxgi) {
      BOOST_LOG(info) << "[display_wgc_ipc_vram_t] Secure desktop detected, returning reinit to trigger factory re-selection";
      return capture_e::reinit;
    }
    
    lazy_init();
    // Strengthen error detection: check all required resources
    if (!_initialized || !_shared_texture || !_frame_event || !_keyed_mutex) {
      return capture_e::error;
    }
    // Call parent class snapshot which will call our overridden acquire_next_frame
    return display_wgc_vram_t::snapshot(pull_free_image_cb, img_out, timeout, cursor_visible);
  }

  void display_wgc_ipc_vram_t::lazy_init() {
    if (_initialized) {
      return;
    }
    // Check if properly initialized via init() first
    if (!_process_helper) {
      BOOST_LOG(debug) << "[display_wgc_ipc_vram_t] Cannot lazy_init without proper initialization";
      return;
    }
    // Get the directory of the main executable
    wchar_t exePathBuffer[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePathBuffer, MAX_PATH);
    std::filesystem::path mainExeDir = std::filesystem::path(exePathBuffer).parent_path();
    std::filesystem::path exe_path = mainExeDir / "tools" / "sunshine_wgc_capture.exe";
    if (!_process_helper->start(exe_path.wstring(), L"")) {
      BOOST_LOG(debug) << "[display_wgc_ipc_vram_t] Failed to start capture process at: " << exe_path.wstring() << " (this is expected when running as service)";
      return;
    }
    BOOST_LOG(info) << "[display_wgc_ipc_vram_t] Started helper process: " << exe_path.wstring();
    // Create and start the named pipe (client mode)
    _pipe = std::make_unique<AsyncNamedPipe>(L"\\\\.\\pipe\\SunshineWGCHelper", false);
    bool handle_received = false;
    auto onMessage = [this, &handle_received](const std::vector<uint8_t> &msg) {
      BOOST_LOG(info) << "[display_wgc_ipc_vram_t] Received message, size: " << msg.size();
      if (msg.size() == sizeof(SharedHandleData)) {
        SharedHandleData handleData;
        memcpy(&handleData, msg.data(), sizeof(SharedHandleData));
        BOOST_LOG(info) << "[display_wgc_ipc_vram_t] Received handle data: " << std::hex
                   << reinterpret_cast<uintptr_t>(handleData.textureHandle) << std::dec
                   << ", " << handleData.width << "x" << handleData.height;
        if (setup_shared_texture(handleData.textureHandle, handleData.width, handleData.height)) {
          handle_received = true;
        }
      } else if (msg.size() == 1 && msg[0] == 0x01) {
        // secure desktop detected
        BOOST_LOG(warning) << "[display_wgc_ipc_vram_t] WGC session closed - secure desktop detected, setting swap flag";
        _should_swap_to_dxgi = true;
      }
    };
    auto onError = [](const std::string &err) {
      BOOST_LOG(error) << "[display_wgc_ipc_vram_t] Pipe error: " << err.c_str();
    };
    _pipe->start(onMessage, onError);
    
    // Send config data to helper process
    ConfigData configData = {};
    configData.width = static_cast<UINT>(_config.width);
    configData.height = static_cast<UINT>(_config.height);
    configData.framerate = _config.framerate;
    configData.dynamicRange = _config.dynamicRange;
    // Convert display_name (std::string) to wchar_t[32]
    if (!_display_name.empty()) {
      std::wstring wdisplay_name(_display_name.begin(), _display_name.end());
      wcsncpy_s(configData.displayName, wdisplay_name.c_str(), 31);
      configData.displayName[31] = L'\0';
    } else {
      configData.displayName[0] = L'\0';
    }
    std::vector<uint8_t> configMessage(sizeof(ConfigData));
    memcpy(configMessage.data(), &configData, sizeof(ConfigData));
    // Convert displayName to std::string for logging
    std::wstring ws_display(configData.displayName);
    std::string display_str(ws_display.begin(), ws_display.end());
    BOOST_LOG(info) << "[display_wgc_ipc_vram_t] Config data prepared: " << configData.width << "x" << configData.height
               << ", fps: " << configData.framerate << ", hdr: " << configData.dynamicRange
               << ", display: '" << display_str << "'";
    
    // Wait for connection and handle data
    BOOST_LOG(info) << "[display_wgc_ipc_vram_t] Waiting for helper process to connect...";
    int wait_count = 0;
    bool config_sent = false;
    while (!handle_received && wait_count < 100) {  // 10 seconds max
      // Send config data once we're connected but haven't sent it yet
      if (!config_sent && _pipe->isConnected()) {
        _pipe->asyncSend(configMessage);
        config_sent = true;
        BOOST_LOG(info) << "[display_wgc_ipc_vram_t] Config data sent to helper process";
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      wait_count++;
    }
    if (handle_received) {
      _initialized = true;
      BOOST_LOG(info) << "[display_wgc_ipc_vram_t] Successfully initialized IPC WGC capture";
    } else {
      BOOST_LOG(debug) << "[display_wgc_ipc_vram_t] Failed to receive handle data from helper process (this is expected when running as service)";
      cleanup();
    }
  }

  capture_e display_wgc_ipc_vram_t::acquire_next_frame(std::chrono::milliseconds timeout, texture2d_t &src, uint64_t &frame_qpc, bool cursor_visible) {
    // Add real-time scheduling hint (once per thread)
    static thread_local bool mmcss_initialized = false;
    if (!mmcss_initialized) {
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
      DWORD taskIdx = 0;
      HANDLE mmcss_handle = AvSetMmThreadCharacteristicsW(L"Games", &taskIdx);
      (void)mmcss_handle; // Suppress unused variable warning
      mmcss_initialized = true;
    }
    
    // Additional error check: ensure required resources are valid
    if (!_shared_texture || !_frame_event || !_keyed_mutex || !_frame_metadata) {
      return capture_e::error;
    }

    // Enhanced diagnostic logging: track frame intervals
    static thread_local auto last_frame_time = std::chrono::steady_clock::now();
    static thread_local uint32_t diagnostic_frame_count = 0;
    static thread_local std::chrono::milliseconds total_interval_time{0};
    
    auto current_time = std::chrono::steady_clock::now();
    auto frame_interval = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_frame_time);
    total_interval_time += frame_interval;
    diagnostic_frame_count++;
    
    // Log diagnostic info every 120 frames (~2 seconds at 60fps)
    if (diagnostic_frame_count % 120 == 0) {
      auto avg_interval = total_interval_time.count() / diagnostic_frame_count;
      auto expected_interval = (_config.framerate > 0) ? (1000 / _config.framerate) : 16;
      BOOST_LOG(debug) << "[display_wgc_ipc_vram_t] Frame timing diagnostics - "
                       << "Avg interval: " << avg_interval << "ms, "
                       << "Expected: " << expected_interval << "ms, "
                       << "Last interval: " << frame_interval.count() << "ms, "
                       << "Timeout count: " << _timeout_count;
      total_interval_time = std::chrono::milliseconds{0};
      diagnostic_frame_count = 0;
    }

    // Increase timeout headroom to +20% as suggested
    std::chrono::milliseconds adjusted_timeout = timeout;
    if (_config.framerate > 0) {
      auto perfect_us = static_cast<int64_t>(1'000'000 / _config.framerate);     // microseconds
      auto tgt_us = (perfect_us * 120) / 100;                                    // +20% headroom (increased from 10%)
      auto target_timeout = std::chrono::microseconds(tgt_us);
      adjusted_timeout = std::min(timeout,                                       // caller's cap
                                 std::max(std::chrono::milliseconds(3),          // floor
                                         std::chrono::duration_cast<
                                             std::chrono::milliseconds>(target_timeout)));
                                             
      // Log timeout adjustment for debugging
      static thread_local uint32_t adjustment_log_counter = 0;
      if (++adjustment_log_counter % 300 == 0) { // Log every 5 seconds at 60fps
        BOOST_LOG(debug) << "[display_wgc_ipc_vram_t] Timeout adjustment: "
                         << "original=" << timeout.count() << "ms, "
                         << "adjusted=" << adjusted_timeout.count() << "ms, "
                         << "fps=" << _config.framerate;
      }
    }

    // Wait for new frame event
    DWORD waitResult = WaitForSingleObject(_frame_event, adjusted_timeout.count());
    if (waitResult != WAIT_OBJECT_0) {
      if (waitResult == WAIT_TIMEOUT) {
        _timeout_count++;
        // Log timeout with detailed timing info
        BOOST_LOG(debug) << "[display_wgc_ipc_vram_t] Frame timeout #" << _timeout_count 
                         << ", interval since last frame: " << frame_interval.count() << "ms"
                         << ", timeout used: " << adjusted_timeout.count() << "ms";
        
        // Adjust threshold for high framerates - expect more timeouts with very high framerates
        auto timeout_threshold = (_config.framerate >= 120) ? 50 : ((_config.framerate > 60) ? 30 : 10);
        if (_timeout_count > timeout_threshold && (_timeout_count % 20) == 0) {
          BOOST_LOG(warning) << "[display_wgc_ipc_vram_t] Frequent timeouts detected (" 
                             << _timeout_count << " timeouts), frame delivery may be irregular"
                             << " (framerate: " << _config.framerate << "fps)";
        }
      }
      last_frame_time = current_time; // Update timing even on timeout
      return waitResult == WAIT_TIMEOUT ? capture_e::timeout : capture_e::error;
    }
    
    // Reset timeout counter on successful frame
    _timeout_count = 0;
    last_frame_time = current_time;

    // Acquire keyed mutex to access shared texture
    // Use infinite wait - the event already guarantees that a frame exists
    HRESULT hr = _keyed_mutex->AcquireSync(1, 0); // 0 == infinite wait
    if (FAILED(hr)) {
      return capture_e::error;
    }
    
    // Set the shared texture as source by assigning the underlying pointer
    src.reset(_shared_texture.get());
    _shared_texture.get()->AddRef();  // Add reference since both will reference the same texture
    
    // Read frame timestamp from shared memory instead of using current time
    const FrameMetadata* metadata = static_cast<const FrameMetadata*>(_frame_metadata);
    frame_qpc = metadata->qpc_timestamp;
    
    // Enhanced suppressed frames logging with more detail
    static uint32_t last_logged_sequence = 0;
    if (metadata->frame_sequence > 0 && (metadata->frame_sequence % 100) == 0 && metadata->frame_sequence != last_logged_sequence) {
      BOOST_LOG(debug) << "[display_wgc_ipc_vram_t] Frame diagnostics - "
                       << "Sequence: " << metadata->frame_sequence 
                       << ", Suppressed in batch: " << metadata->suppressed_frames
                       << ", Target fps: " << _config.framerate
                       << ", Recent timeout count: " << _timeout_count;
      last_logged_sequence = metadata->frame_sequence;
    }
    
    // Note: We don't release the keyed mutex here since the parent class will use the texture
    // We'll release it in release_snapshot
    return capture_e::ok;
  }

  capture_e display_wgc_ipc_vram_t::release_snapshot() {
    // Release the keyed mutex after the parent class is done with the texture
    if (_keyed_mutex) {
      _keyed_mutex->ReleaseSync(0);
    }
    return capture_e::ok;
  }

  void display_wgc_ipc_vram_t::cleanup() {
    if (_pipe) {
      _pipe->stop();
      _pipe.reset();
    }
    if (_process_helper) {
      _process_helper->terminate();
      _process_helper.reset();
    }
    if (_frame_event) {
      CloseHandle(_frame_event);
      _frame_event = nullptr;
    }
    if (_frame_metadata) {
      UnmapViewOfFile(_frame_metadata);
      _frame_metadata = nullptr;
    }
    if (_metadata_mapping) {
      CloseHandle(_metadata_mapping);
      _metadata_mapping = nullptr;
    }
    if (_keyed_mutex) {
      _keyed_mutex->Release();
      _keyed_mutex = nullptr;
    }
    _shared_texture.reset();
    _initialized = false;
  }

  bool display_wgc_ipc_vram_t::setup_shared_texture(HANDLE shared_handle, UINT width, UINT height) {
    HRESULT hr;
    // Open the shared texture
    ID3D11Texture2D *texture = nullptr;
    hr = device->OpenSharedResource(shared_handle, __uuidof(ID3D11Texture2D), (void **) &texture);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "[display_wgc_ipc_vram_t] Failed to open shared texture: " << hr;
      return false;
    }
    _shared_texture.reset(texture);
    // Get texture description to set the capture format
    D3D11_TEXTURE2D_DESC desc;
    _shared_texture->GetDesc(&desc);
    // Get the keyed mutex
    hr = _shared_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **) &_keyed_mutex);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "[display_wgc_ipc_vram_t] Failed to get keyed mutex: " << hr;
      return false;
    }
    // Open the frame event
    _frame_event = OpenEventW(SYNCHRONIZE, FALSE, L"Local\\SunshineWGCFrame");
    if (!_frame_event) {
      BOOST_LOG(error) << "[display_wgc_ipc_vram_t] Failed to open frame event: " << GetLastError();
      return false;
    }

    // Open the frame metadata shared memory
    _metadata_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\SunshineWGCMetadata");
    if (!_metadata_mapping) {
      BOOST_LOG(error) << "[display_wgc_ipc_vram_t] Failed to open metadata mapping: " << GetLastError();
      return false;
    }

    _frame_metadata = MapViewOfFile(_metadata_mapping, FILE_MAP_READ, 0, 0, sizeof(FrameMetadata));
    if (!_frame_metadata) {
      BOOST_LOG(error) << "[display_wgc_ipc_vram_t] Failed to map metadata view: " << GetLastError();
      CloseHandle(_metadata_mapping);
      _metadata_mapping = nullptr;
      return false;
    }
    _width = width;
    _height = height;
    this->width = width;
    this->height = height;
    this->width_before_rotation = width;
    this->height_before_rotation = height;
    this->capture_format = desc.Format;
    BOOST_LOG(info) << "[display_wgc_ipc_vram_t] Successfully set up shared texture: "
               << width << "x" << height;
    return true;
  }

  int display_wgc_ipc_vram_t::dummy_img(platf::img_t *img_base) {
    // During encoder validation, we need to create dummy textures before WGC is initialized
    // If we're running as a service, WGC IPC won't work, so we need to fall back to DXGI
    
    // First try to use lazy_init to see if IPC is possible
    lazy_init();
    
    if (!_initialized) {
      // IPC failed (likely running as service), use DXGI fallback for dummy image creation
      BOOST_LOG(info) << "[display_wgc_ipc_vram_t] IPC not available for dummy_img, using DXGI fallback";
      
      // Create a temporary DXGI display for dummy image creation
      auto temp_dxgi = std::make_unique<display_ddup_vram_t>();
      if (temp_dxgi->init(_config, _display_name) == 0) {
        // Successfully initialized DXGI, use it for dummy image
        return temp_dxgi->dummy_img(img_base);
      } else {
        BOOST_LOG(error) << "[display_wgc_ipc_vram_t] Failed to initialize DXGI fallback for dummy_img";
        return -1;
      }
    }
    
    // IPC is available, use normal WGC path
    // Set a default capture format if it hasn't been set yet
    if (capture_format == DXGI_FORMAT_UNKNOWN) {
      capture_format = DXGI_FORMAT_B8G8R8A8_UNORM;
    }
    return display_vram_t::dummy_img(img_base);
  }

  // Implementation of temp_dxgi_vram_t  
  capture_e temp_dxgi_vram_t::snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) {
    // Check periodically if secure desktop is still active
    auto now = std::chrono::steady_clock::now();
    if (now - _last_check_time >= CHECK_INTERVAL) {
      _last_check_time = now;
      if (!platf::is_secure_desktop_active()) {
        BOOST_LOG(info) << "[temp_dxgi_vram_t] Secure desktop no longer active, returning reinit to trigger factory re-selection";
        return capture_e::reinit;
      }
    }
    
    // Call parent DXGI duplication implementation
    return display_ddup_vram_t::snapshot(pull_free_image_cb, img_out, timeout, cursor_visible);
  }

  // Implementation of temp_dxgi_ram_t
  capture_e temp_dxgi_ram_t::snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) {
    // Check periodically if secure desktop is still active
    auto now = std::chrono::steady_clock::now();
    if (now - _last_check_time >= CHECK_INTERVAL) {
      _last_check_time = now;
      if (!platf::is_secure_desktop_active()) {
        BOOST_LOG(info) << "[temp_dxgi_ram_t] Secure desktop no longer active, returning reinit to trigger factory re-selection";
        return capture_e::reinit;
      }
    }
    
    // Call parent DXGI duplication implementation
    return display_ddup_ram_t::snapshot(pull_free_image_cb, img_out, timeout, cursor_visible);
  }

  // Implementation of display_wgc_ipc_ram_t
  display_wgc_ipc_ram_t::display_wgc_ipc_ram_t() = default;

  display_wgc_ipc_ram_t::~display_wgc_ipc_ram_t() {
    cleanup();
  }

  int display_wgc_ipc_ram_t::init(const ::video::config_t &config, const std::string &display_name) {
    _process_helper = std::make_unique<ProcessHandler>();
    // Save the config data for later use
    _config = config;
    _display_name = display_name;
    // Initialize the base class first
    if (display_ram_t::init(config, display_name)) {
      return -1;
    }
    return 0;
  }

  capture_e display_wgc_ipc_ram_t::snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) {
    // Check if secure desktop swap was triggered by helper process
    if (_should_swap_to_dxgi) {
      BOOST_LOG(info) << "[display_wgc_ipc_ram_t] Secure desktop detected, returning reinit to trigger factory re-selection";
      return capture_e::reinit;
    }
    
    lazy_init();
    // Strengthen error detection: check all required resources
    if (!_initialized || !_shared_texture || !_frame_event || !_keyed_mutex) {
      return capture_e::error;
    }
    // Call parent class snapshot which will call our overridden acquire_next_frame
    return display_wgc_ram_t::snapshot(pull_free_image_cb, img_out, timeout, cursor_visible);
  }

  void display_wgc_ipc_ram_t::lazy_init() {
    if (_initialized) {
      return;
    }
    // Check if properly initialized via init() first
    if (!_process_helper) {
      BOOST_LOG(debug) << "[display_wgc_ipc_ram_t] Cannot lazy_init without proper initialization";
      return;
    }
    // Get the directory of the main executable
    wchar_t exePathBuffer[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePathBuffer, MAX_PATH);
    std::filesystem::path mainExeDir = std::filesystem::path(exePathBuffer).parent_path();
    std::filesystem::path exe_path = mainExeDir / "tools" / "sunshine_wgc_capture.exe";
    if (!_process_helper->start(exe_path.wstring(), L"")) {
      BOOST_LOG(debug) << "[display_wgc_ipc_ram_t] Failed to start capture process at: " << exe_path.wstring() << " (this is expected when running as service)";
      return;
    }
    BOOST_LOG(info) << "[display_wgc_ipc_ram_t] Started helper process: " << exe_path.wstring();
    // Create and start the named pipe (client mode)
    _pipe = std::make_unique<AsyncNamedPipe>(L"\\\\.\\pipe\\SunshineWGCHelper", false);
    bool handle_received = false;
    auto onMessage = [this, &handle_received](const std::vector<uint8_t> &msg) {
      BOOST_LOG(info) << "[display_wgc_ipc_ram_t] Received message, size: " << msg.size();
      if (msg.size() == sizeof(SharedHandleData)) {
        SharedHandleData handleData;
        memcpy(&handleData, msg.data(), sizeof(SharedHandleData));
        BOOST_LOG(info) << "[display_wgc_ipc_ram_t] Received handle data: " << std::hex
                   << reinterpret_cast<uintptr_t>(handleData.textureHandle) << std::dec
                   << ", " << handleData.width << "x" << handleData.height;
        if (setup_shared_texture(handleData.textureHandle, handleData.width, handleData.height)) {
          handle_received = true;
        }
      } else if (msg.size() == 1 && msg[0] == 0x01) {
        // secure desktop detected
        BOOST_LOG(warning) << "[display_wgc_ipc_ram_t] WGC session closed - secure desktop detected, setting swap flag";
        _should_swap_to_dxgi = true;
      }
    };
    auto onError = [](const std::string &err) {
      BOOST_LOG(error) << "[display_wgc_ipc_ram_t] Pipe error: " << err.c_str();
    };
    _pipe->start(onMessage, onError);
    
    // Send config data to helper process
    ConfigData configData = {};
    configData.width = static_cast<UINT>(_config.width);
    configData.height = static_cast<UINT>(_config.height);
    configData.framerate = _config.framerate;
    configData.dynamicRange = _config.dynamicRange;
    // Convert display_name (std::string) to wchar_t[32]
    if (!_display_name.empty()) {
      std::wstring wdisplay_name(_display_name.begin(), _display_name.end());
      wcsncpy_s(configData.displayName, wdisplay_name.c_str(), 31);
      configData.displayName[31] = L'\0';
    } else {
      configData.displayName[0] = L'\0';
    }
    std::vector<uint8_t> configMessage(sizeof(ConfigData));
    memcpy(configMessage.data(), &configData, sizeof(ConfigData));
    // Convert displayName to std::string for logging
    std::wstring ws_display(configData.displayName);
    std::string display_str(ws_display.begin(), ws_display.end());
    BOOST_LOG(info) << "[display_wgc_ipc_ram_t] Config data prepared: " << configData.width << "x" << configData.height
               << ", fps: " << configData.framerate << ", hdr: " << configData.dynamicRange
               << ", display: '" << display_str << "'";
    
    // Wait for connection and handle data
    BOOST_LOG(info) << "[display_wgc_ipc_ram_t] Waiting for helper process to connect...";
    int wait_count = 0;
    bool config_sent = false;
    while (!handle_received && wait_count < 100) {  // 10 seconds max
      // Send config data once we're connected but haven't sent it yet
      if (!config_sent && _pipe->isConnected()) {
        _pipe->asyncSend(configMessage);
        config_sent = true;
        BOOST_LOG(info) << "[display_wgc_ipc_ram_t] Config data sent to helper process";
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      wait_count++;
    }
    if (handle_received) {
      _initialized = true;
      BOOST_LOG(info) << "[display_wgc_ipc_ram_t] Successfully initialized IPC WGC capture";
    } else {
      BOOST_LOG(debug) << "[display_wgc_ipc_ram_t] Failed to receive handle data from helper process (this is expected when running as service)";
      cleanup();
    }
  }

  capture_e display_wgc_ipc_ram_t::acquire_next_frame(std::chrono::milliseconds timeout, texture2d_t &src, uint64_t &frame_qpc, bool cursor_visible) {
    // Add real-time scheduling hint (once per thread)
    static thread_local bool mmcss_initialized = false;
    if (!mmcss_initialized) {
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
      DWORD taskIdx = 0;
      HANDLE mmcss_handle = AvSetMmThreadCharacteristicsW(L"Games", &taskIdx);
      (void)mmcss_handle; // Suppress unused variable warning
      mmcss_initialized = true;
    }
    
    // Additional error check: ensure required resources are valid
    if (!_shared_texture || !_frame_event || !_keyed_mutex || !_frame_metadata) {
      return capture_e::error;
    }

    // Enhanced diagnostic logging: track frame intervals
    static thread_local auto last_frame_time = std::chrono::steady_clock::now();
    static thread_local uint32_t diagnostic_frame_count = 0;
    static thread_local std::chrono::milliseconds total_interval_time{0};
    
    auto current_time = std::chrono::steady_clock::now();
    auto frame_interval = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_frame_time);
    total_interval_time += frame_interval;
    diagnostic_frame_count++;
    
    // Log diagnostic info every 120 frames (~2 seconds at 60fps)
    if (diagnostic_frame_count % 120 == 0) {
      auto avg_interval = total_interval_time.count() / diagnostic_frame_count;
      auto expected_interval = (_config.framerate > 0) ? (1000 / _config.framerate) : 16;
      BOOST_LOG(debug) << "[display_wgc_ipc_ram_t] Frame timing diagnostics - "
                       << "Avg interval: " << avg_interval << "ms, "
                       << "Expected: " << expected_interval << "ms, "
                       << "Last interval: " << frame_interval.count() << "ms, "
                       << "Timeout count: " << _timeout_count;
      total_interval_time = std::chrono::milliseconds{0};
      diagnostic_frame_count = 0;
    }

    // Increase timeout headroom to +20% as suggested
    std::chrono::milliseconds adjusted_timeout = timeout;
    if (_config.framerate > 0) {
      auto perfect_us = static_cast<int64_t>(1'000'000 / _config.framerate);     // microseconds
      auto tgt_us = (perfect_us * 120) / 100;                                    // +20% headroom (increased from 10%)
      auto target_timeout = std::chrono::microseconds(tgt_us);
      adjusted_timeout = std::min(timeout,                                       // caller's cap
                                 std::max(std::chrono::milliseconds(3),          // floor
                                         std::chrono::duration_cast<
                                             std::chrono::milliseconds>(target_timeout)));
                                             
      // Log timeout adjustment for debugging
      static thread_local uint32_t adjustment_log_counter = 0;
      if (++adjustment_log_counter % 300 == 0) { // Log every 5 seconds at 60fps
        BOOST_LOG(debug) << "[display_wgc_ipc_ram_t] Timeout adjustment: "
                         << "original=" << timeout.count() << "ms, "
                         << "adjusted=" << adjusted_timeout.count() << "ms, "
                         << "fps=" << _config.framerate;
      }
    }

    // Wait for new frame event
    DWORD waitResult = WaitForSingleObject(_frame_event, adjusted_timeout.count());
    if (waitResult != WAIT_OBJECT_0) {
      if (waitResult == WAIT_TIMEOUT) {
        _timeout_count++;
        // Log timeout with detailed timing info
        BOOST_LOG(debug) << "[display_wgc_ipc_ram_t] Frame timeout #" << _timeout_count 
                         << ", interval since last frame: " << frame_interval.count() << "ms"
                         << ", timeout used: " << adjusted_timeout.count() << "ms";
        
        // Adjust threshold for high framerates - expect more timeouts with very high framerates
        auto timeout_threshold = (_config.framerate >= 120) ? 50 : ((_config.framerate > 60) ? 30 : 10);
        if (_timeout_count > timeout_threshold && (_timeout_count % 20) == 0) {
          BOOST_LOG(warning) << "[display_wgc_ipc_ram_t] Frequent timeouts detected (" 
                             << _timeout_count << " timeouts), frame delivery may be irregular"
                             << " (framerate: " << _config.framerate << "fps)";
        }
      }
      last_frame_time = current_time; // Update timing even on timeout
      return waitResult == WAIT_TIMEOUT ? capture_e::timeout : capture_e::error;
    }
    
    // Reset timeout counter on successful frame
    _timeout_count = 0;
    last_frame_time = current_time;

    // Acquire keyed mutex to access shared texture
    // Use infinite wait - the event already guarantees that a frame exists
    HRESULT hr = _keyed_mutex->AcquireSync(1, 0); // 0 == infinite wait
    if (FAILED(hr)) {
      return capture_e::error;
    }
    
    // Set the shared texture as source by assigning the underlying pointer
    src.reset(_shared_texture.get());
    _shared_texture.get()->AddRef();  // Add reference since both will reference the same texture
    
    // Read frame timestamp from shared memory
    const FrameMetadata* metadata = static_cast<const FrameMetadata*>(_frame_metadata);
    frame_qpc = metadata->qpc_timestamp;
    
    // Enhanced suppressed frames logging with more detail
    static uint32_t last_logged_sequence = 0;
    if (metadata->frame_sequence > 0 && (metadata->frame_sequence % 100) == 0 && metadata->frame_sequence != last_logged_sequence) {
      BOOST_LOG(debug) << "[display_wgc_ipc_ram_t] Frame diagnostics - "
                       << "Sequence: " << metadata->frame_sequence 
                       << ", Suppressed in batch: " << metadata->suppressed_frames
                       << ", Target fps: " << _config.framerate
                       << ", Recent timeout count: " << _timeout_count;
      last_logged_sequence = metadata->frame_sequence;
    }
    
    // Note: We don't release the keyed mutex here since the parent class will use the texture
    // We'll release it in release_snapshot
    return capture_e::ok;
  }

  capture_e display_wgc_ipc_ram_t::release_snapshot() {
    // Release the keyed mutex after the parent class is done with the texture
    if (_keyed_mutex) {
      _keyed_mutex->ReleaseSync(0);
    }
    return capture_e::ok;
  }

  void display_wgc_ipc_ram_t::cleanup() {
    if (_pipe) {
      _pipe->stop();
      _pipe.reset();
    }
    if (_process_helper) {
      _process_helper->terminate();
      _process_helper.reset();
    }
    if (_frame_event) {
      CloseHandle(_frame_event);
      _frame_event = nullptr;
    }
    if (_frame_metadata) {
      UnmapViewOfFile(_frame_metadata);
      _frame_metadata = nullptr;
    }
    if (_metadata_mapping) {
      CloseHandle(_metadata_mapping);
      _metadata_mapping = nullptr;
    }
    if (_keyed_mutex) {
      _keyed_mutex->Release();
      _keyed_mutex = nullptr;
    }
    _shared_texture.reset();
    _initialized = false;
  }

  bool display_wgc_ipc_ram_t::setup_shared_texture(HANDLE shared_handle, UINT width, UINT height) {
    HRESULT hr;
    // Open the shared texture
    ID3D11Texture2D *texture = nullptr;
    hr = device->OpenSharedResource(shared_handle, __uuidof(ID3D11Texture2D), (void **) &texture);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "[display_wgc_ipc_ram_t] Failed to open shared texture: " << hr;
      return false;
    }
    _shared_texture.reset(texture);
    // Get texture description to set the capture format
    D3D11_TEXTURE2D_DESC desc;
    _shared_texture->GetDesc(&desc);
    // Get the keyed mutex
    hr = _shared_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **) &_keyed_mutex);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "[display_wgc_ipc_ram_t] Failed to get keyed mutex: " << hr;
      return false;
    }
    // Open the frame event
    _frame_event = OpenEventW(SYNCHRONIZE, FALSE, L"Local\\SunshineWGCFrame");
    if (!_frame_event) {
      BOOST_LOG(error) << "[display_wgc_ipc_ram_t] Failed to open frame event: " << GetLastError();
      return false;
    }

    // Open the frame metadata shared memory
    _metadata_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\SunshineWGCMetadata");
    if (!_metadata_mapping) {
      BOOST_LOG(error) << "[display_wgc_ipc_ram_t] Failed to open metadata mapping: " << GetLastError();
      return false;
    }

    _frame_metadata = MapViewOfFile(_metadata_mapping, FILE_MAP_READ, 0, 0, sizeof(FrameMetadata));
    if (!_frame_metadata) {
      BOOST_LOG(error) << "[display_wgc_ipc_ram_t] Failed to map metadata view: " << GetLastError();
      CloseHandle(_metadata_mapping);
      _metadata_mapping = nullptr;
      return false;
    }
    _width = width;
    _height = height;
    this->width = width;
    this->height = height;
    this->width_before_rotation = width;
    this->height_before_rotation = height;
    this->capture_format = desc.Format;
    BOOST_LOG(info) << "[display_wgc_ipc_ram_t] Successfully set up shared texture: "
               << width << "x" << height;
    return true;
  }

  int display_wgc_ipc_ram_t::dummy_img(platf::img_t *img_base) {
    // During encoder validation, we need to create dummy textures before WGC is initialized
    // If we're running as a service, WGC IPC won't work, so we need to fall back to DXGI
    
    // First try to use lazy_init to see if IPC is possible
    lazy_init();
    
    if (!_initialized) {
      // IPC failed (likely running as service), use DXGI fallback for dummy image creation
      BOOST_LOG(info) << "[display_wgc_ipc_ram_t] IPC not available for dummy_img, using DXGI fallback";
      
      // Create a temporary DXGI display for dummy image creation
      auto temp_dxgi = std::make_unique<display_ddup_ram_t>();
      if (temp_dxgi->init(_config, _display_name) == 0) {
        // Successfully initialized DXGI, use it for dummy image
        return temp_dxgi->dummy_img(img_base);
      } else {
        BOOST_LOG(error) << "[display_wgc_ipc_ram_t] Failed to initialize DXGI fallback for dummy_img";
        return -1;
      }
    }
    
    // IPC is available, use normal WGC path
    // Set a default capture format if it hasn't been set yet
    if (capture_format == DXGI_FORMAT_UNKNOWN) {
      capture_format = DXGI_FORMAT_B8G8R8A8_UNORM;
    }
    return display_ram_t::dummy_img(img_base);
  }

  // Factory methods for WGC display
  std::shared_ptr<display_t> display_wgc_ipc_vram_t::create(const ::video::config_t &config, const std::string &display_name) {
    // Check if secure desktop is currently active
    bool secure_desktop_active = platf::is_secure_desktop_active();
    
    if (secure_desktop_active) {
      // Secure desktop is active, use DXGI fallback
      BOOST_LOG(info) << "Secure desktop detected, using DXGI fallback for WGC capture (VRAM)";
      auto disp = std::make_shared<temp_dxgi_vram_t>();
      if (!disp->init(config, display_name)) {
        return disp;
      }
    } else {
      // Secure desktop not active, use WGC IPC
      BOOST_LOG(info) << "Using WGC IPC implementation (VRAM)";
      auto disp = std::make_shared<display_wgc_ipc_vram_t>();
      if (!disp->init(config, display_name)) {
        return disp;
      }
    }
    
    return nullptr;
  }

  std::shared_ptr<display_t> display_wgc_ipc_ram_t::create(const ::video::config_t &config, const std::string &display_name) {
    // Check if secure desktop is currently active
    bool secure_desktop_active = platf::is_secure_desktop_active();
    
    if (secure_desktop_active) {
      // Secure desktop is active, use DXGI fallback
      BOOST_LOG(info) << "Secure desktop detected, using DXGI fallback for WGC capture (RAM)";
      auto disp = std::make_shared<temp_dxgi_ram_t>();
      if (!disp->init(config, display_name)) {
        return disp;
      }
    } else {
      // Secure desktop not active, use WGC IPC
      BOOST_LOG(info) << "Using WGC IPC implementation (RAM)";
      auto disp = std::make_shared<display_wgc_ipc_ram_t>();
      if (!disp->init(config, display_name)) {
        return disp;
      }
    }
    
    return nullptr;
  }

}  // namespace platf::dxgi
