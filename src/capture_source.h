/**
 * @file src/capture_source.h
 * @brief Optional per-application capture source shared by config, input, and platform backends.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace capture_source {
  /**
   * @brief Capture source selected for one Sunshine application.
   */
  enum class mode_e {
    display,  ///< Capture the selected display. This is the default.
    window,  ///< Capture one window.
    application,  ///< Capture the windows that belong to one application.
  };

  /**
   * @brief How an application chooses the captured window or process.
   */
  struct target_t {
    mode_e mode {mode_e::display};  ///< Capture mode. Display leaves the existing behavior unchanged.
    std::optional<std::uint32_t> window_id;  ///< Platform window identifier, when the user selected one window.
    std::string application_id;  ///< Bundle identifier, or a `pid:` prefixed process id.
    std::string window_title;  ///< Case-insensitive title substring used when the window does not exist yet.
    std::optional<bool> show_cursor;  ///< Cursor visibility override. Unset follows the session cursor toggle.
    std::int32_t launched_pid {0};  ///< Process id Sunshine started for this app, when it is still running.
  };

  /**
   * @brief Raw application-config strings accepted from apps.json.
   */
  struct config_fields_t {
    std::optional<std::string> mode;  ///< `display`, `window`, or `application`.
    std::optional<std::string> window_id;  ///< Window identifier text.
    std::optional<std::string> application_id;  ///< Bundle identifier or `pid:` value.
    std::optional<std::string> window_title;  ///< Window title substring.
    std::optional<std::string> show_cursor;  ///< `true` or `false`.
  };

  /**
   * @brief One shareable window reported to the web UI.
   */
  struct window_t {
    std::uint32_t window_id {0};  ///< Platform window identifier.
    std::string title;  ///< Window title.
    std::string application_id;  ///< Bundle identifier of the owning application.
    std::string application_name;  ///< Display name of the owning application.
    std::int32_t pid {0};  ///< Process id of the owning application.
    int width {0};  ///< Window width in points.
    int height {0};  ///< Window height in points.
  };

  /**
   * @brief One running application that can be captured.
   */
  struct application_t {
    std::string application_id;  ///< Bundle identifier.
    std::string application_name;  ///< Display name.
    std::int32_t pid {0};  ///< Process id.
  };

  /**
   * @brief Window rectangle used while choosing a capture target.
   */
  struct window_snapshot_t {
    std::uint32_t window_id {0};  ///< Platform window identifier.
    std::string title;  ///< Window title.
    std::string bundle_id;  ///< Bundle identifier of the owning application.
    std::int32_t pid {0};  ///< Process id of the owning application.
    int x {0};  ///< Horizontal origin in desktop points.
    int y {0};  ///< Vertical origin in desktop points.
    int width {0};  ///< Width in points.
    int height {0};  ///< Height in points.
    bool on_screen {false};  ///< Whether the window is currently on a display.
  };

  /**
   * @brief Capture target selected from the current window list.
   */
  struct match_t {
    bool application {false};  ///< `true` when the match is every window of one application.
    std::uint32_t window_id {0};  ///< Matched window, or zero for an application match.
    std::int32_t pid {0};  ///< Process id that should receive keyboard focus.
    std::string bundle_id;  ///< Bundle identifier of the matched application.
    int x {0};  ///< Horizontal origin of the captured area in desktop points.
    int y {0};  ///< Vertical origin of the captured area in desktop points.
    int width {0};  ///< Width of the captured area in points.
    int height {0};  ///< Height of the captured area in points.
  };

  /**
   * @brief Latest window origin published for absolute pointer input.
   */
  struct geometry_t {
    int x {0};  ///< Horizontal origin in desktop points.
    int y {0};  ///< Vertical origin in desktop points.
    int width {0};  ///< Width in points.
    int height {0};  ///< Height in points.
  };

  /**
   * @brief PCM layout of application audio captured with the target.
   */
  struct audio_spec_t {
    std::uint32_t sample_rate {48000};  ///< Sample rate in hertz.
    std::uint32_t channels {2};  ///< Channel count.
  };

  /**
   * @brief Parse an application capture block.
   *
   * @param fields Raw config strings. Empty mode means display capture.
   * @return Parsed target. An unrecognized mode is reported as display.
   */
  target_t target_from_config(const config_fields_t &fields);

  /**
   * @brief Report whether a capture mode string was recognized.
   *
   * @param mode Raw mode string. Empty is recognized as display.
   * @return `true` when the mode is empty or one of the supported names.
   */
  bool mode_recognized(const std::optional<std::string> &mode);

  /**
   * @brief Return the config name of a capture mode.
   *
   * @param mode Capture mode.
   * @return `display`, `window`, or `application`.
   */
  std::string_view mode_name(mode_e mode);

  /**
   * @brief Report whether this platform can capture the requested source.
   *
   * @param mode Capture mode.
   * @return `true` for display capture, and for window or application capture where it is implemented.
   */
  bool implemented(mode_e mode);

  /**
   * @brief Choose the window or application described by a target.
   *
   * @param target Capture target from the running application.
   * @param windows Windows visible to the capture backend, including occluded windows.
   * @return The matched target, or `nullopt` when it is not available yet.
   */
  std::optional<match_t> match_target(const target_t &target, const std::vector<window_snapshot_t> &windows);

  /**
   * @brief Publish the capture target for the application that is starting.
   *
   * @param target Capture target parsed from that application.
   */
  void activate(const target_t &target);

  /**
   * @brief Drop the active capture target and any window geometry or application audio.
   */
  void clear();

  /**
   * @brief Return the capture target of the running application.
   *
   * @return Active target, or display capture when no application target is active.
   */
  target_t active();

  /**
   * @brief Mark the current thread group as a streaming capture rather than an encoder probe.
   */
  void begin_stream_capture();

  /**
   * @brief Mark the end of a streaming capture.
   */
  void end_stream_capture();

  /**
   * @brief Return the capture target that streaming capture should use.
   *
   * Encoder probes call the display backend before an application stream exists, so they receive
   * display capture even when an application target is already active.
   *
   * @return Active target during a stream, otherwise display capture.
   */
  target_t stream_target();

  /**
   * @brief Publish the captured window origin for absolute pointer input.
   *
   * @param geometry Window origin and size in desktop points.
   */
  void set_live_geometry(const geometry_t &geometry);

  /**
   * @brief Return the captured window origin, when window or application capture is active.
   *
   * @return Latest geometry, or `nullopt` during display capture.
   */
  std::optional<geometry_t> live_geometry();

  /**
   * @brief Remember which process should be focused when a remote input event arrives.
   *
   * @param pid Process id, or zero when no application should be focused.
   */
  void set_focus_pid(std::int32_t pid);

  /**
   * @brief Return the process that should receive streamed keyboard input.
   *
   * @return Process id, or zero when display capture is active.
   */
  std::int32_t focus_pid();

  /**
   * @brief Bring the captured application forward when a remote input event arrives.
   */
  void focus_active_target();

  /**
   * @brief Register the platform implementation of window listing and focus.
   *
   * @param list_windows Enumerates shareable windows.
   * @param list_applications Enumerates shareable applications.
   * @param focus Activates the process returned by focus_pid().
   */
  void set_provider(
    std::function<std::vector<window_t>()> list_windows,
    std::function<std::vector<application_t>()> list_applications,
    std::function<void()> focus
  );

  /**
   * @brief List windows the host can capture.
   *
   * @return Shareable windows, or an empty list when this platform has no provider.
   */
  std::vector<window_t> list_windows();

  /**
   * @brief List applications the host can capture.
   *
   * @return Shareable applications, or an empty list when this platform has no provider.
   */
  std::vector<application_t> list_applications();

  /**
   * @brief Record the audio layout requested by the stream.
   *
   * @param spec Sample rate and channel count expected by the audio encoder.
   */
  void set_audio_request(const audio_spec_t &spec);

  /**
   * @brief Return the audio layout requested by the stream.
   *
   * @return Requested layout, or 48 kHz stereo before a stream asks for one.
   */
  audio_spec_t audio_request();

  /**
   * @brief Start accepting application audio from the capture backend.
   *
   * @param produced Layout of the samples that will be pushed.
   */
  void open_audio(const audio_spec_t &produced);

  /**
   * @brief Stop application audio and wake any reader waiting for samples.
   */
  void close_audio();

  /**
   * @brief Append interleaved float samples captured from the target application.
   *
   * @param interleaved Interleaved PCM samples.
   * @param sample_count Number of float samples, counting every channel.
   */
  void push_audio(const float *interleaved, std::size_t sample_count);

  /**
   * @brief Copy one audio frame from the target application into the encoder buffer.
   *
   * @param destination Interleaved destination. Its size is the number of floats to produce.
   * @param destination_count Number of floats to write.
   * @param sample_rate Sample rate expected by the caller.
   * @param channels Channel count expected by the caller.
   * @param timeout Maximum time to wait for samples.
   * @return `true` when `destination` was filled.
   */
  bool read_audio(float *destination, std::size_t destination_count, std::uint32_t sample_rate, std::uint32_t channels, std::chrono::milliseconds timeout);

  /**
   * @brief Convert interleaved PCM between capture and encoder layouts.
   *
   * @param source Interleaved source samples.
   * @param source_frames Frames available in `source`.
   * @param source_rate Source sample rate.
   * @param source_channels Source channel count.
   * @param destination Interleaved destination for `destination_frames`.
   * @param destination_frames Frames to write.
   * @param destination_rate Destination sample rate.
   * @param destination_channels Destination channel count.
   */
  void convert_interleaved(
    const float *source,
    std::size_t source_frames,
    std::uint32_t source_rate,
    std::uint32_t source_channels,
    float *destination,
    std::size_t destination_frames,
    std::uint32_t destination_rate,
    std::uint32_t destination_channels
  );
}  // namespace capture_source
