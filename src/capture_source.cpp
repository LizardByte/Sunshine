/**
 * @file src/capture_source.cpp
 * @brief Definitions for the optional per-application capture source.
 */

// standard includes
#include <algorithm>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <limits>
#include <mutex>
#include <utility>

// local includes
#include "capture_source.h"

namespace capture_source {
  namespace {
    std::mutex state_mutex;  ///< Guards the active target, geometry, and application audio.
    std::condition_variable audio_available;  ///< Wakes audio readers when samples arrive or capture stops.
    target_t active_target;  ///< Capture target of the running application.
    int stream_depth {0};  ///< Number of streaming capture threads that should honor the active target.
    std::optional<geometry_t> geometry;  ///< Latest captured window origin.
    std::int32_t focused_pid {0};  ///< Process that should receive remote keyboard input.
    std::function<std::vector<window_t>()> windows_provider;  ///< Platform window enumeration.
    std::function<std::vector<application_t>()> applications_provider;  ///< Platform application enumeration.
    std::function<void()> focus_provider;  ///< Platform focus handler.

    audio_spec_t requested_audio {48000, 2};  ///< Layout the audio encoder asked for.
    audio_spec_t produced_audio {48000, 2};  ///< Layout of samples pushed by the capture backend.
    bool audio_is_open {false};  ///< Whether application audio is currently being captured.
    std::deque<float> audio_samples;  ///< Interleaved application audio waiting for the encoder.

    /**
     * @brief Compare a capture mode name without regard to case.
     *
     * @param value Config value.
     * @param expected Expected mode name.
     * @return `true` when the letters match.
     */
    bool equals_ignore_case(std::string_view value, std::string_view expected) {
      if (value.size() != expected.size()) {
        return false;
      }

      for (std::size_t index = 0; index < value.size(); ++index) {
        const auto left = static_cast<unsigned char>(value[index]);
        const auto right = static_cast<unsigned char>(expected[index]);
        if (std::tolower(left) != std::tolower(right)) {
          return false;
        }
      }

      return true;
    }

    /**
     * @brief Parse a positive window identifier.
     *
     * @param text Config text.
     * @return Window id, or `nullopt` when the text is empty or not a positive integer.
     */
    std::optional<std::uint32_t> parse_window_id(const std::optional<std::string> &text) {
      if (!text || text->empty()) {
        return std::nullopt;
      }

      try {
        std::size_t consumed = 0;
        const auto value = std::stoull(*text, &consumed, 10);
        if (consumed != text->size() || value == 0 || value > std::numeric_limits<std::uint32_t>::max()) {
          return std::nullopt;
        }
        return static_cast<std::uint32_t>(value);
      } catch (const std::exception &) {
        return std::nullopt;
      }
    }

    /**
     * @brief Parse a boolean config string.
     *
     * @param text Config text.
     * @return The boolean value, or `nullopt` when the text is empty or unrecognized.
     */
    std::optional<bool> parse_bool(const std::optional<std::string> &text) {
      if (!text || text->empty()) {
        return std::nullopt;
      }
      if (equals_ignore_case(*text, "true") || *text == "1") {
        return true;
      }
      if (equals_ignore_case(*text, "false") || *text == "0") {
        return false;
      }
      return std::nullopt;
    }

    /**
     * @brief Report whether a title contains a substring, ignoring ASCII case.
     *
     * @param title Window title.
     * @param expected Expected substring. Empty matches every title.
     * @return `true` when the substring is present.
     */
    bool title_matches(std::string_view title, std::string_view expected) {
      if (expected.empty()) {
        return true;
      }

      auto lower = [](std::string_view value) {
        std::string folded(value);
        std::ranges::transform(folded, folded.begin(), [](unsigned char character) {
          return static_cast<char>(std::tolower(character));
        });
        return folded;
      };

      return lower(title).find(lower(expected)) != std::string::npos;
    }

