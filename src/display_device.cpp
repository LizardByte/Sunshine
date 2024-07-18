/**
 * @file src/display_device.cpp
 * @brief Definitions for display device handling.
 */
// header include
#include "display_device.h"

// lib includes
#include <display_device/json.h>
#include <display_device/retry_scheduler.h>
#include <display_device/settings_manager_interface.h>

// platform-specific includes
#ifdef _WIN32
  #include <display_device/windows/settings_manager.h>
  #include <display_device/windows/win_api_layer.h>
  #include <display_device/windows/win_display_device.h>
#endif

namespace display_device {
  namespace {
    std::unique_ptr<SettingsManagerInterface>
    make_settings_manager() {
#ifdef _WIN32
      // TODO: In the upcoming PR, add audio context capture and settings persistence
      return std::make_unique<SettingsManager>(
        std::make_shared<WinDisplayDevice>(std::make_shared<WinApiLayer>()),
        nullptr,
        std::make_unique<PersistentState>(nullptr));
#else
      return nullptr;
#endif
    }
  }  // namespace

  session_t &
  session_t::get() {
    static session_t session;
    return session;
  }

  std::unique_ptr<platf::deinit_t>
  session_t::init() {
    // We can support re-init without any issues, however we should make sure to cleanup first!
    get().impl = nullptr;

    // If we fail to create settings manager, this means platform is not supported and
    // we will need to provided error-free passtrough in other methods
    if (auto settings_manager { make_settings_manager() }) {
      get().impl = std::make_unique<RetryScheduler<SettingsManagerInterface>>(std::move(settings_manager));

      const auto available_devices { get().impl->execute([](auto &settings_iface) { return settings_iface.enumAvailableDevices(); }) };
      BOOST_LOG(info) << "Currently available display devices:\n"
                      << toJson(available_devices);

      // TODO: In the upcoming PR, schedule recovery here
    }

    class deinit_t: public platf::deinit_t {
    public:
      ~deinit_t() override {
        // TODO: In the upcoming PR, execute recovery once here
        get().impl = nullptr;
      }
    };
    return std::make_unique<deinit_t>();
  }

  std::string
  session_t::map_output_name(const std::string &output_name) const {
    if (impl) {
      return impl->execute([&output_name](auto &settings_iface) { return settings_iface.getDisplayName(output_name); });
    }

    // Fallback to giving back the output name if the platform is not supported.
    return output_name;
  }

  session_t::session_t() = default;
}  // namespace display_device
