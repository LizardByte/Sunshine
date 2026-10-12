/**
 * @file src/audio.cpp
 * @brief Definitions for audio capture and encoding.
 */
// standard includes
#include <array>
#include <thread>

// lib includes
#include <opus/opus_multistream.h>

// local includes
#include "audio.h"
#include "config.h"
#include "globals.h"
#include "logging.h"
#include "platform/common.h"
#include "thread_safe.h"
#include "utility.h"

namespace audio {
  using namespace std::literals;
  /**
   * @brief Owning pointer for an Opus multistream encoder.
   */
  using opus_t = util::safe_ptr<OpusMSEncoder, opus_multistream_encoder_destroy>;
  /**
   * @brief Shared queue carrying captured PCM sample buffers to the encoder thread.
   */
  using sample_queue_t = std::shared_ptr<safe::queue_t<std::vector<float>>>;

  /**
   * @brief Initialize one lazy audio control attempt.
   * @param ctx Shared sink state to initialize.
   * @param settings Startup audio routing policy.
   * @param factory Backend creation operation.
   * @return Zero; a null context control records initialization failure.
   */
  static int start_audio_control(audio_ctx_t &ctx, const config::settings_t &settings, const std::function<std::unique_ptr<platf::audio_control_t>()> &factory);
  /**
   * @brief Restore the original sink after the final context reference releases.
   * @param ctx Shared sink state to release.
   */
  static void stop_audio_control(audio_ctx_t &ctx);
  /**
   * @brief Apply a client-provided surround channel layout.
   * @param stream Encoder layout to update.
   * @param params Negotiated surround parameters.
   */
  static void apply_surround_params(opus_stream_config_t &stream, const stream_params_t &params);

  /**
   * @brief Configuration and lazy control resources belonging to one application.
   */
  struct service_t::impl_t {
    const config::settings_t &settings;  ///< Stable startup configuration.
    safe::mail_t host_mail;  ///< Host packet mailbox.
    std::function<std::unique_ptr<platf::audio_control_t>()> factory;  ///< Backend creation operation for each lazy initialization.
    safe::shared_t<audio_ctx_t> control_shared;  ///< Lazy sink context shared by this owner's consumers.

    /**
     * @brief Attach startup settings before audio control can be initialized.
     * @param settings Host configuration.
     * @param host_mail Host packet mailbox.
     * @param factory Backend creation operation retained for initialization retries.
     */
    impl_t(const config::settings_t &settings, safe::mail_t host_mail, std::function<std::unique_ptr<platf::audio_control_t>()> factory):
        settings {settings},
        host_mail {std::move(host_mail)},
        factory {std::move(factory)},
        control_shared {[this](audio_ctx_t &context) {
                          return start_audio_control(context, this->settings, this->factory);
                        },
                        stop_audio_control} {}
  };

  service_t::service_t(const config::settings_t &settings, safe::mail_t host_mail):
      service_t {settings, std::move(host_mail), [&settings]() {
                   return platf::audio_control(settings);
                 }} {}

  service_t::service_t(const config::settings_t &settings, safe::mail_t host_mail, std::function<std::unique_ptr<platf::audio_control_t>()> factory):
      impl {std::make_shared<impl_t>(settings, std::move(host_mail), std::move(factory))} {}

  service_t::~service_t() = default;

  /**
   * @brief Select the Opus stream configuration for a channel count and quality tier.
   *
   * @param channels Number of audio channels in the stream.
   * @param quality Whether the high-quality Opus layout should be selected.
   * @return Index into `stream_configs` for the requested layout.
   */
  int map_stream(int channels, bool quality);

  constexpr auto SAMPLE_RATE = 48000;  ///< Audio sample rate in hertz required by Opus.

  // NOTE: If you adjust the bitrates listed here, make sure to update the
  // corresponding bitrate adjustment logic in rtsp_stream::cmd_announce()
  /**
   * @brief Opus stream layouts and bitrates advertised to clients.
   */
  const std::array<opus_stream_config_t, MAX_STREAM_CONFIG> stream_configs {{
    {
      SAMPLE_RATE,
      2,
      1,
      1,
      platf::speaker::map_stereo.data(),
      96000,
    },
    {
      SAMPLE_RATE,
      2,
      1,
      1,
      platf::speaker::map_stereo.data(),
      512000,
    },
    {
      SAMPLE_RATE,
      6,
      4,
      2,
      platf::speaker::map_surround51.data(),
      256000,
    },
    {
      SAMPLE_RATE,
      6,
      6,
      0,
      platf::speaker::map_surround51.data(),
      1536000,
    },
    {
      SAMPLE_RATE,
      8,
      5,
      3,
      platf::speaker::map_surround71.data(),
      450000,
    },
    {
      SAMPLE_RATE,
      8,
      8,
      0,
      platf::speaker::map_surround71.data(),
      2048000,
    },
  }};