    /**
     * @brief Read a `pid:` application id.
     *
     * @param application_id Application id from config.
     * @return Process id, or zero when the value is a bundle id or empty.
     */
    std::int32_t pid_from_application_id(std::string_view application_id) {
      constexpr auto prefix = std::string_view {"pid:"};
      if (!application_id.starts_with(prefix)) {
        return 0;
      }

      try {
        std::size_t consumed = 0;
        const auto text = std::string {application_id.substr(prefix.size())};
        const auto value = std::stoll(text, &consumed, 10);
        if (consumed != text.size() || value <= 0 || value > std::numeric_limits<std::int32_t>::max()) {
          return 0;
        }
        return static_cast<std::int32_t>(value);
      } catch (const std::exception &) {
        return 0;
      }
    }

    /**
     * @brief Return one channel of an interleaved frame, mixing when the layouts differ.
     *
     * @param frame First sample of the source frame.
     * @param source_channels Channels in the source frame.
     * @param channel Destination channel index.
     * @param destination_channels Channels requested by the destination.
     * @return Channel sample.
     */
    float channel_sample(const float *frame, std::uint32_t source_channels, std::uint32_t channel, std::uint32_t destination_channels) {
      if (source_channels == 0) {
        return 0.0F;
      }
      if (destination_channels == 1) {
        float sum = 0.0F;
        for (std::uint32_t index = 0; index < source_channels; ++index) {
          sum += frame[index];
        }
        return sum / static_cast<float>(source_channels);
      }
      if (channel < source_channels) {
        return frame[channel];
      }
      return frame[0];
    }
  }  // namespace

  bool mode_recognized(const std::optional<std::string> &mode) {
    if (!mode || mode->empty()) {
      return true;
    }
    return equals_ignore_case(*mode, "display") || equals_ignore_case(*mode, "window") || equals_ignore_case(*mode, "application");
  }

  target_t target_from_config(const config_fields_t &fields) {
    target_t target;
    if (fields.mode && equals_ignore_case(*fields.mode, "window")) {
      target.mode = mode_e::window;
    } else if (fields.mode && equals_ignore_case(*fields.mode, "application")) {
      target.mode = mode_e::application;
    }

    target.window_id = parse_window_id(fields.window_id);
    if (fields.application_id) {
      target.application_id = *fields.application_id;
    }
    if (fields.window_title) {
      target.window_title = *fields.window_title;
    }
    target.show_cursor = parse_bool(fields.show_cursor);
    return target;
  }

  std::string_view mode_name(mode_e mode) {
    switch (mode) {
      case mode_e::window:
        return "window";
      case mode_e::application:
        return "application";
      case mode_e::display:
        return "display";
    }
    return "display";
  }

  bool implemented(mode_e mode) {
    if (mode == mode_e::display) {
      return true;
    }
#ifdef __APPLE__
    return mode == mode_e::window || mode == mode_e::application;
#else
    return false;
#endif
  }

  std::optional<match_t> match_target(const target_t &target, const std::vector<window_snapshot_t> &windows) {
    if (target.mode == mode_e::display) {
      return std::nullopt;
    }

    const auto requested_pid = pid_from_application_id(target.application_id);
    const auto bundle_id = requested_pid == 0 ? target.application_id : std::string {};
    const auto pid = requested_pid != 0 ? requested_pid : target.launched_pid;

    if (target.mode == mode_e::window && target.window_id) {
      const auto found = std::ranges::find_if(windows, [&](const window_snapshot_t &window) {
        return window.window_id == *target.window_id && window.width > 0 && window.height > 0;
      });
      if (found == windows.end()) {
        return std::nullopt;
      }

      return match_t {
        false,
        found->window_id,
        found->pid,
        found->bundle_id,
        found->x,
        found->y,
        found->width,
        found->height,
      };
    }

    const bool require_bundle = !bundle_id.empty();
    const bool require_pid = pid > 0 && bundle_id.empty();
    const bool require_title = target.mode == mode_e::window && !target.window_title.empty();
    if (target.mode == mode_e::window && !require_bundle && !require_pid && !require_title) {
      return std::nullopt;
    }
    if (target.mode == mode_e::application && !require_bundle && !require_pid) {
      return std::nullopt;
    }

    const window_snapshot_t *best = nullptr;
    for (const auto &window : windows) {
      if (window.width <= 0 || window.height <= 0) {
        continue;
      }
      if (require_bundle && window.bundle_id != bundle_id) {
        continue;
      }
      if (require_pid && window.pid != pid) {
        continue;
      }
      if (require_title && !title_matches(window.title, target.window_title)) {
        continue;
      }
      if (!require_bundle && !require_pid && !require_title) {
        continue;
      }

      const auto area = window.width * window.height;
      const auto best_area = best == nullptr ? 0 : best->width * best->height;
      if (best == nullptr || (window.on_screen && !best->on_screen) || (window.on_screen == best->on_screen && area > best_area)) {
        best = &window;
      }
    }

    if (best == nullptr) {
      return std::nullopt;
    }

    return match_t {
      target.mode == mode_e::application,
      best->window_id,
      best->pid,
      best->bundle_id,
      best->x,
      best->y,
      best->width,
      best->height,
    };
  }

