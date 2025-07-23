/**
 * @file src/platform/windows/wgc/wgc_ipc_session.cpp
 * @brief Implementation of shared IPC session for WGC capture.
 */

#include "wgc_ipc_session.h"

#include "config.h"
#include "misc_utils.h"
#include "src/logging.h"
#include "src/platform/windows/misc.h"

#include <avrt.h>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <string_view>
#include <thread>

namespace platf::dxgi {

  void wgc_ipc_session_t::handle_shared_handle_message(const std::vector<uint8_t> &msg, bool &handle_received) {
    if (msg.size() == sizeof(SharedHandleData)) {
      SharedHandleData handleData;
      memcpy(&handleData, msg.data(), sizeof(SharedHandleData));
      BOOST_LOG(info) << "[wgc_ipc_session_t] Received handle data: " << std::hex
                      << reinterpret_cast<uintptr_t>(handleData.textureHandle) << std::dec
                      << ", " << handleData.width << "x" << handleData.height;
      if (setup_shared_texture(handleData.textureHandle, handleData.width, handleData.height)) {
        handle_received = true;
      }
    }
  }

  void wgc_ipc_session_t::handle_frame_notification(const std::vector<uint8_t> &msg) {
    if (msg.size() == 1 && msg[0] == FRAME_READY_MSG) {
      _frame_ready.store(true, std::memory_order_release);
    }
  }

  void wgc_ipc_session_t::handle_secure_desktop_message(const std::vector<uint8_t> &msg) {
    if (msg.size() == 1 && msg[0] == SECURE_DESKTOP_MSG) {
      // secure desktop detected
      BOOST_LOG(info) << "[wgc_ipc_session_t] WGC can no longer capture the screen due to Secured Desktop, swapping to DXGI";
      _should_swap_to_dxgi = true;
    }
  }

  wgc_ipc_session_t::~wgc_ipc_session_t() {
    cleanup();
  }

  int wgc_ipc_session_t::init(const ::video::config_t &config, std::string_view display_name, ID3D11Device *device) {
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
    std::string exePathBuffer(MAX_PATH, '\0');
    GetModuleFileNameA(nullptr, exePathBuffer.data(), MAX_PATH);
    exePathBuffer.resize(strlen(exePathBuffer.data()));
    std::filesystem::path mainExeDir = std::filesystem::path(exePathBuffer).parent_path();
    std::filesystem::path exe_path = mainExeDir / "tools" / "sunshine_wgc_capture.exe";

    if (!_process_helper->start(exe_path.wstring(), L"")) {
      if (bool is_system = ::platf::wgc::is_running_as_system(); is_system) {
        BOOST_LOG(debug) << "[wgc_ipc_session_t] Failed to start capture process at: " << exe_path.wstring() << " (this is expected when running as service)";
      } else {
        BOOST_LOG(error) << "[wgc_ipc_session_t] Failed to start capture process at: " << exe_path.wstring();
      }
      return;
    }
    BOOST_LOG(info) << "[wgc_ipc_session_t] Started helper process: " << exe_path.wstring();

    bool handle_received = false;

    auto onMessage = [this, &handle_received](const std::vector<uint8_t> &msg) {
      handle_shared_handle_message(msg, handle_received);
      handle_frame_notification(msg);
      handle_secure_desktop_message(msg);
    };

    auto onError = [](const std::string &err) {
      BOOST_LOG(error) << "[wgc_ipc_session_t] Pipe error: " << err.c_str();
    };

    auto anonConnector = std::make_unique<AnonymousPipeFactory>();

    auto rawPipe = anonConnector->create_server("SunshineWGCPipe");
    if (!rawPipe) {
      BOOST_LOG(error) << "[wgc_ipc_session_t] IPC pipe setup failed - aborting WGC session";
      cleanup();
      return;
    }
    _pipe = std::make_unique<AsyncNamedPipe>(std::move(rawPipe));

    _pipe->wait_for_client_connection(3000);

    // Send config data to helper process
    ConfigData configData = {};
    configData.dynamicRange = _config.dynamicRange;
    configData.log_level = config::sunshine.min_log_level;

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
    BOOST_LOG(info) << "[wgc_ipc_session_t] Config data prepared: "
                    << "hdr: " << configData.dynamicRange
                    << ", display: '" << display_str << "'";

    BOOST_LOG(info) << "sending config to helper";

    _pipe->asyncSend(configMessage);

    _pipe->start(onMessage, onError);

    BOOST_LOG(info) << "[wgc_ipc_session_t] Waiting for handle data from helper process...";

    auto start_time = std::chrono::steady_clock::now();
    while (!handle_received) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      if (std::chrono::steady_clock::now() - start_time > std::chrono::seconds(3)) {
        BOOST_LOG(error) << "[wgc_ipc_session_t] Timed out waiting for handle data from helper process (3s)";
        break;
      }
    }

