#include "process_handler.h"
#include "shared_memory.h"
#include "helpers.h"  // for is_secure_desktop_active
#include "src/logging.h"
#include "src/platform/windows/display.h"
#include "src/platform/windows/misc.h"  // for qpc_counter
#include "src/config.h"  // for config::video.capture

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
    if (display_wgc_vram_t::init(config, display_name)) {
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
      BOOST_LOG(error) << "[display_wgc_ipc_vram_t] Cannot lazy_init without proper initialization";
      return;
    }
    // Get the directory of the main executable
    wchar_t exePathBuffer[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePathBuffer, MAX_PATH);
    std::filesystem::path mainExeDir = std::filesystem::path(exePathBuffer).parent_path();
    std::filesystem::path exe_path = mainExeDir / "tools" / "sunshine_wgc_capture.exe";
    if (!_process_helper->start(exe_path.wstring(), L"")) {
      BOOST_LOG(error) << "[display_wgc_ipc_vram_t] Failed to start capture process at: " << exe_path.wstring();
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
      BOOST_LOG(error) << "[display_wgc_ipc_vram_t] Failed to receive handle data from helper process";
      cleanup();
    }
  }

  capture_e display_wgc_ipc_vram_t::acquire_next_frame(std::chrono::milliseconds timeout, texture2d_t &src, uint64_t &frame_qpc, bool cursor_visible) {
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
      BOOST_LOG(error) << "[display_wgc_ipc_ram_t] Cannot lazy_init without proper initialization";
      return;
    }
    // Get the directory of the main executable
    wchar_t exePathBuffer[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePathBuffer, MAX_PATH);
    std::filesystem::path mainExeDir = std::filesystem::path(exePathBuffer).parent_path();
    std::filesystem::path exe_path = mainExeDir / "tools" / "sunshine_wgc_capture.exe";
    if (!_process_helper->start(exe_path.wstring(), L"")) {
      BOOST_LOG(error) << "[display_wgc_ipc_ram_t] Failed to start capture process at: " << exe_path.wstring();
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
      BOOST_LOG(error) << "[display_wgc_ipc_ram_t] Failed to receive handle data from helper process";
      cleanup();
    }
  }

  capture_e display_wgc_ipc_ram_t::acquire_next_frame(std::chrono::milliseconds timeout, texture2d_t &src, uint64_t &frame_qpc, bool cursor_visible) {
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