  void activate(const target_t &target) {
    std::lock_guard lock {state_mutex};
    active_target = target;
  }

  void clear() {
    std::lock_guard lock {state_mutex};
    active_target = {};
    geometry.reset();
    focused_pid = 0;
    audio_is_open = false;
    audio_samples.clear();
    audio_available.notify_all();
  }

  target_t active() {
    std::lock_guard lock {state_mutex};
    return active_target;
  }

  void begin_stream_capture() {
    std::lock_guard lock {state_mutex};
    ++stream_depth;
  }

  void end_stream_capture() {
    std::lock_guard lock {state_mutex};
    if (stream_depth > 0) {
      --stream_depth;
    }
  }

  target_t stream_target() {
    std::lock_guard lock {state_mutex};
    if (stream_depth == 0) {
      return {};
    }
    return active_target;
  }

  void set_live_geometry(const geometry_t &value) {
    std::lock_guard lock {state_mutex};
    geometry = value;
  }

  std::optional<geometry_t> live_geometry() {
    std::lock_guard lock {state_mutex};
    return geometry;
  }

  void set_focus_pid(std::int32_t pid) {
    std::lock_guard lock {state_mutex};
    focused_pid = pid;
  }

  std::int32_t focus_pid() {
    std::lock_guard lock {state_mutex};
    return focused_pid;
  }

  void focus_active_target() {
    std::function<void()> focus;
    {
      std::lock_guard lock {state_mutex};
      if (focused_pid <= 0 || !focus_provider) {
        return;
      }
      focus = focus_provider;
    }
    focus();
  }

  void set_provider(
    std::function<std::vector<window_t>()> list_windows_fn,
    std::function<std::vector<application_t>()> list_applications_fn,
    std::function<void()> focus
  ) {
    std::lock_guard lock {state_mutex};
    windows_provider = std::move(list_windows_fn);
    applications_provider = std::move(list_applications_fn);
    focus_provider = std::move(focus);
  }

  std::vector<window_t> list_windows() {
    std::function<std::vector<window_t>()> provider;
    {
      std::lock_guard lock {state_mutex};
      provider = windows_provider;
    }
    if (!provider) {
      return {};
    }
    return provider();
  }

  std::vector<application_t> list_applications() {
    std::function<std::vector<application_t>()> provider;
    {
      std::lock_guard lock {state_mutex};
      provider = applications_provider;
    }
    if (!provider) {
      return {};
    }
    return provider();
  }

  void set_audio_request(const audio_spec_t &spec) {
    std::lock_guard lock {state_mutex};
    if (spec.sample_rate > 0 && spec.channels > 0) {
      requested_audio = spec;
    }
  }

  audio_spec_t audio_request() {
    std::lock_guard lock {state_mutex};
    return requested_audio;
  }

  void open_audio(const audio_spec_t &produced) {
    std::lock_guard lock {state_mutex};
    produced_audio = produced.sample_rate > 0 && produced.channels > 0 ? produced : audio_spec_t {};
    audio_samples.clear();
    audio_is_open = true;
    audio_available.notify_all();
  }

