/**
 * @file src/config.h
 * @brief Declarations for the configuration of Sunshine.
 */
#pragma once

// standard includes
#include <bitset>
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// local includes
#include "nvenc/nvenc_config.h"

namespace config {
  // Valid range for the packetsize limit
  constexpr int PACKETSIZE_MIN = 200;  ///< Lowest accepted configured packet size in bytes.
  constexpr int PACKETSIZE_MAX = 65535;  ///< Highest accepted configured packet size in bytes.
  constexpr int PACKETSIZE_SMALL = 500;  ///< Conservative packet size used for low-MTU links.
  constexpr int PACKETSIZE_LARGE = 1456;  ///< Default large packet size that avoids common MTU fragmentation.

  // track modified config options
  inline std::unordered_map<std::string, std::string> modified_config_settings;  ///< Configuration keys changed during the current parse or UI update.

  // sensitive values that should be redacted from logging
  /**
   * @brief Configuration keys whose values must be hidden in logs.
   */
  inline constexpr std::array redacted_config = {
    "csrf_allowed_origins"
  };

  /**
   * @brief Log configuration entries and optionally mark them for persistence.
   *
   * @param vars Parsed configuration entries to log.
   * @param save Whether modified configuration values should be written back to disk.
   */
  void log_config_settings(const std::unordered_map<std::string, std::string> &vars, bool save);

  /**
   * @brief Video encoder, capture, and color settings loaded from configuration.
   */
  struct video_t {
    // ffmpeg params
    /**
     * @brief Quantization parameter used by encoders where higher values trade quality for compression.
     */
    int qp;  // higher == more compression and less quality

    int hevc_mode;  ///< HEVC support mode advertised to clients.
    int av1_mode;  ///< AV1 support mode advertised to clients.

    int min_threads;  ///< Minimum number of threads or slices for CPU encoding.

    struct {
      std::string sw_preset;
      std::string sw_tune;
      std::optional<int> svtav1_preset;
    } sw;  ///< Software encoder options.

    nvenc::nvenc_config nv;  ///< NVIDIA NVENC encoder settings.
    bool nv_realtime_hags;  ///< Enable the NVIDIA realtime HAGS workaround.
    bool nv_opengl_vulkan_on_dxgi;  ///< Prefer NVIDIA OpenGL/Vulkan-on-DXGI interop.
    bool nv_sunshine_high_power_mode;  ///< Request NVIDIA high-power mode for Sunshine.

    struct {
      int preset;
      int multipass;
      int h264_coder;
      int aq;
      int vbv_percentage_increase;
    } nv_legacy;  ///< Legacy NVIDIA encoder options kept for config compatibility.

    struct {
      std::optional<int> qsv_preset;
      std::optional<int> qsv_cavlc;
      bool qsv_slow_hevc;
    } qsv;  ///< Intel Quick Sync encoder options.

    struct {
      std::optional<int> amd_usage_h264;
      std::optional<int> amd_usage_hevc;
      std::optional<int> amd_usage_av1;
      std::optional<int> amd_rc_h264;
      std::optional<int> amd_rc_hevc;
      std::optional<int> amd_rc_av1;
      std::optional<int> amd_enforce_hrd;
      std::optional<int> amd_quality_h264;
      std::optional<int> amd_quality_hevc;
      std::optional<int> amd_quality_av1;
      std::optional<int> amd_preanalysis;
      std::optional<int> amd_vbaq;
      int amd_coder;
    } amd;  ///< AMD AMF encoder options.

    struct {
      int vt_allow_sw;
      int vt_require_sw;
      int vt_realtime;
      int vt_coder;
    } vt;  ///< VideoToolbox encoder options.

    struct {
      bool blbrc;
      std::optional<int> vaapi_quality;
      std::optional<int> vaapi_rc;
      std::string vaapi_rc_str;
      bool strict_rc_buffer;
    } vaapi;  ///< VA-API encoder options.

    struct {
      int tune;  // 0=default, 1=hq, 2=ll, 3=ull, 4=lossless
      int rc_mode;  // 0=driver, 1=cqp, 2=cbr, 4=vbr
    } vk;  ///< Vulkan encoder options.