  /**
   * @brief Encode captured PCM samples into Opus packets on the audio worker thread.
   *
   * @param host_mail Host mailbox receiving encoded audio packets.
   * @param samples Queue of captured PCM sample buffers to encode.
   * @param config Audio stream settings negotiated with the client.
   * @param channel_data Borrowed transport session that must outlive encoding and consumption of its queued packets.
   */
  void encodeThread(safe::mail_t host_mail, sample_queue_t samples, config_t config, stream::session_t *channel_data) {
    auto packets = host_mail->queue<packet_t>(mail::audio_packets);
    auto stream = stream_configs[map_stream(config.channels, config.flags[config_t::HIGH_QUALITY])];
    if (config.flags[config_t::CUSTOM_SURROUND_PARAMS]) {
      apply_surround_params(stream, config.customStreamParams);
    }

    // Encoding takes place on this thread
    platf::set_thread_name("audio::encode");
    platf::adjust_thread_priority(platf::thread_priority_e::high);

    opus_t opus {opus_multistream_encoder_create(
      stream.sampleRate,
      stream.channelCount,
      stream.streams,
      stream.coupledStreams,
      stream.mapping,
      OPUS_APPLICATION_RESTRICTED_LOWDELAY,
      nullptr
    )};

    opus_multistream_encoder_ctl(opus.get(), OPUS_SET_BITRATE(stream.bitrate));
    opus_multistream_encoder_ctl(opus.get(), OPUS_SET_VBR(0));

    logging::info("Opus initialized: {} kHz, {} channels, {} kbps (total), LOWDELAY", stream.sampleRate / 1000, stream.channelCount, stream.bitrate / 1000);

    auto frame_size = config.packetDuration * stream.sampleRate / 1000;
    while (auto sample = samples->pop()) {
      buffer_t packet {1400};

      int bytes = opus_multistream_encode_float(opus.get(), sample->data(), frame_size, std::begin(packet), (opus_int32) packet.size());
      if (bytes < 0) {
        logging::error([&](std::ostream &out) {
          out << "Couldn't encode audio: "sv << opus_strerror(bytes);
        });
        packets->stop();

        return;
      }

      packet.fake_resize(bytes);
      packets->raise(channel_data, std::move(packet));
    }
  }

  /**
   * @brief Capture and publish audio until the session shuts down, joining its encoder before returning.
   * @note Only the first session changes a differing sink, and a failed sink change stops capture before creating a microphone.
   */
  void service_t::capture(safe::mail_t mail, config_t config, stream::session_t *channel_data) {
    const auto host_mail = impl->host_mail;
    const auto &settings = impl->settings;
    auto shutdown_event = mail->event<bool>(mail::shutdown);
    if (!settings.audio.stream) {
      shutdown_event->view();
      return;
    }
    auto stream = stream_configs[map_stream(config.channels, config.flags[config_t::HIGH_QUALITY])];
    if (config.flags[config_t::CUSTOM_SURROUND_PARAMS]) {
      apply_surround_params(stream, config.customStreamParams);
    }

    auto ref = context();
    if (!ref) {
      return;
    }

    auto init_failure_fg = util::fail_guard([&shutdown_event]() {
      logging::error("Unable to initialize audio capture. The stream will not have audio."sv);

      // Wait for shutdown to be signalled if we fail init.
      // This allows streaming to continue without audio.
      shutdown_event->view();
    });

    auto &control = ref->control;
    if (!control) {
      return;
    }

    const auto &sink = select_capture_sink(ref->sink, settings.audio.sink, config.flags[config_t::HOST_AUDIO], stream.channelCount);

    // Only the first to start a session may change the default sink
    if (!ref->sink_flag->exchange(true, std::memory_order_acquire)) {
      // If the selected sink is different than the current one, change sinks.
      ref->restore_sink = ref->sink.host != sink;
      if (ref->restore_sink && control->set_sink(sink)) {
        return;
      }
    }

    auto frame_size = config.packetDuration * stream.sampleRate / 1000;
    bool host_audio = config.flags[config_t::HOST_AUDIO];
    bool continuous_audio = config.flags[config_t::CONTINUOUS_AUDIO];
    auto mic = control->microphone(stream.mapping, stream.channelCount, stream.sampleRate, frame_size, continuous_audio, host_audio);
    if (!mic) {
      return;
    }

    // Audio is initialized, so we don't want to print the failure message
    init_failure_fg.disable();

    // Capture takes place on this thread
    platf::adjust_thread_priority(platf::thread_priority_e::critical);

    auto samples = std::make_shared<sample_queue_t::element_type>(30);
    std::jthread thread {encodeThread, host_mail, samples, config, channel_data};

    auto fg = util::fail_guard([&]() {
      samples->stop();
      thread.join();

      shutdown_event->view();
    });

    int samples_per_frame = frame_size * stream.channelCount;

    while (!shutdown_event->peek()) {
      std::vector<float> sample_buffer;
      sample_buffer.resize(samples_per_frame);

      auto status = mic->sample(sample_buffer);
      switch (status) {
        case platf::capture_e::ok:
          break;
        case platf::capture_e::timeout:
          continue;
        case platf::capture_e::reinit:
          logging::info("Reinitializing audio capture"sv);
          mic.reset();
          do {
            mic = control->microphone(stream.mapping, stream.channelCount, stream.sampleRate, frame_size, continuous_audio, host_audio);
            if (!mic) {
              logging::warning("Couldn't re-initialize audio input"sv);
            }
          } while (!mic && !shutdown_event->view(5s));
          continue;
        default:
          return;
      }

      samples->raise(std::move(sample_buffer));
    }
  }