    if (handle_received) {
      _initialized = true;
      BOOST_LOG(info) << "[wgc_ipc_session_t] Successfully initialized IPC WGC capture";
    } else {
      BOOST_LOG(error) << "[wgc_ipc_session_t] Failed to receive handle data from helper process! Helper is likely deadlocked!";
      cleanup();
    }
  }

  void wgc_ipc_session_t::cleanup() {
    // Stop pipe communication first if active
    if (_pipe) {
      _pipe->stop();
    }

    // Terminate process if running
    if (_process_helper) {
      _process_helper->terminate();
    }

    // Reset all resources - RAII handles automatic cleanup
    _pipe.reset();
    _process_helper.reset();
    _keyed_mutex.reset();
    _shared_texture.reset();

    // Reset frame state
    _frame_ready.store(false, std::memory_order_release);

    _initialized = false;
  }

  void wgc_ipc_session_t::initialize_mmcss_for_thread() const {
    static thread_local bool mmcss_initialized = false;
    if (!mmcss_initialized) {
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
      DWORD taskIdx = 0;
      HANDLE const mmcss_handle = AvSetMmThreadCharacteristicsW(L"Games", &taskIdx);
      (void) mmcss_handle;
      mmcss_initialized = true;
    }
  }

  bool wgc_ipc_session_t::wait_for_frame(std::chrono::milliseconds timeout) {
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
      // Check if frame is ready
      if (_frame_ready.load(std::memory_order_acquire)) {
        _frame_ready.store(false, std::memory_order_release);
        return true;
      }

      // Check timeout
      if (auto elapsed = std::chrono::steady_clock::now() - start_time; elapsed >= timeout) {
        return false;
      }

      // Small sleep to avoid busy waiting
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  void wgc_ipc_session_t::log_timing_diagnostics(uint64_t timestamp_before_wait, uint64_t timestamp_after_wait, uint64_t timestamp_after_mutex) const {
    static thread_local uint32_t main_timing_log_counter = 0;
    if ((++main_timing_log_counter % 150) == 0) {
      static uint64_t qpc_freq_main = 0;
      if (qpc_freq_main == 0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        qpc_freq_main = freq.QuadPart;
      }

      if (qpc_freq_main > 0) {
        double wait_time_us = static_cast<double>(timestamp_after_wait - timestamp_before_wait) * 1000000.0 / static_cast<double>(qpc_freq_main);
        double mutex_time_us = static_cast<double>(timestamp_after_mutex - timestamp_after_wait) * 1000000.0 / static_cast<double>(qpc_freq_main);
        double total_acquire_us = static_cast<double>(timestamp_after_mutex - timestamp_before_wait) * 1000000.0 / static_cast<double>(qpc_freq_main);

        BOOST_LOG(info) << "[wgc_ipc_session_t] Acquire timing - "
                        << "Wait: " << std::fixed << std::setprecision(1) << wait_time_us << "μs, "
                        << "Mutex: " << mutex_time_us << "μs, "
                        << "Total: " << total_acquire_us << "μs";
      }
    }
  }

  bool wgc_ipc_session_t::acquire(std::chrono::milliseconds timeout, ID3D11Texture2D *&gpu_tex_out) {
    // Add real-time scheduling hint (once per thread)
    initialize_mmcss_for_thread();

    // Additional error check: ensure required resources are valid
    if (!_shared_texture || !_keyed_mutex) {
      return false;
    }

    // Wait for new frame via async pipe messages
    // Timestamp #1: Before waiting for frame
    uint64_t timestamp_before_wait = qpc_counter();

    if (bool frame_available = wait_for_frame(timeout); !frame_available) {
      return false;
    }

    // Timestamp #2: Immediately after frame becomes available
    uint64_t timestamp_after_wait = qpc_counter();

    // Reset timeout counter on successful frame
    _timeout_count = 0;

    // Acquire keyed mutex to access shared texture
    // Use infinite wait - the message already guarantees that a frame exists
    HRESULT hr = _keyed_mutex->AcquireSync(1, 0);  // 0 == infinite wait
    // Timestamp #3: After _keyed_mutex->AcquireSync succeeds
    uint64_t timestamp_after_mutex = qpc_counter();

    // No frame is ready -- timed out
    if (FAILED(hr)) {
      return false;
    }

    // Set output parameters
    gpu_tex_out = _shared_texture.get();

    // Log high-precision timing deltas every 150 frames for main process timing
    log_timing_diagnostics(timestamp_before_wait, timestamp_after_wait, timestamp_after_mutex);

    return true;
  }

  void wgc_ipc_session_t::release() {
    // Release the keyed mutex
    if (_keyed_mutex) {
      _keyed_mutex->ReleaseSync(0);
    }

    // Send heartbeat to helper after each frame is released
    if (_pipe && _pipe->isConnected()) {
      _pipe->asyncSend(std::vector<uint8_t> {HEARTBEAT_MSG});
    }
  }

  bool wgc_ipc_session_t::setup_shared_texture(HANDLE shared_handle, UINT width, UINT height) {
    if (!_device) {
      BOOST_LOG(error) << "[wgc_ipc_session_t] No D3D11 device available for setup_shared_texture";
      return false;
    }

    HRESULT hr;

    safe_com_ptr<ID3D11Texture2D> texture;
    ID3D11Texture2D *raw_texture = nullptr;
    hr = _device->OpenSharedResource(shared_handle, __uuidof(ID3D11Texture2D), (void **) &raw_texture);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "[wgc_ipc_session_t] Failed to open shared texture: " << hr;
      return false;
    }

    texture.reset(raw_texture);

    // Get texture description to set the capture format
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    safe_com_ptr<IDXGIKeyedMutex> keyed_mutex;
    IDXGIKeyedMutex *raw_mutex = nullptr;
    hr = texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **) &raw_mutex);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "[wgc_ipc_session_t] Failed to get keyed mutex: " << hr;
      return false;
    }
    keyed_mutex.reset(raw_mutex);

    _shared_texture = std::move(texture);
    _keyed_mutex = std::move(keyed_mutex);

    _width = width;
    _height = height;

    BOOST_LOG(info) << "[wgc_ipc_session_t] Successfully set up shared texture: "
                    << width << "x" << height;
    return true;
  }

}  // namespace platf::dxgi
