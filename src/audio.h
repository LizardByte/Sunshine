/**
 * @file src/audio.h
 * @brief Declarations for audio capture and encoding.
 */
#pragma once

// local includes
#include "platform/common.h"
#include "thread_safe.h"
#include "utility.h"

#include <array>
#include <bitset>

namespace stream {
  struct session_t;
}

namespace audio {
  /**
   * @brief Supported Opus channel layouts advertised to Moonlight clients.
   */
  enum stream_config_e : int {
    STEREO,  ///< Stereo
    HIGH_STEREO,  ///< High stereo
    SURROUND51,  ///< Surround 5.1
    HIGH_SURROUND51,  ///< High surround 5.1
    SURROUND71,  ///< Surround 7.1
    HIGH_SURROUND71,  ///< High surround 7.1
    MAX_STREAM_CONFIG  ///< Maximum audio stream configuration
  };

  /**
   * @brief Static Opus encoder layout for one advertised audio mode.
   */
  struct opus_stream_config_t {
    std::int32_t sampleRate;  ///< Opus sample rate in hertz.
    int channelCount;  ///< Number of audio channels in the Opus layout.
    int streams;  ///< Number of Opus streams in the layout.
    int coupledStreams;  ///< Number of stereo-coupled Opus streams.
    const std::uint8_t *mapping;  ///< Channel mapping table passed to the Opus encoder.
    int bitrate;  ///< Target bitrate in bits per second.
  };

  /**
   * @brief Custom Opus channel layout supplied by configuration.
   */
  struct stream_params_t {
    int channelCount;  ///< Number of audio channels in the Opus layout.
    int streams;  ///< Number of Opus streams in the custom layout.
    int coupledStreams;  ///< Number of stereo-coupled Opus streams.
    std::array<std::uint8_t, 8> mapping;  ///< Channel mapping table for up to eight speakers.
  };

  /**
   * @brief Opus layouts and bitrates advertised for each supported stream mode.
   */
  extern const std::array<opus_stream_config_t, MAX_STREAM_CONFIG> stream_configs;

  /**
   * @brief Audio capture and encoder settings for a stream.
   */
  struct config_t {
    /**
     * @brief Boolean audio feature flags.
     */
    enum flags_e : int {
      HIGH_QUALITY,  ///< High quality audio
      HOST_AUDIO,  ///< Host audio
      CUSTOM_SURROUND_PARAMS,  ///< Custom surround parameters
      CONTINUOUS_AUDIO,  ///< Continuous audio
      MAX_FLAGS  ///< Maximum number of flags
    };

    int packetDuration;  ///< Packet duration in milliseconds requested by the client.
    int channels;  ///< Number of audio channels requested by the client.
    int mask;  ///< Speaker mask describing the requested channel layout.

    stream_params_t customStreamParams;  ///< Custom Opus layout used when CUSTOM_SURROUND_PARAMS is enabled.

    std::bitset<MAX_FLAGS> flags;  ///< Enabled audio feature flags.
  };

  /**
   * @brief Shared audio device state used while streams are active.
   */
  struct audio_ctx_t {
    // We want to change the sink for the first stream only
    std::unique_ptr<std::atomic_bool> sink_flag;  ///< Tracks whether the capture sink was already switched.

    std::unique_ptr<platf::audio_control_t> control;  ///< Platform audio-control implementation.

    bool restore_sink;  ///< Whether Sunshine should restore the original sink when capture ends.
    std::string fallback_sink;  ///< Configured sink captured for this context lifetime.
    platf::sink_t sink;  ///< Original sink captured before Sunshine switched devices.
  };

  /**
   * @brief Byte buffer used for encoded audio packet payloads.
   */
  using buffer_t = util::buffer_t<std::uint8_t>;
  /**
   * @brief Encoded audio packet paired with a borrowed transport session that must outlive packet consumption.
   */
  using packet_t = std::pair<stream::session_t *, buffer_t>;
  /**
   * @brief Shared reference retaining one audio owner and its lazy context.
   */
  using audio_ctx_ref_t = std::shared_ptr<audio_ctx_t>;

  /**
   * @brief Own the audio control context shared by captures and display restoration.
   */
  class service_t {
  public:
    /**
     * @brief Bind host configuration and packet delivery without opening audio devices.
     * @param settings Application startup settings that outlive this owner and retained contexts.
     * @param host_mail Host mailbox receiving encoded packets.
     */
    service_t(const config::settings_t &settings, safe::mail_t host_mail);
    /**
     * @brief Bind an explicit audio backend factory for controlled initialization.
     * @param settings Host audio configuration that outlives retained context references.
     * @param host_mail Host packet mailbox.
     * @param factory Factory invoked when the first context reference is acquired.
     */
    service_t(const config::settings_t &settings, safe::mail_t host_mail, std::function<std::unique_ptr<platf::audio_control_t>()> factory);
    /** @brief Release audio resources after retained context references finish. */
    ~service_t();
    service_t(const service_t &) = delete;
    service_t &operator=(const service_t &) = delete;
    /**
     * @brief Capture and publish audio for one transport session.
     * @param mail Session shutdown mailbox.
     * @param config Negotiated audio layout and encoding settings.
     * @param channel_data Borrowed transport session that must outlive capture and consumption of its queued packets.
     */
    void capture(safe::mail_t mail, config_t config, stream::session_t *channel_data);
    /**
     * @brief Retain the shared audio control and original sink until it can be restored.
     * @return Retained context; its control is null when backend initialization failed.
     */
    audio_ctx_ref_t context();

  private:
    struct impl_t;  ///< Lazy audio control and its explicit dependencies.
    std::shared_ptr<impl_t> impl;  ///< Retained by active captures and context references.
  };

  /**
   * @brief Check if the audio sink held by audio context is available.
   * @returns True if available (and can probably be restored), false otherwise.
   * @note Useful for delaying the release of audio context shared pointer (which
   *       tries to restore original sink).
   *
   * @examples
   * audio_ctx_ref_t audio = audios.context()
   * if (audio.get()) {
   *     return is_audio_ctx_sink_available(*audio.get());
   * }
   * return false;
   * @examples_end
   * @param ctx Native context object used by the operation or callback.
   */
  bool is_audio_ctx_sink_available(const audio_ctx_t &ctx);

  /**
   * @brief Select the capture sink for the requested channel layout and host playback policy.
   *
   * @param sinks Host and virtual sinks discovered by the audio backend.
   * @param configured_sink Explicit capture sink, or an empty string to use the host sink.
   * @param host_audio Whether the host should continue playing the captured audio.
   * @param channel_count Negotiated number of channels used to select a virtual sink.
   * @return Borrowed sink name whose discovered or configured owner must outlive its use.
   */
  const std::string &select_capture_sink(const platf::sink_t &sinks, const std::string &configured_sink, bool host_audio, int channel_count);
}  // namespace audio
