/**
 * @file src/vdd_control.cpp
 * @brief Definitions for Parsec Virtual Display Driver control.
 */

#ifdef _WIN32

// header include
#include "vdd_control.h"

// standard includes
#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

// lib includes
#include <d3d11.h>
#include <dxgi1_6.h>

// local includes
#include "config.h"
#include "file_handler.h"
#include "logging.h"
#include "platform/common.h"

// parsec-vdd core header
#include <parsec-vdd/parsec-vdd.h>

using namespace std::literals;
using namespace parsec_vdd;

namespace vdd {

  namespace {

    // Driver constants
    constexpr auto KEEPALIVE_INTERVAL = 50ms;

    // State
    HANDLE g_vdd_handle = INVALID_HANDLE_VALUE;
    std::mutex g_handle_mutex;
    std::unique_ptr<std::thread> g_keepalive_thread;
    std::atomic<bool> g_keepalive_running{false};
    std::atomic<bool> g_initialized{false};

    // Track VDD driver indices and device names for displays created by this session
    std::vector<int> g_vdd_indices;
    std::vector<std::string> g_vdd_device_names;
    struct VddDisplayConfig { int width; int height; int hz; };
    std::vector<VddDisplayConfig> g_vdd_configs;

    /**
     * @brief Persist the current display count to the config file.
     */
    void persist_display_count() {
      auto vars = config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str()));
      vars["vdd_display_count"] = std::to_string(g_vdd_indices.size());
      std::stringstream ss;
      for (auto &[k, v] : vars) {
        ss << k << " = " << v << "\n";
      }
      file_handler::write_file(config::sunshine.config_file.c_str(), ss.str());
    }

    /**
     * @brief Enumerate DXGI outputs (physical monitors).
     * @param vdd_only If true, only count VDD displays (PSCCDD0).
     * @return Number of displays matching the criteria.
     */
    int enumerate_dxgi_outputs(bool vdd_only) {
      int count = 0;

      IDXGIFactory1 *factory = nullptr;
      auto hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory);
      if (FAILED(hr)) {
        return 0;
      }

      IDXGIAdapter1 *adapter = nullptr;
      for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        IDXGIOutput *output = nullptr;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
          DXGI_OUTPUT_DESC desc;
          if (SUCCEEDED(output->GetDesc(&desc))) {
            // VDD displays typically appear as "PSCCDD0" in the device name
            std::wstring wname(desc.DeviceName);
            bool is_vdd = (wname.find(L"PSCCDD0") != std::wstring::npos);

            if (vdd_only == is_vdd) {
              ++count;
            }
          }
          output->Release();
        }
        adapter->Release();
      }

      factory->Release();
      return count;
    }

  }  // anonymous namespace

  /**
   * @brief Check if a display device is a VDD display.
   * Uses multiple detection methods for robustness.
   */
  static bool is_vdd_display(const DISPLAY_DEVICEA &dd) {
    // Method 1: Check DeviceID for PSCCDD0 (hardware ID)
    if (dd.DeviceID[0] != '\0' && strstr(dd.DeviceID, "PSCCDD0")) {
      return true;
    }
    // Method 2: Check DeviceString for Parsec adapter name
    if (strstr(dd.DeviceString, "Parsec")) {
      return true;
    }
    return false;
  }

  // Forward declaration for internal helpers used by locked functions
  std::vector<DisplayInfo> get_displays_internal();

  bool init() {
    std::lock_guard<std::mutex> lock(g_handle_mutex);

    if (g_initialized) {
      return true;
    }

    // Check if driver is installed
    auto status = QueryDeviceStatus(&VDD_CLASS_GUID, VDD_HARDWARE_ID);
    if (status != DEVICE_OK) {
      BOOST_LOG(warning) << "VDD: Driver not ready (status="sv << (int)status << ')' << std::endl;
    }

    // Try to open handle regardless - driver might be usable
    HANDLE handle = OpenDeviceHandle(&VDD_ADAPTER_GUID);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
      BOOST_LOG(warning) << "VDD: Failed to open device handle - driver may not be installed"sv << std::endl;
      return false;
    }

    g_vdd_handle = handle;
    g_initialized = true;

    BOOST_LOG(info) << "VDD: Initialized successfully (handle="sv << (void *)handle << ')' << std::endl;
    return true;
  }

  void destroy() {
    stop_keepalive();

    {
      std::lock_guard<std::mutex> lock(g_handle_mutex);

      if (!g_initialized) {
        return;
      }

      // Remove only displays tracked by this session
      for (auto it = g_vdd_indices.rbegin(); it != g_vdd_indices.rend(); ++it) {
        VddRemoveDisplay(g_vdd_handle, *it);
      }
      g_vdd_indices.clear();
      g_vdd_device_names.clear();
      g_vdd_configs.clear();
      BOOST_LOG(info) << "VDD: Cleaned up displays on shutdown"sv << std::endl;

      if (g_vdd_handle != INVALID_HANDLE_VALUE && g_vdd_handle != nullptr) {
        CloseDeviceHandle(g_vdd_handle);
        g_vdd_handle = INVALID_HANDLE_VALUE;
      }

      g_initialized = false;
    }

    BOOST_LOG(info) << "VDD: Destroyed"sv << std::endl;
  }

  bool is_initialized() {
    return g_initialized;
  }

  DriverStatus get_driver_status() {
    auto status = QueryDeviceStatus(&VDD_CLASS_GUID, VDD_HARDWARE_ID);
    switch (status) {
      case DEVICE_OK:
        return DriverStatus::OK;
      case DEVICE_NOT_INSTALLED:
        return DriverStatus::NOT_INSTALLED;
      case DEVICE_DISABLED:
      case DEVICE_DISABLED_SERVICE:
        return DriverStatus::DISABLED;
      case DEVICE_RESTART_REQUIRED:
        return DriverStatus::RESTART_REQUIRED;
      case DEVICE_INACCESSIBLE:
        return DriverStatus::INACCESSIBLE;
      default:
        return DriverStatus::UNKNOWN;
    }
  }

  std::string get_driver_version() {
    std::lock_guard<std::mutex> lock(g_handle_mutex);
    if (!g_initialized || g_vdd_handle == INVALID_HANDLE_VALUE) {
      return "(not connected)"s;
    }

    int minor = VddVersion(g_vdd_handle);
    if (minor < 0) {
      return "(unknown)"s;
    }

    // The version IOCTL returns (major << 16) | minor
    int major = (minor >> 16) & 0xFFFF;
    int minor_ver = minor & 0xFFFF;
    return std::to_string(major) + "."s + std::to_string(minor_ver);
  }

  bool need_virtual_display() {
    // Count physical (non-VDD) DXGI outputs
    int physical_count = enumerate_dxgi_outputs(false);
    BOOST_LOG(info) << "VDD: Physical displays detected: "sv << physical_count << std::endl;
    return physical_count == 0;
  }

  int add_display(int width, int height, int hz) {
    int idx;

    {
      std::lock_guard<std::mutex> lock(g_handle_mutex);
      if (!g_initialized || g_vdd_handle == INVALID_HANDLE_VALUE) {
        BOOST_LOG(error) << "VDD: Cannot add display - not initialized"sv << std::endl;
        return -1;
      }

      idx = VddAddDisplay(g_vdd_handle);
      if (idx < 0) {
        BOOST_LOG(error) << "VDD: VddAddDisplay failed (returned "sv << idx
                         << ", handle="sv << (void *)g_vdd_handle
                         << ", GetLastError="sv << GetLastError() << ')' << std::endl;
        return -1;
      }

      // Track the VDD driver index
      g_vdd_indices.push_back(idx);
    }

    persist_display_count();

    BOOST_LOG(info) << "VDD: Added display #"sv << idx << " ("sv << width << 'x' << height << '@' << hz << "Hz)"sv << std::endl;

    // Change display mode via Win32 API
    // After VDD adds the display, we need to wait briefly for Windows to detect it
    std::this_thread::sleep_for(500ms);

    // Find the newly added VDD display by diffing against the
    // already-tracked device names. Taking the "last" VDD display
    // from EnumDisplayDevicesA is unreliable — enumeration order
    // does not match creation order.
    std::set<std::string> known_names;
    {
      std::lock_guard<std::mutex> lock(g_handle_mutex);
      for (const auto &name : g_vdd_device_names) {
        known_names.insert(name);
      }
    }

    std::string new_device_name;
    DISPLAY_DEVICEA dd;
    ZeroMemory(&dd, sizeof(dd));
    dd.cb = sizeof(dd);

    for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &dd, EDD_GET_DEVICE_INTERFACE_NAME); ++i) {
      if (is_vdd_display(dd) && known_names.find(dd.DeviceName) == known_names.end()) {
        new_device_name = dd.DeviceName;
        break;
      }
    }

    if (new_device_name.empty()) {
      BOOST_LOG(warning) << "VDD: Could not find newly created display"sv << std::endl;
      // Undo: remove the display we just created
      {
        std::lock_guard<std::mutex> lock(g_handle_mutex);
        VddRemoveDisplay(g_vdd_handle, idx);
      }
      return -1;
    }

    // Track the device name and config
    {
      std::lock_guard<std::mutex> lock(g_handle_mutex);
      g_vdd_device_names.push_back(new_device_name);
      g_vdd_configs.push_back({width, height, hz});
    }

    // Build DEVMODEA from scratch rather than relying on
    // EnumDisplaySettingsA(ENUM_CURRENT_SETTINGS), which may fail
    // for VDD displays that haven't had a mode persisted yet.
    {
      DEVMODEA dm;
      ZeroMemory(&dm, sizeof(dm));
      dm.dmSize = sizeof(dm);
      dm.dmPelsWidth  = width;
      dm.dmPelsHeight = height;
      dm.dmDisplayFrequency = hz;
      dm.dmBitsPerPel = 32;
      dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL;

      LONG ret = ChangeDisplaySettingsExA(
        new_device_name.c_str(), &dm, nullptr,
        CDS_UPDATEREGISTRY, nullptr);

      if (ret == DISP_CHANGE_SUCCESSFUL) {
        BOOST_LOG(info) << "VDD: Set display "sv << new_device_name
                        << " to "sv << width << 'x' << height << '@' << hz << "Hz"sv << std::endl;
      } else {
        BOOST_LOG(warning) << "VDD: Requested mode "sv << width << 'x' << height << '@' << hz
                          << "Hz not accepted (error="sv << ret << "), enumerating supported modes"sv << std::endl;

        // Fallback: enumerate supported modes and pick the closest match.
        // Reset dm before enumeration — the failed ChangeDisplaySettingsExA
        // may have modified the structure.
        int best_idx = -1;
        int best_score = 99999999;
        int actual_w = 0, actual_h = 0, actual_hz = 0;

        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);

        for (int i = 0; EnumDisplaySettingsA(new_device_name.c_str(), i, &dm); ++i) {
          int w_diff  = std::abs((int) dm.dmPelsWidth  - width);
          int h_diff  = std::abs((int) dm.dmPelsHeight - height);
          int hz_diff = std::abs((int) dm.dmDisplayFrequency - hz);
          int score   = w_diff * 10000 + h_diff * 100 + hz_diff;

          if (score < best_score) {
            best_score = score;
            best_idx   = i;
            actual_w   = dm.dmPelsWidth;
            actual_h   = dm.dmPelsHeight;
            actual_hz  = dm.dmDisplayFrequency;
          }
        }

        if (best_idx >= 0) {
          ZeroMemory(&dm, sizeof(dm));
          dm.dmSize = sizeof(dm);
          if (EnumDisplaySettingsA(new_device_name.c_str(), best_idx, &dm)) {
            ret = ChangeDisplaySettingsExA(
              new_device_name.c_str(), &dm, nullptr,
              CDS_UPDATEREGISTRY, nullptr);

            if (ret == DISP_CHANGE_SUCCESSFUL) {
              BOOST_LOG(info) << "VDD: Set display "sv << new_device_name
                              << " to closest supported mode "sv << actual_w << 'x' << actual_h << '@' << actual_hz << "Hz"sv << std::endl;

              // Update stored config with the mode that was actually applied
              std::lock_guard<std::mutex> lock(g_handle_mutex);
              if (!g_vdd_configs.empty()) {
                g_vdd_configs.back() = {actual_w, actual_h, actual_hz};
              }
            } else {
              BOOST_LOG(warning) << "VDD: Failed to set closest mode (error="sv << ret << ')' << std::endl;
            }
          }
        } else {
          BOOST_LOG(warning) << "VDD: No supported display modes found for "sv << new_device_name << std::endl;
        }
      }
    }

    // Re-apply stored resolutions to all previously created displays.
    // VddAddDisplay + VddUpdate can reset existing VDD displays to the
    // driver's default EDID mode, so we must restore them.
    {
      std::lock_guard<std::mutex> lock(g_handle_mutex);
      for (size_t n = 0; n + 1 < g_vdd_device_names.size(); ++n) {
        if (n >= g_vdd_configs.size()) continue;

        auto &cfg = g_vdd_configs[n];
        DEVMODEA check_dm;
        ZeroMemory(&check_dm, sizeof(check_dm));
        check_dm.dmSize = sizeof(check_dm);

        if (EnumDisplaySettingsA(g_vdd_device_names[n].c_str(), ENUM_CURRENT_SETTINGS, &check_dm)) {
          if ((int) check_dm.dmPelsWidth != cfg.width ||
              (int) check_dm.dmPelsHeight != cfg.height ||
              (int) check_dm.dmDisplayFrequency != cfg.hz) {
            BOOST_LOG(info) << "VDD: Restoring display "sv << g_vdd_device_names[n]
                            << " to "sv << cfg.width << 'x' << cfg.height << '@' << cfg.hz << "Hz"sv << std::endl;

            check_dm.dmPelsWidth  = cfg.width;
            check_dm.dmPelsHeight = cfg.height;
            check_dm.dmDisplayFrequency = cfg.hz;
            check_dm.dmBitsPerPel = 32;
            check_dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL;

            ChangeDisplaySettingsExA(
              g_vdd_device_names[n].c_str(), &check_dm, nullptr,
              CDS_UPDATEREGISTRY, nullptr);
          }
        }
      }
    }

    // Ensure keepalive is running for the new display
    start_keepalive();

    return idx;
  }

  bool remove_display(int index) {
    std::lock_guard<std::mutex> lock(g_handle_mutex);
    if (!g_initialized || g_vdd_handle == INVALID_HANDLE_VALUE) {
      return false;
    }

    // Map from our enumeration index to the VDD driver index
    if (index < 0 || index >= static_cast<int>(g_vdd_indices.size())) {
      BOOST_LOG(warning) << "VDD: Invalid display index "sv << index << std::endl;
      return false;
    }

    int vdd_idx = g_vdd_indices[index];
    VddRemoveDisplay(g_vdd_handle, vdd_idx);
    g_vdd_indices.erase(g_vdd_indices.begin() + index);
    g_vdd_device_names.erase(g_vdd_device_names.begin() + index);
    g_vdd_configs.erase(g_vdd_configs.begin() + index);
    BOOST_LOG(info) << "VDD: Removed display #"sv << index << " (vdd_idx="sv << vdd_idx << ')' << std::endl;

    persist_display_count();
    return true;
  }

  bool remove_last_display() {
    int vdd_idx;

    {
      std::lock_guard<std::mutex> lock(g_handle_mutex);
      if (!g_initialized || g_vdd_handle == INVALID_HANDLE_VALUE || g_vdd_indices.empty()) {
        return false;
      }

      vdd_idx = g_vdd_indices.back();
      g_vdd_indices.pop_back();
      g_vdd_device_names.pop_back();
      g_vdd_configs.pop_back();
      VddRemoveDisplay(g_vdd_handle, vdd_idx);
    }

    persist_display_count();
    BOOST_LOG(info) << "VDD: Removed last display (vdd_idx="sv << vdd_idx << ')' << std::endl;
    return true;
  }

  void remove_all_displays() {
    {
      std::lock_guard<std::mutex> lock(g_handle_mutex);
      if (!g_initialized || g_vdd_handle == INVALID_HANDLE_VALUE) {
        return;
      }

      for (auto it = g_vdd_indices.rbegin(); it != g_vdd_indices.rend(); ++it) {
        VddRemoveDisplay(g_vdd_handle, *it);
      }

      if (!g_vdd_indices.empty()) {
        BOOST_LOG(info) << "VDD: Removed all "sv << g_vdd_indices.size() << " display(s)"sv << std::endl;
        g_vdd_indices.clear();
        g_vdd_device_names.clear();
        g_vdd_configs.clear();
      }
    }

    persist_display_count();
  }

  /**
   * @brief Internal display enumeration (caller must hold g_handle_mutex).
   */
  std::vector<DisplayInfo> get_displays_internal() {
    std::vector<DisplayInfo> result;

    // Only return displays created by this session
    for (size_t n = 0; n < g_vdd_device_names.size(); ++n) {
      const auto &name = g_vdd_device_names[n];

      DisplayInfo info{};
      info.index = static_cast<int>(n);
      info.device_name = name;

      // Try to read actual display settings first; fall back to stored config
      DEVMODEA dm;
      ZeroMemory(&dm, sizeof(dm));
      dm.dmSize = sizeof(dm);
      if (EnumDisplaySettingsA(name.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
        info.width  = dm.dmPelsWidth;
        info.height = dm.dmPelsHeight;
        info.hz     = dm.dmDisplayFrequency;
      } else if (n < g_vdd_configs.size()) {
        info.width  = g_vdd_configs[n].width;
        info.height = g_vdd_configs[n].height;
        info.hz     = g_vdd_configs[n].hz;
      }

      // Parse identifier from device name
      auto pos = info.device_name.rfind("DISPLAY");
      if (pos != std::string::npos) {
        try {
          info.identifier = std::stoi(info.device_name.substr(pos + 7));
        } catch (...) {
          info.identifier = static_cast<int>(n) + 1;
        }
      } else {
        info.identifier = static_cast<int>(n) + 1;
      }

      result.push_back(info);
    }

    BOOST_LOG(info) << "VDD: Found "sv << result.size() << " virtual display(s)"sv << std::endl;
    return result;
  }

  std::vector<DisplayInfo> get_displays() {
    std::lock_guard<std::mutex> lock(g_handle_mutex);
    if (!g_initialized) {
      return {};
    }
    return get_displays_internal();
  }

  int get_display_count() {
    return static_cast<int>(get_displays().size());
  }

  void start_keepalive() {
    if (g_keepalive_running) {
      return;
    }

    g_keepalive_running = true;
    g_keepalive_thread = std::make_unique<std::thread>([]() {
      platf::set_thread_name("vdd_keepalive");
      BOOST_LOG(info) << "VDD: Keepalive thread started"sv << std::endl;

      while (g_keepalive_running) {
        {
          std::lock_guard<std::mutex> lock(g_handle_mutex);
          if (g_initialized && g_vdd_handle != INVALID_HANDLE_VALUE) {
            VddUpdate(g_vdd_handle);
          }
        }

        std::this_thread::sleep_for(KEEPALIVE_INTERVAL);
      }

      BOOST_LOG(info) << "VDD: Keepalive thread stopped"sv << std::endl;
    });

    BOOST_LOG(info) << "VDD: Keepalive started"sv << std::endl;
  }

  void stop_keepalive() {
    if (g_keepalive_running) {
      g_keepalive_running = false;

      if (g_keepalive_thread && g_keepalive_thread->joinable()) {
        g_keepalive_thread->join();
      }

      g_keepalive_thread.reset();
    }
  }

}  // namespace vdd

#endif  // _WIN32