    std::string capture;  ///< Capture backend name selected by configuration.
    std::string encoder;  ///< Encoder backend name selected by configuration.
    std::string adapter_name;  ///< Display adapter name selected in configuration.
    std::string output_name;  ///< Display output name selected in configuration.

    /**
     * @brief Display-device integration settings.
     */
    struct dd_t {
      /**
       * @brief Compatibility workarounds for display-device control.
       */
      struct workarounds_t {
        std::chrono::milliseconds hdr_toggle_delay;  ///< Specify whether to apply HDR high-contrast color workaround and what delay to use.
      };

      /**
       * @brief Selects how Sunshine prepares the active display before streaming.
       */
      enum class config_option_e {
        disabled,  ///< Disable the configuration for the device.
        verify_only,  ///< @seealso{display_device::SingleDisplayConfiguration::DevicePreparation}
        ensure_active,  ///< @seealso{display_device::SingleDisplayConfiguration::DevicePreparation}
        ensure_primary,  ///< @seealso{display_device::SingleDisplayConfiguration::DevicePreparation}
        ensure_only_display  ///< @seealso{display_device::SingleDisplayConfiguration::DevicePreparation}
      };

      /**
       * @brief Selects how Sunshine chooses the stream display resolution.
       */
      enum class resolution_option_e {
        disabled,  ///< Do not change resolution.
        automatic,  ///< Change resolution and use the one received from Moonlight.
        manual  ///< Change resolution and use the manually provided one.
      };

      /**
       * @brief Selects how Sunshine chooses the stream display refresh rate.
       */
      enum class refresh_rate_option_e {
        disabled,  ///< Do not change refresh rate.
        automatic,  ///< Change refresh rate and use the one received from Moonlight.
        manual  ///< Change refresh rate and use the manually provided one.
      };

      /**
       * @brief Selects how Sunshine handles HDR state for the stream display.
       */
      enum class hdr_option_e {
        disabled,  ///< Do not change HDR settings.
        automatic  ///< Change HDR settings and use the state requested by Moonlight.
      };

      /**
       * @brief Single display mode remapping rule from configuration.
       */
      struct mode_remapping_entry_t {
        std::string requested_resolution;  ///< Resolution string requested by the client.
        std::string requested_fps;  ///< Refresh-rate string requested by the client.
        std::string final_resolution;  ///< Resolution string to apply after remapping.
        std::string final_refresh_rate;  ///< Refresh-rate string to apply after remapping.
      };

      /**
       * @brief Collection of display mode remapping rules.
       */
      struct mode_remapping_t {
        std::vector<mode_remapping_entry_t> mixed;  ///< To be used when `resolution_option` and `refresh_rate_option` is set to `automatic`.
        std::vector<mode_remapping_entry_t> resolution_only;  ///< To be use when only `resolution_option` is set to `automatic`.
        std::vector<mode_remapping_entry_t> refresh_rate_only;  ///< To be use when only `refresh_rate_option` is set to `automatic`.
      };

      config_option_e configuration_option;  ///< Display-preparation mode selected by configuration.
      resolution_option_e resolution_option;  ///< Resolution-selection mode selected by configuration.
      std::string manual_resolution;  ///< Manual resolution in case `resolution_option == resolution_option_e::manual`.
      refresh_rate_option_e refresh_rate_option;  ///< Refresh-rate selection mode selected by configuration.
      std::string manual_refresh_rate;  ///< Manual refresh rate in case `refresh_rate_option == refresh_rate_option_e::manual`.
      hdr_option_e hdr_option;  ///< HDR-selection mode selected by configuration.
      std::chrono::milliseconds config_revert_delay;  ///< Time to wait until settings are reverted (after stream ends/app exists).
      bool config_revert_on_disconnect;  ///< Specify whether to revert display configuration on client disconnect.
      mode_remapping_t mode_remapping;  ///< Display mode remapping rules grouped by automatic selection mode.
      workarounds_t wa;  ///< Display-device compatibility workarounds.
    } dd;  ///< Display-device integration settings.

