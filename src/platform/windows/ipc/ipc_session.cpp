
/**
 * @file ipc_session.cpp
 * @brief Implements the IPC session logic for Windows WGC capture integration.
 *
 * Handles inter-process communication, shared texture setup, and frame synchronization
 * between the main process and the WGC capture helper process.
 */

// standard includes
#include <array>
#include <chrono>
#include <filesystem>
#include <span>
#include <string_view>
#include <thread>

// local includes
#include "config.h"
#include "ipc_session.h"
#include "misc_utils.h"
#include "src/logging.h"
#include "src/platform/windows/misc.h"

// platform includes
#include <avrt.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

namespace platf::dxgi {

  void ipc_session_t::handle_shared_handle_message(std::span<const uint8_t> msg, bool &handle_received) {
    if (msg.size() == sizeof(shared_handle_data_t)) {
      shared_handle_data_t handle_data;
      memcpy(&handle_data, msg.data(), sizeof(shared_handle_data_t));
      if (setup_shared_texture(handle_data.texture_handle, handle_data.width, handle_data.height)) {
        handle_received = true;
      }
    }
  }


  void ipc_session_t::handle_secure_desktop_message(std::span<const uint8_t> msg) {
    if (msg.size() == 1 && msg[0] == SECURE_DESKTOP_MSG) {
      // secure desktop detected
      BOOST_LOG(info) << "WGC can no longer capture the screen due to Secured Desktop, swapping to DXGI";
      _should_swap_to_dxgi = true;
    }
  }

  int ipc_session_t::init(const ::video::config_t &config, std::string_view display_name, ID3D11Device *device) {
    _process_helper = std::make_unique<ProcessHandler>();
    _config = config;
    _display_name = display_name;
    _device = device;
    return 0;
  }

  void ipc_session_t::initialize_if_needed() {
    if (_initialized.load()) {
      return;
    }

    if (bool expected = false; !_initialized.compare_exchange_strong(expected, true)) {
      // Another thread is already initializing, wait for it to complete
      while (_initialized.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      return;
    }

    // Check if properly initialized via init() first
    if (!_process_helper) {
      BOOST_LOG(debug) << "Cannot lazy_init without proper initialization";
      return;
    }

    // Get the directory of the main executable (Unicode-safe)
    std::wstring exePathBuffer(MAX_PATH, L'\0');
    GetModuleFileNameW(nullptr, exePathBuffer.data(), MAX_PATH);
    exePathBuffer.resize(wcslen(exePathBuffer.data()));
    std::filesystem::path mainExeDir = std::filesystem::path(exePathBuffer).parent_path();
    std::string pipe_guid = generate_guid();
    std::string frame_pipe_guid = generate_guid();

    std::filesystem::path exe_path = mainExeDir / L"tools" / L"sunshine_wgc_capture.exe";
    std::wstring arguments = platf::from_utf8(pipe_guid + " " + frame_pipe_guid);  // Convert GUIDs to wide string for arguments

    if (!_process_helper->start(exe_path.wstring(), arguments)) {
      auto err = GetLastError();
      BOOST_LOG(error) << "Failed to start sunshine_wgc_capture executable at: " << exe_path.wstring()
                       << " with pipe GUID: " << pipe_guid << " and frame pipe GUID: " << frame_pipe_guid << " (error code: " << err << ")";
      return;
    }

    bool handle_received = false;

    auto on_message = [this, &handle_received](std::span<const uint8_t> msg) {
      if (msg.size() == sizeof(shared_handle_data_t)) {
        handle_shared_handle_message(msg, handle_received);
      } else if (msg.size() == 1) {
        handle_secure_desktop_message(msg);
      }
    };

    auto on_error = [](const std::string &err) {
      BOOST_LOG(error) << "Pipe error: " << err.c_str();
    };

    auto on_broken_pipe = [this]() {
      BOOST_LOG(warning) << "Broken pipe detected, forcing re-init";
      _force_reinit.store(true);
    };

    auto anon_connector = std::make_unique<AnonymousPipeFactory>();

    auto raw_pipe = anon_connector->create_server(pipe_guid);
    if (!raw_pipe) {
      BOOST_LOG(error) << "IPC pipe setup failed with GUID: " << pipe_guid << " - aborting WGC session";
      return;
    }
    _pipe = std::make_unique<AsyncNamedPipe>(std::move(raw_pipe));

    _frame_pipe = anon_connector->create_server(frame_pipe_guid);
    if (!_frame_pipe) {
      BOOST_LOG(error) << "IPC frame pipe queue pipe failed with GUID: " << frame_pipe_guid << " - aborting WGC session";
      return;
    }

    _pipe->wait_for_client_connection(3000);
    _frame_pipe->wait_for_client_connection(3000);

    // Send config data to helper process
    config_data_t config_data = {};
    config_data.dynamic_range = _config.dynamicRange;
    config_data.log_level = config::sunshine.min_log_level;

    // Set capture mode based on capture method string
    if (config::video.capture == "wgcv") {
      config_data.wgc_capture_mode = 1;  // Variable/Dynamic FPS this only works well on 24H2
    } else {
      config_data.wgc_capture_mode = 0;  // Constant FPS (default)
    }

    // Convert display_name (std::string) to wchar_t[32]
    if (!_display_name.empty()) {
      std::wstring wdisplay_name(_display_name.begin(), _display_name.end());
      wcsncpy_s(config_data.display_name, wdisplay_name.c_str(), 31);
      config_data.display_name[31] = L'\0';
    } else {
      config_data.display_name[0] = L'\0';
    }

    // We need to make sure helper uses the same adapter for now.
    // This won't be a problem in future versions when we add support for cross adapter capture.
    // But for now, it is required that we use the exact same one.
    if (_device) {
      try_get_adapter_luid(config_data.adapter_luid);
    } else {
      BOOST_LOG(warning) << "No D3D11 device available, helper will use default adapter";
      memset(&config_data.adapter_luid, 0, sizeof(LUID));
    }

    _pipe->send(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(&config_data), sizeof(config_data_t)));

    _pipe->start(on_message, on_error, on_broken_pipe);

    auto start_time = std::chrono::steady_clock::now();
    while (!handle_received) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      if (std::chrono::steady_clock::now() - start_time > std::chrono::seconds(3)) {
        BOOST_LOG(error) << "Timed out waiting for handle data from helper process (3s)";
        break;
      }
    }

