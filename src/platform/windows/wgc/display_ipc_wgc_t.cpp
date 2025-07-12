#include "process_handler.h"
#include "shared_memory.h"
#include "src/logging.h"
#include "src/platform/windows/display.h"
#include "src/platform/windows/misc.h"  // for qpc_counter

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace platf::dxgi {

  // Structure for shared handle data received via named pipe
  struct SharedHandleData {
    HANDLE textureHandle;
    UINT width;
    UINT height;
  };

  // Implementation of platf::dxgi::display_ipc_wgc_t methods

  display_ipc_wgc_t::display_ipc_wgc_t() = default;

  display_ipc_wgc_t::~display_ipc_wgc_t() {
    cleanup();
  }

  int display_ipc_wgc_t::init(const ::video::config_t &config, const std::string &display_name) {
    _process_helper = std::make_unique<ProcessHandler>();
    // Initialize the base class first
    if (display_vram_t::init(config, display_name)) {
      return -1;
    }
    return 0;
  }

  capture_e display_ipc_wgc_t::snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) {
    lazy_init();
    // Strengthen error detection: check all required resources
    if (!_initialized || !_shared_texture || !_frame_event || !_keyed_mutex) {
      return capture_e::error;
    }
    // Call parent class snapshot which will call our overridden acquire_next_frame
    return display_wgc_vram_t::snapshot(pull_free_image_cb, img_out, timeout, cursor_visible);
  }

  void display_ipc_wgc_t::lazy_init() {
    if (_initialized) {
      return;
    }
    // Check if properly initialized via init() first
    if (!_process_helper) {
      std::wcerr << L"[display_ipc_wgc_t] Cannot lazy_init without proper initialization" << std::endl;
      return;
    }
    // Start the helper process
    std::filesystem::path exe_path = std::filesystem::current_path() / "build" / "tools" / "sunshine-wgc-helper.exe";
    // Fallback to tools directory for different working directory scenarios
    if (!std::filesystem::exists(exe_path)) {
      exe_path = std::filesystem::path("D:/sources/sunshine/build/tools") / "sunshine-wgc-helper.exe";
    }
    if (!_process_helper->start(exe_path.wstring(), L"")) {
      std::wcerr << L"[display_ipc_wgc_t] Failed to start helper process at: " << exe_path.wstring() << std::endl;
      return;
    }
    std::wcout << L"[display_ipc_wgc_t] Started helper process: " << exe_path.wstring() << std::endl;
    // Create and start the named pipe (client mode)
    _pipe = std::make_unique<AsyncNamedPipe>(L"\\\\.\\pipe\\SunshineWGCHelper", false);
    bool handle_received = false;
    auto onMessage = [this, &handle_received](const std::vector<uint8_t> &msg) {
      std::wcout << L"[display_ipc_wgc_t] Received message, size: " << msg.size() << std::endl;
      if (msg.size() == sizeof(SharedHandleData)) {
        SharedHandleData handleData;
        memcpy(&handleData, msg.data(), sizeof(SharedHandleData));
        std::wcout << L"[display_ipc_wgc_t] Received handle data: " << std::hex
                   << reinterpret_cast<uintptr_t>(handleData.textureHandle) << std::dec
                   << L", " << handleData.width << L"x" << handleData.height << std::endl;
        if (setup_shared_texture(handleData.textureHandle, handleData.width, handleData.height)) {
          handle_received = true;
        }
      }
    };
    auto onError = [](const std::string &err) {
      std::wcout << L"[display_ipc_wgc_t] Pipe error: " << err.c_str() << std::endl;
    };
    _pipe->start(onMessage, onError);
    // Wait for connection and handle data
    std::wcout << L"[display_ipc_wgc_t] Waiting for helper process to connect..." << std::endl;
    int wait_count = 0;
    while (!handle_received && wait_count < 100) {  // 10 seconds max
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      wait_count++;
    }
    if (handle_received) {
      _initialized = true;
      std::wcout << L"[display_ipc_wgc_t] Successfully initialized IPC WGC capture" << std::endl;
    } else {
      std::wcerr << L"[display_ipc_wgc_t] Failed to receive handle data from helper process" << std::endl;
      cleanup();
    }
  }

  capture_e display_ipc_wgc_t::acquire_next_frame(std::chrono::milliseconds timeout, texture2d_t &src, uint64_t &frame_qpc, bool cursor_visible) {
    // Additional error check: ensure required resources are valid
    if (!_shared_texture || !_frame_event || !_keyed_mutex) {
      return capture_e::error;
    }
    // Wait for new frame event
    DWORD waitResult = WaitForSingleObject(_frame_event, timeout.count());
    if (waitResult != WAIT_OBJECT_0) {
      return waitResult == WAIT_TIMEOUT ? capture_e::timeout : capture_e::error;
    }
    // Acquire keyed mutex to access shared texture
    HRESULT hr = _keyed_mutex->AcquireSync(1, timeout.count());
    if (FAILED(hr)) {
      return capture_e::error;
    }
    // Set the shared texture as source by assigning the underlying pointer
    src.reset(_shared_texture.get());
    _shared_texture.get()->AddRef();  // Add reference since both will reference the same texture
    // Set frame timestamp (we don't have QPC from helper process, so use current time)
    frame_qpc = qpc_counter();
    // Note: We don't release the keyed mutex here since the parent class will use the texture
    // We'll release it in release_snapshot
    return capture_e::ok;
  }

  capture_e display_ipc_wgc_t::release_snapshot() {
    // Release the keyed mutex after the parent class is done with the texture
    if (_keyed_mutex) {
      _keyed_mutex->ReleaseSync(0);
    }
    return capture_e::ok;
  }

  void display_ipc_wgc_t::cleanup() {
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
    if (_keyed_mutex) {
      _keyed_mutex->Release();
      _keyed_mutex = nullptr;
    }
    _shared_texture.reset();
    _initialized = false;
  }

  bool display_ipc_wgc_t::setup_shared_texture(HANDLE shared_handle, UINT width, UINT height) {
    HRESULT hr;
    // Open the shared texture
    ID3D11Texture2D *texture = nullptr;
    hr = device->OpenSharedResource(shared_handle, __uuidof(ID3D11Texture2D), (void **) &texture);
    if (FAILED(hr)) {
      std::wcerr << L"[display_ipc_wgc_t] Failed to open shared texture: " << hr << std::endl;
      return false;
    }
    _shared_texture.reset(texture);
    // Get texture description to set the capture format
    D3D11_TEXTURE2D_DESC desc;
    _shared_texture->GetDesc(&desc);
    // Get the keyed mutex
    hr = _shared_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **) &_keyed_mutex);
    if (FAILED(hr)) {
      std::wcerr << L"[display_ipc_wgc_t] Failed to get keyed mutex: " << hr << std::endl;
      return false;
    }
    // Open the frame event
    _frame_event = OpenEventW(SYNCHRONIZE, FALSE, L"Local\\SunshineWGCFrame");
    if (!_frame_event) {
      std::wcerr << L"[display_ipc_wgc_t] Failed to open frame event: " << GetLastError() << std::endl;
      return false;
    }
    _width = width;
    _height = height;
    this->width = width;
    this->height = height;
    this->width_before_rotation = width;
    this->height_before_rotation = height;
    this->capture_format = desc.Format;
    std::wcout << L"[display_ipc_wgc_t] Successfully set up shared texture: "
               << width << L"x" << height << std::endl;
    return true;
  }

}  // namespace platf::dxgi