    int max_bitrate;  ///< Maximum bitrate ceiling in kbps for bitrate requested from the client.
    double minimum_fps_target;  ///< Lowest framerate that will be used when streaming. Range 0-1000, 0 = half of client's requested framerate.
  };

  /**
   * @brief Audio capture and encoder settings loaded from configuration.
   */
  struct audio_t {
    std::string sink;  ///< Audio output device/sink to use for audio capture
    std::string virtual_sink;  ///< Virtual audio sink for audio routing
    bool stream;  ///< Enable audio streaming to clients
    bool install_steam_drivers;  ///< Install Steam audio drivers for enhanced compatibility
  };

  /**
   * @brief Encryption policy that always sends unencrypted video.
   */
  constexpr int ENCRYPTION_MODE_NEVER = 0;  // Never use video encryption, even if the client supports it
  /**
   * @brief Encryption policy that uses encrypted video only when the client supports it.
   */
  constexpr int ENCRYPTION_MODE_OPPORTUNISTIC = 1;  // Use video encryption if available, but stream without it if not supported
  /**
   * @brief Encryption policy that rejects clients without video encryption support.
   */
  constexpr int ENCRYPTION_MODE_MANDATORY = 2;  // Always use video encryption and refuse clients that can't encrypt

  /**
   * @brief Network stream settings shared by audio, video, and control channels.
   */
  struct stream_t {
    std::chrono::milliseconds ping_timeout;  ///< Timeout used when waiting for client ping responses.

    std::string file_apps;  ///< Path to the configured applications file.

    int fec_percentage;  ///< Percentage of forward-error-correction packets to add to the stream.

    // Video encryption settings for LAN and WAN streams
    int lan_encryption_mode;  ///< Video encryption policy for LAN clients.
    int wan_encryption_mode;  ///< Video encryption policy for WAN clients.

    // Limit the packetsize to avoid fragmentation on a low MTU link
    int packetsize;  ///< Maximum payload size for network packets.
  };

  /**
   * @brief HTTP and HTTPS settings used by the GameStream pairing server.
   */
  struct nvhttp_t {
    // Could be any of the following values:
    // pc|lan|wan
    std::string origin_web_ui_allowed;  ///< Origin policy used for Web UI access checks.

    std::string pkey;  ///< Private key PEM string or path.
    std::string cert;  ///< Certificate PEM string or path.

    std::string sunshine_name;  ///< Host name advertised to Moonlight clients.

    std::string file_state;  ///< Path to the persisted Sunshine state file.

    std::string external_ip;  ///< External address advertised to clients when configured.
  };

  /**
   * @brief Input emulation settings loaded from configuration.
   */
  struct input_t {
    std::unordered_map<int, int> keybindings;  ///< Client keycode to platform keycode bindings.

    std::chrono::milliseconds back_button_timeout;  ///< Hold duration that turns a controller Back button into a special action.
    std::chrono::milliseconds key_repeat_delay;  ///< Delay before repeating a held keyboard key.
    std::chrono::duration<double> key_repeat_period;  ///< Interval between repeated keyboard key events.

    std::string gamepad;  ///< Virtual controller backend selected by configuration.
    bool ds4_back_as_touchpad_click;  ///< Map the DS4 Back button to a touchpad click.
    bool motion_as_ds4;  ///< Expose motion controls through the DS4 protocol.
    bool touchpad_as_ds4;  ///< Expose touchpad input through the DS4 protocol.
    bool ds5_inputtino_randomize_mac;  ///< Randomize the inputtino DualSense MAC address.

    bool keyboard;  ///< Enable keyboard input from clients.
    bool key_rightalt_to_key_win;  ///< Map the client Right Alt key to the Windows key.
    bool mouse;  ///< Enable mouse input from clients.
    bool controller;  ///< Enable controller input from clients.

    bool always_send_scancodes;  ///< Always send keyboard scancodes when available.

    bool high_resolution_scrolling;  ///< Enable high-resolution mouse-wheel events.
    bool native_pen_touch;  ///< Enable native pen and touch injection.
  };

