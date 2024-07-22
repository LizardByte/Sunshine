#pragma once

// standard includes
#include <filesystem>
#include <memory>

// local includes
#include "parsed_config.h"

namespace display_device {

  /**
   * @brief A platform specific class that can apply configuration to the display device and later revert it.
   *
   * Main goals of this class:
   *   - Apply the configuration to the display device.
   *   - Revert the applied configuration to get back to the initial state.
   *   - Save and load the previous state to/from a file.
   */
  class settings_t {
  public:
    /**
     * @brief Platform specific persistent data.
     */
    struct persistent_data_t;

    /**
     * @brief Platform specific non-persistent audio data in case we need to manipulate
     *        audio session and keep some temporary data around.
     */
    struct audio_data_t;

    /**
     * @brief The result value of the apply_config with additional metadata.
     * @note Metadata is used when generating an XML status report to the client.
     * @see apply_config
     */
    struct apply_result_t {
      /**
       * @brief Possible result values/reasons from apply_config.
       * @note There is no deeper meaning behind the values. They simply represent
       *       the stage where the method has failed to give some hints to the user.
       * @note The value of 700 has no special meaning and is just arbitrary.
       * @see apply_config
       */
      enum class result_e : int {
        success,
        topology_fail,
        primary_display_fail,
        modes_fail,
        hdr_states_fail,
        file_save_fail,
        revert_fail
      };

      /**
       * @brief Convert the result to boolean equivalent.
       * @returns True if result means success, false otherwise.
       *
       * EXAMPLES:
       * ```cpp
       * const apply_result_t result { result_e::topology_fail };
       * if (result) {
       *   // Handle good result
       * }
       * else {
       *   // Handle bad result
       * }
       * ```
       */
      explicit
      operator bool() const;

      /**
       * @brief Get a string message with better explanation for the result.
       * @returns String message for the result.
       *
       * EXAMPLES:
       * ```cpp
       * const apply_result_t result { result_e::topology_fail };
       * if (!result) {
       *   const int error_message = result.get_error_message();
       * }
       * ```
       */
      [[nodiscard]] std::string
      get_error_message() const;

      result_e result; /**< The result value. */
    };

    /**
     * @brief A platform specific default constructor.
     * @note Needed due to forwarding declarations used by the class.
     */
    explicit settings_t();

    /**
     * @brief A platform specific destructor.
     * @note Needed due to forwarding declarations used by the class.
     */
    virtual ~settings_t();

    /**
     * @brief Check whether it is already known that changing settings will fail due to various reasons.
     * @returns True if it's definitely known that changing settings will fail, false otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * settings_t settings;
     * const bool will_fail { settings.is_changing_settings_going_to_fail() };
     * ```
     */
    bool
    is_changing_settings_going_to_fail() const;

    /**
     * @brief Set the file path for persistent data.
     *
     * EXAMPLES:
     * ```cpp
     * settings_t settings;
     * settings.set_filepath("/foo/bar.json");
     * ```
     */
    void
    set_filepath(std::filesystem::path filepath);

    /**
     * @brief Apply the parsed configuration.
     * @param config A parsed and validated configuration.
     * @returns The apply result value.
     * @see apply_result_t
     * @see parsed_config_t
     *
     * EXAMPLES:
     * ```cpp
     * const parsed_config_t config;
     *
     * settings_t settings;
     * const auto result = settings.apply_config(config);
     * ```
     */
    apply_result_t
    apply_config(const parsed_config_t &config);

    /**
     * @brief Revert the applied configuration and restore the previous settings.
     * @note It automatically loads the settings from persistence file if cached settings do not exist.
     * @returns True if settings were reverted or there was nothing to revert, false otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * const std::shared_ptr<rtsp_stream::launch_session_t> launch_session; // Assuming ptr is properly initialized
     * const config::video_t &video_config { config::video };
     *
     * settings_t settings;
     * const auto result = settings.apply_config(video_config, *launch_session);
     * if (result) {
     *   // Wait for some time
     *   settings.revert_settings();
     * }
     * ```
     */
    bool
    revert_settings();

    /**
     * @brief Reset the persistence and currently held initial display state.
     * @see session_t::reset_persistence for more details.
     *
     * EXAMPLES:
     * ```cpp
     * const std::shared_ptr<rtsp_stream::launch_session_t> launch_session; // Assuming ptr is properly initialized
     * const config::video_t &video_config { config::video };
     *
     * settings_t settings;
     * const auto result = settings.apply_config(video_config, *launch_session);
     * if (result) {
     *   // Wait for some time
     *   if (settings.revert_settings()) {
     *     // Wait for user input
     *     const bool user_wants_reset { true };
     *     if (user_wants_reset) {
     *       settings.reset_persistence();
     *     }
     *   }
     * }
     * ```
     */
    void
    reset_persistence();

  private:
    std::unique_ptr<persistent_data_t> persistent_data; /**< Platform specific persistent data. */
    std::unique_ptr<audio_data_t> audio_data; /**< Platform specific temporary audio data. */
    std::filesystem::path filepath; /**< Filepath for persistent file. */
  };

}  // namespace display_device