    if (handle_received) {
      _initialized.store(true);
    } else {
      BOOST_LOG(error) << "Failed to receive handle data from helper process! Helper is likely deadlocked!";
    }
  }

  bool ipc_session_t::wait_for_frame(std::chrono::milliseconds timeout) {
    if (!_frame_pipe || !_frame_pipe->is_connected()) {
      return false;
    }

    // Because frame queue is time sensitive we use a dedicated IPC Pipe just for frame timing.
    std::array<uint8_t, sizeof(frame_ready_msg_t)> buffer;
    size_t bytes_read = 0;
    
    
    if (auto result = _frame_pipe->receive(std::span<uint8_t>(buffer), bytes_read, static_cast<int>(timeout.count())); result == PipeResult::Success && bytes_read == sizeof(frame_ready_msg_t)) {
      frame_ready_msg_t frame_msg;
      memcpy(&frame_msg, buffer.data(), sizeof(frame_msg));
      
      if (frame_msg.message_type == FRAME_READY_MSG) {
        _frame_qpc.store(frame_msg.frame_qpc, std::memory_order_release);
        return true;
      }
    }
    
    return false;
  }  bool ipc_session_t::try_get_adapter_luid(LUID &luid_out) {
    // Guarantee a clean value on failure
    memset(&luid_out, 0, sizeof(LUID));

    if (!_device) {
      BOOST_LOG(warning) << "No D3D11 device available; default adapter will be used";
      return false;
    }

    winrt::com_ptr<IDXGIDevice> dxgi_device;
    winrt::com_ptr<IDXGIAdapter> adapter;

    HRESULT hr =
      _device->QueryInterface(__uuidof(IDXGIDevice), dxgi_device.put_void());
    if (FAILED(hr)) {
      BOOST_LOG(warning) << "QueryInterface(IDXGIDevice) failed; default adapter will be used";
      return false;
    }

    hr = dxgi_device->GetAdapter(adapter.put());
    if (FAILED(hr)) {
      BOOST_LOG(warning) << "GetAdapter() failed; default adapter will be used";
      return false;
    }

    DXGI_ADAPTER_DESC desc {};
    hr = adapter->GetDesc(&desc);
    if (FAILED(hr)) {
      BOOST_LOG(warning) << "GetDesc() failed; default adapter will be used";
      return false;
    }

    luid_out = desc.AdapterLuid;
    return true;
  }

  capture_e ipc_session_t::acquire(std::chrono::milliseconds timeout, ID3D11Texture2D *&gpu_tex_out, uint64_t &frame_qpc_out) {
    if (!wait_for_frame(timeout)) {
      return capture_e::timeout;
    }

    // Additional validation: ensure required resources are available
    if (!_shared_texture || !_keyed_mutex) {
      return capture_e::error;
    }

    HRESULT hr = _keyed_mutex->AcquireSync(1, 200);

    if (hr == WAIT_ABANDONED) {
      BOOST_LOG(error) << "Helper process abandoned the keyed mutex, implying it may have crashed or was forcefully terminated.";
      _should_swap_to_dxgi = false;  // Don't swap to DXGI, just reinit
      _force_reinit = true;
      return capture_e::reinit;
    } else if (hr != S_OK || hr == WAIT_TIMEOUT) {
      return capture_e::error;
    }

    // Set output parameters
    gpu_tex_out = _shared_texture.get();
    frame_qpc_out = _frame_qpc.load(std::memory_order_acquire);

    return capture_e::ok;
  }

  void ipc_session_t::release() {
    if (_keyed_mutex) {
      // The keyed mutex has two behaviors, traditional mutex and signal-style/ping-pong.
      // If you use a key > 0, you must first RELEASE that key, even though it was never acquired.
      // Think of it like an inverse mutex, we're signaling the helper that they can work by releasing it first.
      _keyed_mutex->ReleaseSync(2);
    }
  }

  bool ipc_session_t::setup_shared_texture(HANDLE shared_handle, UINT width, UINT height) {
    if (!_device) {
      BOOST_LOG(error) << "No D3D11 device available for setup_shared_texture";
      return false;
    }

    // Open shared texture directly into safe_com_ptr
    ID3D11Texture2D *raw_texture = nullptr;
    HRESULT hr = _device->OpenSharedResource(shared_handle, __uuidof(ID3D11Texture2D), (void **) &raw_texture);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to open shared texture: " << hr;
      return false;
    }

    safe_com_ptr<ID3D11Texture2D> texture(raw_texture);

    // Get texture description to set the capture format
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    IDXGIKeyedMutex *raw_mutex = nullptr;
    hr = texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **) &raw_mutex);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "Failed to get keyed mutex: " << hr;
      return false;
    }

    safe_com_ptr<IDXGIKeyedMutex> keyed_mutex(raw_mutex);

    // Move into member variables
    _shared_texture = std::move(texture);
    _keyed_mutex = std::move(keyed_mutex);

    _width = width;
    _height = height;

    return true;
  }

}  // namespace platf::dxgi