  void close_audio() {
    std::lock_guard lock {state_mutex};
    audio_is_open = false;
    audio_samples.clear();
    audio_available.notify_all();
  }

  void push_audio(const float *interleaved, std::size_t sample_count) {
    if (interleaved == nullptr || sample_count == 0) {
      return;
    }

    std::lock_guard lock {state_mutex};
    if (!audio_is_open) {
      return;
    }

    audio_samples.insert(audio_samples.end(), interleaved, interleaved + sample_count);
    const auto max_samples = static_cast<std::size_t>(produced_audio.sample_rate) * produced_audio.channels;
    if (max_samples > 0 && audio_samples.size() > max_samples) {
      audio_samples.erase(audio_samples.begin(), audio_samples.begin() + static_cast<std::ptrdiff_t>(audio_samples.size() - max_samples));
    }
    audio_available.notify_all();
  }

  void convert_interleaved(
    const float *source,
    std::size_t source_frames,
    std::uint32_t source_rate,
    std::uint32_t source_channels,
    float *destination,
    std::size_t destination_frames,
    std::uint32_t destination_rate,
    std::uint32_t destination_channels
  ) {
    if (destination == nullptr || destination_frames == 0 || destination_channels == 0) {
      return;
    }
    if (source == nullptr || source_frames == 0 || source_channels == 0 || source_rate == 0 || destination_rate == 0) {
      std::fill_n(destination, destination_frames * destination_channels, 0.0F);
      return;
    }

    // Frame counts are already chosen from the two sample rates. The interpolation below uses those counts.

    for (std::size_t frame = 0; frame < destination_frames; ++frame) {
      const double position = destination_frames == 1 || source_frames == 1 ?
                                0.0 :
                                static_cast<double>(frame) * static_cast<double>(source_frames - 1) / static_cast<double>(destination_frames - 1);
      const auto first = static_cast<std::size_t>(position);
      const auto second = std::min(first + 1, source_frames - 1);
      const auto blend = static_cast<float>(position - static_cast<double>(first));
      for (std::uint32_t channel = 0; channel < destination_channels; ++channel) {
        const auto *left = source + first * source_channels;
        const auto *right = source + second * source_channels;
        const auto start = channel_sample(left, source_channels, channel, destination_channels);
        const auto end = channel_sample(right, source_channels, channel, destination_channels);
        destination[frame * destination_channels + channel] = start + (end - start) * blend;
      }
    }
  }

  bool read_audio(float *destination, std::size_t destination_count, std::uint32_t sample_rate, std::uint32_t channels, std::chrono::milliseconds timeout) {
    if (destination == nullptr || destination_count == 0 || sample_rate == 0 || channels == 0 || destination_count % channels != 0) {
      return false;
    }

    const auto destination_frames = destination_count / channels;
    std::unique_lock lock {state_mutex};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
      if (!audio_is_open) {
        if (audio_available.wait_until(lock, deadline) == std::cv_status::timeout) {
          return false;
        }
        continue;
      }

      const auto source_frames = sample_rate == produced_audio.sample_rate ?
                                   destination_frames :
                                   std::max<std::size_t>(1, (destination_frames * produced_audio.sample_rate + sample_rate - 1) / sample_rate);
      const auto needed = source_frames * produced_audio.channels;
      if (audio_samples.size() >= needed) {
        std::vector<float> source(needed);
        std::copy_n(audio_samples.begin(), needed, source.begin());
        audio_samples.erase(audio_samples.begin(), audio_samples.begin() + static_cast<std::ptrdiff_t>(needed));
        const auto produced = produced_audio;
        lock.unlock();
        convert_interleaved(source.data(), source_frames, produced.sample_rate, produced.channels, destination, destination_frames, sample_rate, channels);
        return true;
      }

      if (audio_available.wait_until(lock, deadline) == std::cv_status::timeout) {
        return false;
      }
    }
  }
}  // namespace capture_source
