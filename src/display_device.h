/**
 * @file src/display_device.h
 * @brief Declarations for display device handling.
 */
#pragma once

// local includes
#include "platform/common.h"

// forward declarations
namespace display_device {
  template <class T>
  class RetryScheduler;
  class SettingsManagerInterface;
}  // namespace display_device

namespace display_device {
  /**
   * @brief A singleton class for managing the display device configuration for the whole Sunshine session.
   *
   * This class is meant to be an entry point for applying the configuration and reverting it later
   * from within the various places in the Sunshine's source code.
   */
  class session_t {
  public:
    /**
     * @brief Get the singleton instance.
     * @returns Singleton instance for the class.
     *
     * @examples
     * session_t& session { session_t::get() };
     * @examples_end
     */
    [[nodiscard]] static session_t &
    get();

    /**
     * @brief Initialize the singleton and perform the initial state recovery (if needed).
     * @returns A deinit_t instance that performs cleanup when destroyed.
     *
     * @examples
     * const auto session_guard { session_t::init() };
     * @examples_end
     */
    [[nodiscard]] static std::unique_ptr<platf::deinit_t>
    init();

    /**
     * @brief Map the output name to a specific display.
     * @param output_name The user-configurable output name.
     * @returns Mapped display name or empty string if the output name could not be mapped.
     *
     * @examples
     * session_t& session { session_t::get() };
     * const auto mapped_name_config { session.get_display_name(config::video.output_name) };
     * const auto mapped_name_custom { session.get_display_name("{some-device-id}") };
     * @examples_end
     */
    [[nodiscard]] std::string
    map_output_name(const std::string &output_name) const;

    /**
     * @brief A deleted copy constructor for singleton pattern.
     * @note Public to ensure better error message.
     */
    session_t(session_t const &) = delete;

    /**
     * @brief A deleted assignment operator for singleton pattern.
     * @note Public to ensure better error message.
     */
    void
    operator=(session_t const &) = delete;

  private:
    /**
     * @brief A private constructor to ensure the singleton pattern.
     * @note Cannot be defaulted in declaration because of forward declared RetryScheduler.
     */
    explicit session_t();

    std::unique_ptr<RetryScheduler<SettingsManagerInterface>> impl; /**< Platform specific interface for managing settings (with retry functionality). */
  };
}  // namespace display_device