  namespace flag {
    /**
     * @brief Enumerates supported flag options.
     */
    enum flag_e : std::size_t {
      PIN_STDIN = 0,  ///< Read PIN from stdin instead of http
      FRESH_STATE,  ///< Do not load or save state
      FORCE_VIDEO_HEADER_REPLACE,  ///< force replacing headers inside video data
      UPNP,  ///< Try Universal Plug 'n Play
      CONST_PIN,  ///< Use "universal" pin
      FLAG_SIZE  ///< Number of flags
    };
  }  // namespace flag

  /**
   * @brief External preparation command plus its privilege requirement.
   */
  struct prep_cmd_t {
    /**
     * @brief Build a preparation command entry from parsed configuration data.
     *
     * @param do_cmd Command to run before the application starts.
     * @param undo_cmd Command to run after the application exits.
     * @param elevated Whether the command should run with elevated privileges.
     */
    prep_cmd_t(std::string &&do_cmd, std::string &&undo_cmd, bool &&elevated):
        do_cmd(std::move(do_cmd)),
        undo_cmd(std::move(undo_cmd)),
        elevated(std::move(elevated)) {
    }

    /**
     * @brief Build a preparation command entry from parsed configuration data.
     *
     * @param do_cmd Command to run before the application starts.
     * @param elevated Whether the command should run with elevated privileges.
     */
    explicit prep_cmd_t(std::string &&do_cmd, bool &&elevated):
        do_cmd(std::move(do_cmd)),
        elevated(std::move(elevated)) {
    }

    std::string do_cmd;  ///< Command to run before the application starts.
    std::string undo_cmd;  ///< Command to run after the application exits.
    bool elevated;  ///< Whether the process should be launched elevated.
  };

  /**
   * @brief Top-level Sunshine configuration and credential state.
   */
  struct sunshine_t {
    std::string locale;  ///< Locale selected for Sunshine UI and log messages.
    int min_log_level;  ///< Minimum severity level written to the configured log sink.
    std::bitset<flag::FLAG_SIZE> flags;  ///< Runtime flags parsed from command-line options.
    std::string credentials_file;  ///< Path to the stored pairing credentials file.

    std::string username;  ///< Username for the local Web UI account.
    std::string password;  ///< Password hash or secret for the local Web UI account.
    std::string salt;  ///< Salt used when hashing the Web UI password.

    std::string config_file;  ///< Path to the active Sunshine configuration file.

    /**
     * @brief Command-line options parsed before configuration loading.
     */
    struct cmd_t {
      std::string name;  ///< Executable name from the command line.
      int argc;  ///< Number of command-line arguments.
      char **argv;  ///< Command-line argument vector.
    } cmd;  ///< Command line used to launch the application.

    std::uint16_t port;  ///< TCP port used by Sunshine services.
    std::string address_family;  ///< Address family requested for listening sockets.
    std::string bind_address;  ///< Local address Sunshine should bind to.

    std::string log_file;  ///< Path to the configured log file.
    bool notify_pre_releases;  ///< Notify users about pre-release updates.
    bool system_tray;  ///< Enable the system tray integration.
    std::vector<prep_cmd_t> prep_cmds;  ///< Preparation commands executed around application launch.

    // List of allowed origins for CSRF protection (e.g., "https://example.com,https://app.example.com")
    // Comma-separated list of additional origins. Default includes localhost variants and web UI port.
    std::vector<std::string> csrf_allowed_origins;  ///< Additional origins allowed by CSRF validation.
  };

  extern video_t video;
  extern audio_t audio;
  extern stream_t stream;
  extern nvhttp_t nvhttp;
  extern input_t input;
  extern sunshine_t sunshine;

  /**
   * @brief Parse serialized text into the corresponding runtime representation.
   *
   * @param argc Number of command-line arguments.
   * @param argv Command-line argument vector.
   * @return 0 on success; nonzero when command-line or configuration parsing fails.
   */
  int parse(int argc, char *argv[]);
  /**
   * @brief Parse Sunshine configuration text into key-value entries.
   *
   * @param file_content Raw configuration file contents to parse.
   * @return Parsed configuration key-value entries.
   */
  std::unordered_map<std::string, std::string> parse_config(const std::string_view &file_content);
}  // namespace config
