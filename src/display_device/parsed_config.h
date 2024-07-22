#pragma once

// local includes
#include "display_device.h"

// forward declarations
namespace config {
  struct video_t;
}
namespace rtsp_stream {
  struct launch_session_t;
}

namespace display_device {

  /**
   * @brief Configuration containing parsed information from the user config (video related)
   *        and the current session.
   */
  struct parsed_config_t {
    /**
     * @brief Enum detailing how to prepare the display device.
     */
    enum class device_prep_e : int {
      no_operation, /**< User has to make sure the display device is active, we will only verify. */
      ensure_active, /**< Activate the device if needed. */
      ensure_primary, /**< Activate the device if needed and make it a primary display. */
      ensure_only_display /**< Deactivate other displays and turn on the specified one only. */
    };

    /**
     * @brief Convert the string to the matching value of device_prep_e.
     * @param value String value to map to device_prep_e.
     * @returns A device_prep_e value (converted to int) that matches the string
     *          or the default value if string does not match anything.
     * @see device_prep_e
     *
     * EXAMPLES:
     * ```cpp
     * const int device_prep = device_prep_from_view("ensure_only_display");
     * ```
     */
    static int
    device_prep_from_view(std::string_view value);

    /**
     * @brief Enum detailing how to change the display's resolution.
     */
    enum class resolution_change_e : int {
      no_operation, /**< Keep the current resolution. */
      automatic, /**< Set the resolution to the one received from the client if the "Optimize game settings" option is also enabled in the client. */
      manual /**< User has to specify the resolution ("Optimize game settings" option must be enabled in the client). */
    };

    /**
     * @brief Convert the string to the matching value of resolution_change_e.
     * @param value String value to map to resolution_change_e.
     * @returns A resolution_change_e value (converted to int) that matches the string
     *          or the default value if string does not match anything.
     * @see resolution_change_e
     *
     * EXAMPLES:
     * ```cpp
     * const int resolution_change = resolution_change_from_view("manual");
     * ```
     */
    static int
    resolution_change_from_view(std::string_view value);

    /**
     * @brief Enum detailing how to change the display's refresh rate.
     */
    enum class refresh_rate_change_e : int {
      no_operation, /**< Keep the current refresh rate. */
      automatic, /**< Set the refresh rate to the FPS value received from the client. */
      manual /**< User has to specify the refresh rate. */
    };

    /**
     * @brief Convert the string to the matching value of refresh_rate_change_e.
     * @param value String value to map to refresh_rate_change_e.
     * @returns A refresh_rate_change_e value (converted to int) that matches the string
     *          or the default value if string does not match anything.
     * @see refresh_rate_change_e
     *
     * EXAMPLES:
     * ```cpp
     * const int refresh_rate_change = refresh_rate_change_from_view("manual");
     * ```
     */
    static int
    refresh_rate_change_from_view(std::string_view value);

    /**
     * @brief Enum detailing how to change the display's HDR state.
     */
    enum class hdr_prep_e : int {
      no_operation, /**< User has to switch the HDR state manually */
      automatic /**< Switch HDR state based on the session settings and if display supports it. */
    };

    /**
     * @brief Convert the string to the matching value of hdr_prep_e.
     * @param value String value to map to hdr_prep_e.
     * @returns A hdr_prep_e value (converted to int) that matches the string
     *          or the default value if string does not match anything.
     * @see hdr_prep_e
     *
     * EXAMPLES:
     * ```cpp
     * const int hdr_prep = hdr_prep_from_view("automatic");
     * ```
     */
    static int
    hdr_prep_from_view(std::string_view value);

    std::string device_id; /**< Device id manually provided by the user via config. */
    device_prep_e device_prep; /**< The device_prep_e value taken from config. */
    boost::optional<resolution_t> resolution; /**< Parsed resolution value we need to switch to. Empty optional if no action is required. */
    boost::optional<refresh_rate_t> refresh_rate; /**< Parsed refresh rate value we need to switch to. Empty optional if no action is required. */
    boost::optional<bool> change_hdr_state; /**< Parsed HDR state value we need to switch to (true == ON, false == OFF). Empty optional if no action is required. */
  };

  /**
   * @brief Parse the user configuration and the session information.
   * @param config User's video related configuration.
   * @param session Session information.
   * @returns Parsed configuration or empty optional if parsing has failed.
   *
   * EXAMPLES:
   * ```cpp
   * const std::shared_ptr<rtsp_stream::launch_session_t> launch_session; // Assuming ptr is properly initialized
   * const config::video_t &video_config { config::video };
   * const auto parsed_config = make_parsed_config(video_config, *launch_session);
   * ```
   */
  boost::optional<parsed_config_t>
  make_parsed_config(const config::video_t &config, const rtsp_stream::launch_session_t &session);

}  // namespace display_device
