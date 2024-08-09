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

// local includes
#include "platform/common.h"

// platform-specific includes
#ifdef _WIN32
  #include <display_device/windows/settings_manager.h>
  #include <display_device/windows/win_api_layer.h>
  #include <display_device/windows/win_display_device.h>
#endif

namespace display_device {
  namespace {
    /**
     * @brief A global for the settings manager interface whose lifetime is managed by `display_device::init()`.
     */
    std::unique_ptr<RetryScheduler<SettingsManagerInterface>> SM_INSTANCE;

    /**
     * @brief Construct a settings manager interface to manage display device settings.
     * @return An interface or nullptr if the OS does not support the interface.
     */
    std::unique_ptr<SettingsManagerInterface>
    make_settings_manager() {
#ifdef _WIN32
      // TODO: In the upcoming PR, add audio context capture and settings persistence
      return std::make_unique<SettingsManager>(
        std::make_shared<WinDisplayDevice>(std::make_shared<WinApiLayer>()),
        nullptr,
        std::make_unique<PersistentState>(nullptr),
        WinWorkarounds {});
#else
      return nullptr;
#endif
    }
  }  // namespace

  std::unique_ptr<platf::deinit_t>
  init() {
    // We can support re-init without any issues, however we should make sure to cleanup first!
    SM_INSTANCE = nullptr;

    // If we fail to create settings manager, this means platform is not supported and
    // we will need to provided error-free passtrough in other methods
    if (auto settings_manager { make_settings_manager() }) {
      SM_INSTANCE = std::make_unique<RetryScheduler<SettingsManagerInterface>>(std::move(settings_manager));

      const auto available_devices { SM_INSTANCE->execute([](auto &settings_iface) { return settings_iface.enumAvailableDevices(); }) };
      BOOST_LOG(info) << "Currently available display devices:\n"
                      << toJson(available_devices);

      // TODO: In the upcoming PR, schedule recovery here
    }

    class deinit_t: public platf::deinit_t {
    public:
      ~deinit_t() override {
        // TODO: In the upcoming PR, execute recovery once here
        SM_INSTANCE = nullptr;
      }
    };
    return std::make_unique<deinit_t>();
  }

  std::string
  map_output_name(const std::string &output_name) {
    if (!SM_INSTANCE) {
      // Fallback to giving back the output name if the platform is not supported.
      return output_name;
    }

    return SM_INSTANCE->execute([&output_name](auto &settings_iface) { return settings_iface.getDisplayName(output_name); });
  }
}  // namespace display_device
