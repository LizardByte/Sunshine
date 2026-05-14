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
#include <format>
#include <mutex>
#include <set>
#include <sstream>
#include <string_view>
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

  // Forward declaration — used by enumerate_displays in the anonymous namespace
  static bool is_vdd_display(const DISPLAY_DEVICEA &dd);

  namespace {

    // Driver constants
    constexpr auto KEEPALIVE_INTERVAL = 50ms;

    // State — intentionally mutable globals, scoped to this translation unit
    HANDLE g_vdd_handle = INVALID_HANDLE_VALUE;        // NOSONAR -- reassigned at runtime
    std::mutex g_handle_mutex;                          // NOSONAR -- mutex must be mutable
    std::unique_ptr<std::jthread> g_keepalive_thread;   // NOSONAR -- reassigned at runtime
    std::atomic<bool> g_keepalive_running{false};       // NOSONAR -- runtime state flag
    std::atomic<bool> g_initialized{false};             // NOSONAR -- runtime state flag

    // Track VDD driver indices and device names for displays created by this session
    std::vector<int> g_vdd_indices;                     // NOSONAR -- runtime collection
    std::vector<std::string> g_vdd_device_names;        // NOSONAR -- runtime collection
    struct VddDisplayConfig { int width; int height; int hz; };
    std::vector<VddDisplayConfig> g_vdd_configs;        // NOSONAR -- runtime collection

    /**
     * @brief Persist the current display count to the config file.
     */
    void persist_display_count() {
      auto content = file_handler::read_file(config::sunshine.config_file.c_str());
      auto new_value = std::to_string(g_vdd_indices.size());

      // Update the vdd_display_count line in-place to preserve
      // comments, blank lines, and option ordering.
      std::string line;
      std::stringstream result;
      bool found = false;

      for (size_t i = 0; i <= content.size(); ++i) {
        bool at_end = (i == content.size());
        if (!at_end && content[i] != '\n') {
          line += content[i];
          continue;
        }
        if (!line.empty() && line.rfind("vdd_display_count", 0) == 0) {
          result << "vdd_display_count = "sv << new_value << '\n';
          found = true;
        } else {
          result << line;
          if (!at_end) result << '\n';
        }
        line.clear();
      }

      if (!found) {
        result << "vdd_display_count = "sv << new_value << '\n';
      }

      file_handler::write_file(config::sunshine.config_file.c_str(), result.str());
    }

    /**
     * @brief Enumerate displays via EnumDisplayDevices.
     * @param vdd_only If true, only count VDD displays.
     * @return Number of displays matching the criteria.
     */
    int enumerate_displays(bool vdd_only) {
      int count = 0;

      DISPLAY_DEVICEA dd;
      ZeroMemory(&dd, sizeof(dd));
      dd.cb = sizeof(dd);

      for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &dd, 0); ++i) {
        bool is_vdd = is_vdd_display(dd);
        if (vdd_only == is_vdd) {
          ++count;
        }
      }

      return count;
    }

  }  // anonymous namespace

  /**
   * @brief Check if a display device is a VDD display.
   * Uses multiple detection methods for robustness.
   */
  static bool is_vdd_display(const DISPLAY_DEVICEA &dd) {
    // Method 1: Check DeviceID for PSCCDD0 (hardware ID)
    if (dd.DeviceID[0] != '\0' && std::string_view(dd.DeviceID).contains("PSCCDD0")) {
      return true;
    }
    // Method 2: Check DeviceString for Parsec adapter name
    if (std::string_view(dd.DeviceString).contains("Parsec")) {
      return true;
    }
    return false;
  }

  // Forward declaration for internal helpers used by locked functions
  std::vector<DisplayInfo> get_displays_internal();

  bool init() {
    std::scoped_lock lock(g_handle_mutex);

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
      std::scoped_lock lock(g_handle_mutex);

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
    using enum DriverStatus;
    auto status = QueryDeviceStatus(&VDD_CLASS_GUID, VDD_HARDWARE_ID);
    switch (status) {
      case DEVICE_OK:
        return OK;
      case DEVICE_NOT_INSTALLED:
        return NOT_INSTALLED;
      case DEVICE_DISABLED:
      case DEVICE_DISABLED_SERVICE:
        return DISABLED;
      case DEVICE_RESTART_REQUIRED:
        return RESTART_REQUIRED;
      case DEVICE_INACCESSIBLE:
        return INACCESSIBLE;
      default:
        return UNKNOWN;
    }
  }

  std::string get_driver_version() {
    std::scoped_lock lock(g_handle_mutex);
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
    return std::format("{}.{}", major, minor_ver);
  }

  bool need_virtual_display() {
    // Count physical (non-VDD) DXGI outputs
    int physical_count = enumerate_displays(false);
    BOOST_LOG(info) << "VDD: Physical displays detected: "sv << physical_count << std::endl;
    return physical_count == 0;
  }

  namespace {

    /**
     * @brief Find the newly added VDD display by diffing against known names.
     * Enumeration order does not match creation order, so we must diff.
     */
    std::string find_new_display_name(const std::set<std::string, std::less<>> &known_names) {
      DISPLAY_DEVICEA dd;
      ZeroMemory(&dd, sizeof(dd));
      dd.cb = sizeof(dd);

      for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &dd, EDD_GET_DEVICE_INTERFACE_NAME); ++i) {
        if (is_vdd_display(dd) && !known_names.contains(dd.DeviceName)) {
          return dd.DeviceName;
        }
      }
      return {};
    }

    /**
     * @brief Apply a display mode to the named device with fallback enumeration.
     * @return true if a mode was successfully applied.
     */
    bool apply_display_mode(const std::string &device_name, int width, int height, int hz) {
      DEVMODEA dm;
      ZeroMemory(&dm, sizeof(dm));
      dm.dmSize = sizeof(dm);
      dm.dmPelsWidth  = width;
      dm.dmPelsHeight = height;
      dm.dmDisplayFrequency = hz;
      dm.dmBitsPerPel = 32;
      dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL;

      LONG ret = ChangeDisplaySettingsExA(
        device_name.c_str(), &dm, nullptr,
        CDS_UPDATEREGISTRY, nullptr);

      if (ret == DISP_CHANGE_SUCCESSFUL) {
        BOOST_LOG(info) << "VDD: Set display "sv << device_name
                        << " to "sv << width << 'x' << height << '@' << hz << "Hz"sv << std::endl;
        return true;
      }

      BOOST_LOG(warning) << "VDD: Requested mode "sv << width << 'x' << height << '@' << hz
                        << "Hz not accepted (error="sv << ret << "), enumerating supported modes"sv << std::endl;

      // Fallback: enumerate supported modes and pick the closest match.
      int best_idx = -1;
      int best_score = 99999999;
      int actual_w = 0;
      int actual_h = 0;
      int actual_hz = 0;

      ZeroMemory(&dm, sizeof(dm));
      dm.dmSize = sizeof(dm);

      for (int i = 0; EnumDisplaySettingsA(device_name.c_str(), i, &dm); ++i) {
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

      if (best_idx < 0) {
        BOOST_LOG(warning) << "VDD: No supported display modes found for "sv << device_name << std::endl;
        return false;
      }

      ZeroMemory(&dm, sizeof(dm));
      dm.dmSize = sizeof(dm);
      if (!EnumDisplaySettingsA(device_name.c_str(), best_idx, &dm)) {
        return false;
      }

      ret = ChangeDisplaySettingsExA(
        device_name.c_str(), &dm, nullptr,
        CDS_UPDATEREGISTRY, nullptr);

      if (ret == DISP_CHANGE_SUCCESSFUL) {
        BOOST_LOG(info) << "VDD: Set display "sv << device_name
                        << " to closest supported mode "sv << actual_w << 'x' << actual_h << '@' << actual_hz << "Hz"sv << std::endl;

        // Update stored config with the mode that was actually applied
        std::scoped_lock lock(g_handle_mutex);
        if (!g_vdd_configs.empty()) {
          g_vdd_configs.back() = {actual_w, actual_h, actual_hz};
        }
      } else {
        BOOST_LOG(warning) << "VDD: Failed to set closest mode (error="sv << ret << ')' << std::endl;
      }

      return ret == DISP_CHANGE_SUCCESSFUL;
    }

  }  // anonymous namespace

  int add_display(int width, int height, int hz) {
    int idx;

    {
      std::scoped_lock lock(g_handle_mutex);
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
    }

    BOOST_LOG(info) << "VDD: Added display #"sv << idx << " ("sv << width << 'x' << height << '@' << hz << "Hz)"sv << std::endl;

    // After VDD adds the display, wait briefly for Windows to detect it
    std::this_thread::sleep_for(500ms);

    // Find the newly added VDD display by diffing against tracked device names.
    std::set<std::string, std::less<>> known_names;
    {
      std::scoped_lock lock(g_handle_mutex);
      for (const auto &name : g_vdd_device_names) {
        known_names.insert(name);
      }
    }

    std::string new_device_name = find_new_display_name(known_names);

    if (new_device_name.empty()) {
      BOOST_LOG(warning) << "VDD: Could not find newly created display"sv << std::endl;
      VddRemoveDisplay(g_vdd_handle, idx);
      return -1;
    }

    // Only now track the driver index, device name, and persist —
    // the display is confirmed visible in Windows.
    {
      std::scoped_lock lock(g_handle_mutex);
      g_vdd_indices.push_back(idx);
      g_vdd_device_names.push_back(new_device_name);
      g_vdd_configs.push_back({width, height, hz});
    }

    persist_display_count();

    // Apply the requested display mode (with fallback enumeration)
    apply_display_mode(new_device_name, width, height, hz);

    // Re-apply stored resolutions to all previously created displays.
    // VddAddDisplay + VddUpdate can reset existing VDD displays to the
    // driver's default EDID mode, so we must restore them.
    {
      std::scoped_lock lock(g_handle_mutex);
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
    std::scoped_lock lock(g_handle_mutex);
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
      std::scoped_lock lock(g_handle_mutex);
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
      std::scoped_lock lock(g_handle_mutex);
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

      DisplayInfo display_info{};
      display_info.index = static_cast<int>(n);
      display_info.device_name = name;

      // Try to read actual display settings first; fall back to stored config
      DEVMODEA dm;
      ZeroMemory(&dm, sizeof(dm));
      dm.dmSize = sizeof(dm);
      if (EnumDisplaySettingsA(name.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
        display_info.width  = dm.dmPelsWidth;
        display_info.height = dm.dmPelsHeight;
        display_info.hz     = dm.dmDisplayFrequency;
      } else if (n < g_vdd_configs.size()) {
        display_info.width  = g_vdd_configs[n].width;
        display_info.height = g_vdd_configs[n].height;
        display_info.hz     = g_vdd_configs[n].hz;
      }

      // Parse identifier from device name
      auto pos = display_info.device_name.rfind("DISPLAY");
      if (pos != std::string::npos) {
        try {
          display_info.identifier = std::stoi(display_info.device_name.substr(pos + 7));
        } catch (const std::exception &) {
          display_info.identifier = static_cast<int>(n) + 1;
        }
      } else {
        display_info.identifier = static_cast<int>(n) + 1;
      }

      result.push_back(display_info);
    }

    BOOST_LOG(info) << "VDD: Found "sv << result.size() << " virtual display(s)"sv << std::endl;
    return result;
  }

  std::vector<DisplayInfo> get_displays() {
    std::scoped_lock lock(g_handle_mutex);
    if (!g_initialized) {
      return {};
    }
    return get_displays_internal();
  }

  int get_display_count() {
    return static_cast<int>(get_displays().size());
  }

  void start_keepalive() {
    bool expected = false;
    if (!g_keepalive_running.compare_exchange_strong(expected, true)) {
      return;
    }

    g_keepalive_thread = std::make_unique<std::jthread>([]() {
      platf::set_thread_name("vdd_keepalive");
      BOOST_LOG(info) << "VDD: Keepalive thread started"sv << std::endl;

      while (g_keepalive_running) {
        {
          std::scoped_lock lock(g_handle_mutex);
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