  audio_ctx_ref_t service_t::context() {
    auto ref = impl->control_shared.ref();
    if (!ref) {
      return nullptr;
    }

    /** @brief Keep the shared wrapper alive until its last control reference is released. */
    struct retained_t {
      std::shared_ptr<impl_t> owner;  ///< Released after the lazy context reference.
      safe::shared_t<audio_ctx_t>::ptr_t context;  ///< Triggers final sink restoration before owner destruction.
    };

    auto retained = std::make_shared<retained_t>(impl, std::move(ref));
    return audio_ctx_ref_t {retained, retained->context.get()};
  }

  bool is_audio_ctx_sink_available(const audio_ctx_t &ctx) {
    if (!ctx.control) {
      return false;
    }

    const std::string &sink = ctx.sink.host.empty() ? ctx.fallback_sink : ctx.sink.host;
    if (sink.empty()) {
      return false;
    }

    return ctx.control->is_sink_available(sink);
  }

  const std::string &select_capture_sink(const platf::sink_t &sinks, const std::string &configured_sink, bool host_audio, int channel_count) {
    const auto &selected = configured_sink.empty() ? sinks.host : configured_sink;
    if (!sinks.null || (host_audio && !selected.empty())) {
      return selected;
    }

    switch (channel_count) {
      case 2:
        return sinks.null->stereo;
      case 6:
        return sinks.null->surround51;
      case 8:
        return sinks.null->surround71;
      default:
        return selected;
    }
  }

  /**
   * @brief Select the Opus stream configuration for a channel count and quality tier.
   */
  int map_stream(int channels, bool quality) {
    int shift = quality ? 1 : 0;
    switch (channels) {
      case 2:
        return STEREO + shift;
      case 6:
        return SURROUND51 + shift;
      case 8:
        return SURROUND71 + shift;
    }
    return STEREO;
  }

  int start_audio_control(audio_ctx_t &ctx, const config::settings_t &settings, const std::function<std::unique_ptr<platf::audio_control_t>()> &factory) {
    ctx.fallback_sink = settings.audio.sink;
    auto fg = util::fail_guard([]() {
      logging::warning("There will be no audio"sv);
    });

    ctx.sink_flag = std::make_unique<std::atomic_bool>(false);

    // The default sink has not been replaced yet.
    ctx.restore_sink = false;

    if (!(ctx.control = factory())) {
      return 0;
    }

    auto sink = ctx.control->sink_info();
    if (!sink) {
      // Let the calling code know it failed
      ctx.control.reset();
      return 0;
    }

    ctx.sink = std::move(*sink);

    fg.disable();
    return 0;
  }

  void stop_audio_control(audio_ctx_t &ctx) {
    // restore audio-sink if applicable
    if (!ctx.restore_sink) {
      return;
    }

    // Change back to the host sink, unless there was none
    const std::string &sink = ctx.sink.host.empty() ? ctx.fallback_sink : ctx.sink.host;
    if (!sink.empty()) {
      // Best effort, it's allowed to fail
      ctx.control->set_sink(sink);
    }
  }

  void apply_surround_params(opus_stream_config_t &stream, const stream_params_t &params) {
    stream.channelCount = params.channelCount;
    stream.streams = params.streams;
    stream.coupledStreams = params.coupledStreams;
    stream.mapping = params.mapping.data();
  }
}  // namespace audio
