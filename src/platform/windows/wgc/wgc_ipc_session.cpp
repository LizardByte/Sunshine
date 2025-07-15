/**
 * @file src/platform/windows/wgc/wgc_ipc_session.cpp
 * @brief Implementation of shared IPC session for WGC capture.
 */

#include "wgc_ipc_session.h"
#include "helpers.h"
#include "src/logging.h"
#include "src/platform/windows/misc.h"

#include <avrt.h>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <thread>

namespace platf::dxgi {

  // Structures from display_ipc_wgc_refactored.cpp (we need these here)
  struct SharedHandleData {
    HANDLE textureHandle;
    UINT width;
    UINT height;
  };

  struct FrameMetadata {
    uint64_t qpc_timestamp;
    uint32_t frame_sequence;
    uint32_t suppressed_frames;
  };

  struct ConfigData {
    UINT width;
    UINT height;
    int framerate;
    int dynamicRange;
    wchar_t displayName[32];
  };

  wgc_ipc_session_t::~wgc_ipc_session_t() {
    cleanup();
  }

  int wgc_ipc_session_t::init(const ::video::config_t& config, const std::string& display_name, ID3D11Device* device) {
    _process_helper = std::make_unique<ProcessHandler>();
    _config = config;
    _display_name = display_name;
    _device = device;
    return 0;
  }

  void wgc_ipc_session_t::lazy_init() {
    if (_initialized) {
      return;
    }

    // Check if properly initialized via init() first
    if (!_process_helper) {
      BOOST_LOG(debug) << "[wgc_ipc_session_t] Cannot lazy_init without proper initialization";
      return;
    }

    // Get the directory of the main executable
    wchar_t exePathBuffer[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePathBuffer, MAX_PATH);
    std::filesystem::path mainExeDir = std::filesystem::path(exePathBuffer).parent_path();
    std::filesystem::path exe_path = mainExeDir / "tools" / "sunshine_wgc_capture.exe";

    if (!_process_helper->start(exe_path.wstring(), L"")) {
      BOOST_LOG(debug) << "[wgc_ipc_session_t] Failed to start capture process at: " << exe_path.wstring() << " (this is expected when running as service)";
      return;
    }
    BOOST_LOG(info) << "[wgc_ipc_session_t] Started helper process: " << exe_path.wstring();

    // Create and start the named pipe (client mode)
    _pipe = std::make_unique<AsyncNamedPipe>(L"\\\\.\\pipe\\SunshineWGCHelper", false);
    bool handle_received = false;

    auto onMessage = [this, &handle_received](const std::vector<uint8_t>& msg) {
      BOOST_LOG(info) << "[wgc_ipc_session_t] Received message, size: " << msg.size();
      if (msg.size() == sizeof(SharedHandleData)) {
        SharedHandleData handleData;
        memcpy(&handleData, msg.data(), sizeof(SharedHandleData));
        BOOST_LOG(info) << "[wgc_ipc_session_t] Received handle data: " << std::hex
                        << reinterpret_cast<uintptr_t>(handleData.textureHandle) << std::dec
                        << ", " << handleData.width << "x" << handleData.height;
        if (setup_shared_texture(handleData.textureHandle, handleData.width, handleData.height)) {
          handle_received = true;
        }
      } else if (msg.size() == 1 && msg[0] == 0x01) {
        // secure desktop detected
        BOOST_LOG(warning) << "[wgc_ipc_session_t] WGC session closed - secure desktop detected, setting swap flag";
        _should_swap_to_dxgi = true;
      }
    };

    auto onError = [](const std::string& err) {
      BOOST_LOG(error) << "[wgc_ipc_session_t] Pipe error: " << err.c_str();
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
    BOOST_LOG(info) << "[wgc_ipc_session_t] Config data prepared: " << configData.width << "x" << configData.height
                    << ", fps: " << configData.framerate << ", hdr: " << configData.dynamicRange
                    << ", display: '" << display_str << "'";

    // Wait for connection and handle data
    BOOST_LOG(info) << "[wgc_ipc_session_t] Waiting for helper process to connect...";
    int wait_count = 0;
    bool config_sent = false;
    while (!handle_received && wait_count < 100) {  // 10 seconds max
      // Send config data once we're connected but haven't sent it yet
      if (!config_sent && _pipe->isConnected()) {
        _pipe->asyncSend(configMessage);
        config_sent = true;
        BOOST_LOG(info) << "[wgc_ipc_session_t] Config data sent to helper process";
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      wait_count++;
    }

    if (handle_received) {
      _initialized = true;
      BOOST_LOG(info) << "[wgc_ipc_session_t] Successfully initialized IPC WGC capture";
    } else {
      BOOST_LOG(debug) << "[wgc_ipc_session_t] Failed to receive handle data from helper process (this is expected when running as service)";
      cleanup();
    }
  }

  void wgc_ipc_session_t::cleanup() {
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
    if (_shared_texture) {
      _shared_texture->Release();
      _shared_texture = nullptr;
    }
    _initialized = false;
  }

  bool wgc_ipc_session_t::acquire(std::chrono::milliseconds timeout,
                                  ID3D11Texture2D*& gpu_tex_out,
                                  const FrameMetadata*& meta_out) {
    // Add real-time scheduling hint (once per thread)
    static thread_local bool mmcss_initialized = false;
    if (!mmcss_initialized) {
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
      DWORD taskIdx = 0;
      HANDLE mmcss_handle = AvSetMmThreadCharacteristicsW(L"Games", &taskIdx);
      (void) mmcss_handle;  // Suppress unused variable warning
      mmcss_initialized = true;
    }

    // Additional error check: ensure required resources are valid
    if (!_shared_texture || !_frame_event || !_keyed_mutex || !_frame_metadata) {
      return false;
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
    if (diagnostic_frame_count % 120 == 0 && diagnostic_frame_count > 0) {
      auto avg_interval = total_interval_time.count() / diagnostic_frame_count;
      auto expected_interval = (_config.framerate > 0) ? (1000 / _config.framerate) : 16;
      BOOST_LOG(info) << "[wgc_ipc_session_t] Frame timing diagnostics - "
                      << "Avg interval: " << avg_interval << "ms, "
                      << "Expected: " << expected_interval << "ms, "
                      << "Last interval: " << frame_interval.count() << "ms, "
                      << "Timeout count: " << _timeout_count;
      total_interval_time = std::chrono::milliseconds{0};
      diagnostic_frame_count = 0;
    }

    // Timeout adjustment with proper handling of 0ms timeout
    std::chrono::milliseconds adjusted_timeout = timeout;
    if (timeout.count() == 0) {
      // Keep the original behavior for 0ms timeout (immediate return)
      adjusted_timeout = timeout;
    } else if (_config.framerate > 0) {
      // Apply frame rate based timeout adjustment only for non-zero timeouts
      auto perfect_us = static_cast<int64_t>(1'000'000 / _config.framerate);  // microseconds
      auto tgt_us = (perfect_us * 120) / 100;  // +20% headroom (increased from 10%)
      auto target_timeout = std::chrono::microseconds(tgt_us);
      adjusted_timeout = std::min(timeout,  // caller's cap
                                  std::max(std::chrono::milliseconds(3),  // floor
                                           std::chrono::duration_cast<std::chrono::milliseconds>(target_timeout)));

      // Log timeout adjustment for debugging
      static thread_local uint32_t adjustment_log_counter = 0;
      if (++adjustment_log_counter % 300 == 0) {  // Log every 5 seconds at 60fps
        BOOST_LOG(info) << "[wgc_ipc_session_t] Timeout adjustment: "
                        << "original=" << timeout.count() << "ms, "
                        << "adjusted=" << adjusted_timeout.count() << "ms, "
                        << "fps=" << _config.framerate;
      }
    }

    // Wait for new frame event
    // Timestamp #1: Before calling WaitForSingleObject
    uint64_t timestamp_before_wait = qpc_counter();
    DWORD waitResult = WaitForSingleObject(_frame_event, adjusted_timeout.count());
    // Timestamp #2: Immediately after WaitForSingleObject returns
    uint64_t timestamp_after_wait = qpc_counter();

    if (waitResult != WAIT_OBJECT_0) {
      if (waitResult == WAIT_TIMEOUT) {
        _timeout_count++;
        // Only log timeouts if we were actually waiting for a frame (non-blocking checks are expected to timeout)
        if (adjusted_timeout.count() > 0) {
          // Log timeout with detailed timing info
          BOOST_LOG(info) << "[wgc_ipc_session_t] Frame timeout #" << _timeout_count
                          << ", interval since last frame: " << frame_interval.count() << "ms"
                          << ", timeout used: " << adjusted_timeout.count() << "ms";

          // Adjust threshold for high framerates - expect more timeouts with very high framerates
          auto timeout_threshold = (_config.framerate >= 120) ? 50 : ((_config.framerate > 60) ? 30 : 10);
          if (_timeout_count > timeout_threshold && (_timeout_count % 20) == 0) {
            BOOST_LOG(warning) << "[wgc_ipc_session_t] Frequent timeouts detected ("
                               << _timeout_count << " timeouts), frame delivery may be irregular"
                               << " (framerate: " << _config.framerate << "fps)";
          }
        }
      }
      last_frame_time = current_time;  // Update timing even on timeout
      return false;
    }

    // Reset timeout counter on successful frame
    _timeout_count = 0;
    last_frame_time = current_time;

    // Acquire keyed mutex to access shared texture
    // Use infinite wait - the event already guarantees that a frame exists
    HRESULT hr = _keyed_mutex->AcquireSync(1, 0);  // 0 == infinite wait
    // Timestamp #3: After _keyed_mutex->AcquireSync succeeds
    uint64_t timestamp_after_mutex = qpc_counter();

    if (FAILED(hr)) {
      return false;
    }

    // Set output parameters
    gpu_tex_out = _shared_texture;
    meta_out = static_cast<const FrameMetadata*>(_frame_metadata);

    // Enhanced suppressed frames logging with more detail
    static uint32_t last_logged_sequence = 0;
    if (meta_out->frame_sequence > 0 && (meta_out->frame_sequence % 100) == 0 && meta_out->frame_sequence != last_logged_sequence) {
      BOOST_LOG(info) << "[wgc_ipc_session_t] Frame diagnostics - "
                      << "Sequence: " << meta_out->frame_sequence
                      << ", Suppressed in batch: " << meta_out->suppressed_frames
                      << ", Target fps: " << _config.framerate
                      << ", Recent timeout count: " << _timeout_count;
      last_logged_sequence = meta_out->frame_sequence;
    }

    // Log high-precision timing deltas every 150 frames for main process timing
    static uint32_t main_timing_log_counter = 0;
    if ((++main_timing_log_counter % 150) == 0) {
      static uint64_t qpc_freq_main = 0;
      if (qpc_freq_main == 0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        qpc_freq_main = freq.QuadPart;
      }

      double wait_time_us = (qpc_freq_main > 0) ? (double) (timestamp_after_wait - timestamp_before_wait) * 1000000.0 / qpc_freq_main : 0.0;
      double mutex_time_us = (qpc_freq_main > 0) ? (double) (timestamp_after_mutex - timestamp_after_wait) * 1000000.0 / qpc_freq_main : 0.0;
      double total_acquire_us = (qpc_freq_main > 0) ? (double) (timestamp_after_mutex - timestamp_before_wait) * 1000000.0 / qpc_freq_main : 0.0;

      BOOST_LOG(info) << "[wgc_ipc_session_t] Acquire timing - "
                      << "Wait: " << std::fixed << std::setprecision(1) << wait_time_us << "μs, "
                      << "Mutex: " << mutex_time_us << "μs, "
                      << "Total: " << total_acquire_us << "μs";
    }

    return true;
  }

  void wgc_ipc_session_t::release() {
    // Release the keyed mutex
    if (_keyed_mutex) {
      _keyed_mutex->ReleaseSync(0);
    }

    // Send heartbeat to helper after each frame is released
    if (_pipe && _pipe->isConnected()) {
      uint8_t heartbeat_msg = 0x01;
      _pipe->asyncSend(std::vector<uint8_t>{heartbeat_msg});
    }
  }

  bool wgc_ipc_session_t::setup_shared_texture(HANDLE shared_handle, UINT width, UINT height) {
    if (!_device) {
      BOOST_LOG(error) << "[wgc_ipc_session_t] No D3D11 device available for setup_shared_texture";
      return false;
    }

    HRESULT hr;
    // Open the shared texture
    ID3D11Texture2D* texture = nullptr;
    hr = _device->OpenSharedResource(shared_handle, __uuidof(ID3D11Texture2D), (void**)&texture);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "[wgc_ipc_session_t] Failed to open shared texture: " << hr;
      return false;
    }
    _shared_texture = texture;

    // Get texture description to set the capture format
    D3D11_TEXTURE2D_DESC desc;
    _shared_texture->GetDesc(&desc);

    // Get the keyed mutex
    hr = _shared_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&_keyed_mutex);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "[wgc_ipc_session_t] Failed to get keyed mutex: " << hr;
      return false;
    }

    // Open the frame event
    _frame_event = OpenEventW(SYNCHRONIZE, FALSE, L"Local\\SunshineWGCFrame");
    if (!_frame_event) {
      BOOST_LOG(error) << "[wgc_ipc_session_t] Failed to open frame event: " << GetLastError();
      return false;
    }

    // Open the frame metadata shared memory
    _metadata_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\SunshineWGCMetadata");
    if (!_metadata_mapping) {
      BOOST_LOG(error) << "[wgc_ipc_session_t] Failed to open metadata mapping: " << GetLastError();
      return false;
    }

    _frame_metadata = MapViewOfFile(_metadata_mapping, FILE_MAP_READ, 0, 0, sizeof(FrameMetadata));
    if (!_frame_metadata) {
      BOOST_LOG(error) << "[wgc_ipc_session_t] Failed to map metadata view: " << GetLastError();
      CloseHandle(_metadata_mapping);
      _metadata_mapping = nullptr;
      return false;
    }

    _width = width;
    _height = height;

    BOOST_LOG(info) << "[wgc_ipc_session_t] Successfully set up shared texture: "
                    << width << "x" << height;
    return true;
  }

} // namespace platf::dxgi
