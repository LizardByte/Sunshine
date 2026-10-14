/**
 * @file src/audio.cpp
 * @brief Definitions for audio capture and encoding.
 */
// standard includes
#include <algorithm>
#include <array>
#include <bit>
#include <filesystem>
#include <format>
#include <mutex>
#include <thread>

// Optional runtime-loaded FFmpeg Dolby audio backend. Headers are a build-time dependency only;
// no FFmpeg import library is linked and no FFmpeg DLL is bundled with Sunshine.
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <Windows.h>
  #include <libavcodec/avcodec.h>
  #include <libavutil/avutil.h>
  #include <libavutil/channel_layout.h>
  #include <libavutil/frame.h>
  #include <libavutil/version.h>
#endif

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

  static int start_audio_control(audio_ctx_t &ctx);
  static void stop_audio_control(audio_ctx_t &);
  static void apply_surround_params(opus_stream_config_t &stream, const stream_params_t &params);

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

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  template<typename Function>
  bool load_ffmpeg_symbol(HMODULE dll_module, Function &function, const char *name) {
    const auto symbol = GetProcAddress(dll_module, name);
    static_assert(sizeof(Function) == sizeof(FARPROC));
    function = std::bit_cast<Function>(symbol);
    return function != nullptr;
  }

  /**
   * @brief Runtime-loaded FFmpeg API used by the optional AC-3 and E-AC-3 encoders.
   *
   * The runtime DLL major versions must match the headers used for this build. Keeping all
   * calls behind function pointers means Sunshine still starts normally when the DLLs are absent.
   */
  class ffmpeg_audio_api_t {
  public:
    ~ffmpeg_audio_api_t() {
      if (avcodec_module_) {
        FreeLibrary(avcodec_module_);
      }
      if (avutil_module_) {
        FreeLibrary(avutil_module_);
      }
    }

    bool libraries_available() {
      std::call_once(load_once_, [this]() {
        load();
      });
      return ready_;
    }

    bool encoder_available(AVCodecID codec_id) {
      return libraries_available() && find_encoder(codec_id) != nullptr;
    }

    decltype(&avutil_version) util_version {};
    decltype(&avcodec_version) codec_version {};
    decltype(&av_channel_layout_from_mask) channel_layout_from_mask {};
    decltype(&av_channel_layout_copy) channel_layout_copy {};
    decltype(&avcodec_find_encoder) find_encoder {};
    decltype(&avcodec_alloc_context3) alloc_context {};
    decltype(&avcodec_open2) open_encoder {};
    decltype(&avcodec_free_context) free_context {};
    decltype(&avcodec_send_frame) send_frame {};
    decltype(&avcodec_receive_packet) receive_packet {};
    decltype(&av_frame_alloc) alloc_frame {};
    decltype(&av_frame_free) free_frame {};
    decltype(&av_frame_get_buffer) get_frame_buffer {};
    decltype(&av_frame_make_writable) make_frame_writable {};
    decltype(&av_packet_alloc) alloc_packet {};
    decltype(&av_packet_free) free_packet {};
    decltype(&av_packet_unref) unref_packet {};

  private:
    void clear_symbols() {
      util_version = nullptr;
      codec_version = nullptr;
      channel_layout_from_mask = nullptr;
      channel_layout_copy = nullptr;
      find_encoder = nullptr;
      alloc_context = nullptr;
      open_encoder = nullptr;
      free_context = nullptr;
      send_frame = nullptr;
      receive_packet = nullptr;
      alloc_frame = nullptr;
      free_frame = nullptr;
      get_frame_buffer = nullptr;
      make_frame_writable = nullptr;
      alloc_packet = nullptr;
      free_packet = nullptr;
      unref_packet = nullptr;
    }

    bool load_directory(const std::filesystem::path &directory) {
      const auto avutil_path = directory / std::format(L"avutil-{}.dll", LIBAVUTIL_VERSION_MAJOR);
      const auto avcodec_path = directory / std::format(L"avcodec-{}.dll", LIBAVCODEC_VERSION_MAJOR);

      auto avutil_module = LoadLibraryExW(avutil_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      if (!avutil_module) {
        return false;
      }

      auto avcodec_module = LoadLibraryExW(avcodec_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      if (!avcodec_module) {
        FreeLibrary(avutil_module);
        return false;
      }

      const auto symbols_loaded =
        load_ffmpeg_symbol(avutil_module, util_version, "avutil_version") &&
        load_ffmpeg_symbol(avutil_module, channel_layout_from_mask, "av_channel_layout_from_mask") &&
        load_ffmpeg_symbol(avutil_module, channel_layout_copy, "av_channel_layout_copy") &&
        load_ffmpeg_symbol(avutil_module, alloc_frame, "av_frame_alloc") &&
        load_ffmpeg_symbol(avutil_module, free_frame, "av_frame_free") &&
        load_ffmpeg_symbol(avutil_module, get_frame_buffer, "av_frame_get_buffer") &&
        load_ffmpeg_symbol(avutil_module, make_frame_writable, "av_frame_make_writable") &&
        load_ffmpeg_symbol(avcodec_module, alloc_packet, "av_packet_alloc") &&
        load_ffmpeg_symbol(avcodec_module, free_packet, "av_packet_free") &&
        load_ffmpeg_symbol(avcodec_module, unref_packet, "av_packet_unref") &&
        load_ffmpeg_symbol(avcodec_module, codec_version, "avcodec_version") &&
        load_ffmpeg_symbol(avcodec_module, find_encoder, "avcodec_find_encoder") &&
        load_ffmpeg_symbol(avcodec_module, alloc_context, "avcodec_alloc_context3") &&
        load_ffmpeg_symbol(avcodec_module, open_encoder, "avcodec_open2") &&
        load_ffmpeg_symbol(avcodec_module, free_context, "avcodec_free_context") &&
        load_ffmpeg_symbol(avcodec_module, send_frame, "avcodec_send_frame") &&
        load_ffmpeg_symbol(avcodec_module, receive_packet, "avcodec_receive_packet");

      if (const auto versions_match = symbols_loaded &&
          (util_version() >> 16) == LIBAVUTIL_VERSION_MAJOR &&
          (codec_version() >> 16) == LIBAVCODEC_VERSION_MAJOR &&
          (find_encoder(AV_CODEC_ID_AC3) != nullptr || find_encoder(AV_CODEC_ID_EAC3) != nullptr);
          !versions_match) {
        FreeLibrary(avcodec_module);
        FreeLibrary(avutil_module);
        clear_symbols();
        return false;
      }

      avutil_module_ = avutil_module;
      avcodec_module_ = avcodec_module;
      return true;
    }

    void load() {
      std::wstring executable_path(32768, L'\0');
      const auto length = GetModuleFileNameW(nullptr, executable_path.data(), static_cast<DWORD>(executable_path.size()));
      if (length == 0 || length >= executable_path.size()) {
        return;
      }
      executable_path.resize(length);

      const auto executable_directory = std::filesystem::path(executable_path).parent_path();
      const std::array directories {
        executable_directory,
        executable_directory / L"ffmpeg",
      };

      for (const auto &directory : directories) {
        if (load_directory(directory)) {
          ready_ = true;
          BOOST_LOG(info) << "Optional FFmpeg Dolby audio encoder API is available"sv;
          return;
        }
      }

      clear_symbols();
      BOOST_LOG(debug) << "Compatible FFmpeg Dolby audio encoders not found; continuing with Opus"sv;
    }

    std::once_flag load_once_;
    HMODULE avutil_module_ {nullptr};
    HMODULE avcodec_module_ {nullptr};
    bool ready_ {false};
  };

  ffmpeg_audio_api_t &ffmpeg_audio_api() {
    static ffmpeg_audio_api_t api;
    return api;
  }

  /**
   * @brief Encode one interleaved stereo or 5.1 float PCM frame as an AC-3-family access unit.
   */
  class ffmpeg_audio_encoder_t {
  public:
    ~ffmpeg_audio_encoder_t() {
      if (packet_) {
        api_.free_packet(&packet_);
      }
      if (frame_) {
        api_.free_frame(&frame_);
      }
      if (context_) {
        api_.free_context(&context_);
      }
    }

    bool initialize(AVCodecID codec_id, std::int64_t bit_rate, int channels) {
      if (!api_.libraries_available() || (channels != 2 && channels != 6)) {
        return false;
      }

      const auto *codec = api_.find_encoder(codec_id);
      if (!codec) {
        return false;
      }

      context_ = api_.alloc_context(codec);
      if (!context_) {
        return false;
      }

      context_->bit_rate = bit_rate;
      context_->sample_rate = SAMPLE_RATE;
      context_->sample_fmt = AV_SAMPLE_FMT_FLTP;
      context_->time_base = AVRational {1, SAMPLE_RATE};
      context_->thread_count = 1;
      channels_ = channels;

      const auto channel_layout = channels == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_5POINT1_BACK;
      if (api_.channel_layout_from_mask(&context_->ch_layout, channel_layout) < 0 ||
          api_.open_encoder(context_, codec, nullptr) < 0) {
        return false;
      }

      // AC-3 and E-AC-3 use 1536 samples per frame here (32 ms at 48 kHz).
      if (context_->frame_size != 1536) {
        return false;
      }

      frame_ = api_.alloc_frame();
      if (!frame_) {
        return false;
      }

      frame_->format = context_->sample_fmt;
      frame_->sample_rate = context_->sample_rate;
      frame_->nb_samples = context_->frame_size;
      if (api_.channel_layout_copy(&frame_->ch_layout, &context_->ch_layout) < 0 ||
          api_.get_frame_buffer(frame_, 0) < 0) {
        return false;
      }

      packet_ = api_.alloc_packet();
      return packet_ != nullptr;
    }

    int encode(const std::vector<float> &interleaved, buffer_t &output) {
      if (!frame_ || interleaved.size() != static_cast<std::size_t>(frame_->nb_samples * channels_)) {
        return -1;
      }

      if (api_.make_frame_writable(frame_) < 0) {
        return -1;
      }

      for (int channel = 0; channel < channels_; ++channel) {
        auto *destination = static_cast<float *>(static_cast<void *>(frame_->extended_data[channel]));
        for (int sample = 0; sample < frame_->nb_samples; ++sample) {
          destination[sample] = interleaved[static_cast<std::size_t>(sample * channels_ + channel)];
        }
      }

      frame_->pts = next_pts_;
      next_pts_ += frame_->nb_samples;

      if (api_.send_frame(context_, frame_) < 0 || api_.receive_packet(context_, packet_) < 0) {
        api_.unref_packet(packet_);
        return -1;
      }

      const auto packet_size = packet_->size;
      if (packet_size <= 0 || static_cast<std::size_t>(packet_size) > output.size()) {
        api_.unref_packet(packet_);
        return -1;
      }

      std::copy_n(packet_->data, packet_size, std::begin(output));
      api_.unref_packet(packet_);
      return packet_size;
    }

  private:
    ffmpeg_audio_api_t &api_ {ffmpeg_audio_api()};
    AVCodecContext *context_ {nullptr};
    AVFrame *frame_ {nullptr};
    AVPacket *packet_ {nullptr};
    std::int64_t next_pts_ {0};
    int channels_ {0};
  };
#endif

  /**
   * @brief Encode captured PCM samples into Opus packets on the audio worker thread.
   *
   * @param samples Queue of captured PCM sample buffers to encode.
   * @param config Audio stream settings negotiated with the client.
   * @param channel_data Stream session carried with each encoded packet.
   */
  void encodeThread(sample_queue_t samples, config_t config, stream::session_t *channel_data) {
    auto packets = mail::man->queue<packet_t>(mail::audio_packets);
    auto stream = stream_configs[map_stream(config.channels, config.flags[config_t::HIGH_QUALITY])];
    if (config.flags[config_t::CUSTOM_SURROUND_PARAMS]) {
      apply_surround_params(stream, config.customStreamParams);
    }

    // Encoding takes place on this thread
    platf::set_thread_name("audio::encode");
    platf::adjust_thread_priority(platf::thread_priority_e::high);

    if (config.use_ac3 || config.use_eac3) {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
      const auto codec_id = config.use_eac3 ? AV_CODEC_ID_EAC3 : AV_CODEC_ID_AC3;
      const auto codec_name = config.use_eac3 ? "E-AC-3"sv : "AC-3"sv;
      const auto bitrate_kbps = dolby_bitrate_kbps(config.use_eac3, config.high_quality_dolby, config.channels);
      ffmpeg_audio_encoder_t encoder;
      if (bitrate_kbps <= 0 || !encoder.initialize(codec_id, static_cast<std::int64_t>(bitrate_kbps) * 1000, config.channels)) {
        BOOST_LOG(error) << "Couldn't initialize the optional FFmpeg "sv << codec_name << " encoder"sv;
        packets->stop();
        return;
      }

      BOOST_LOG(info) << codec_name << " initialized: "sv << config.channels
                      << " channels, 48 kHz, "sv << bitrate_kbps << " kbit/s"sv;
      while (auto sample = samples->pop()) {
        // High-quality 448 kbit/s frames can exceed the old 1400-byte buffer.
        buffer_t packet {2048};
        const auto bytes = encoder.encode(*sample, packet);
        if (bytes < 0) {
          BOOST_LOG(error) << "Couldn't encode "sv << codec_name << " audio frame"sv;
          packets->stop();
          return;
        }

        packet.fake_resize(bytes);
        packets->raise(channel_data, std::move(packet));
      }
      return;
#else
      BOOST_LOG(error) << "Dolby audio was requested, but this build does not support its optional FFmpeg backend"sv;
      packets->stop();
      return;
#endif
    }

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

    BOOST_LOG(info) << "Opus initialized: "sv << stream.sampleRate / 1000 << " kHz, "sv
                    << stream.channelCount << " channels, "sv
                    << stream.bitrate / 1000 << " kbps (total), LOWDELAY"sv;

    auto frame_size = config.packetDuration * stream.sampleRate / 1000;
    while (auto sample = samples->pop()) {
      buffer_t packet {1400};

      int bytes = opus_multistream_encode_float(opus.get(), sample->data(), frame_size, std::begin(packet), (opus_int32) packet.size());
      if (bytes < 0) {
        BOOST_LOG(error) << "Couldn't encode audio: "sv << opus_strerror(bytes);
        packets->stop();

        return;
      }

      packet.fake_resize(bytes);
      packets->raise(channel_data, std::move(packet));
    }
  }

  /**
   * @brief Run the capture loop for this backend.
   */
  void capture(safe::mail_t mail, config_t config, stream::session_t *channel_data) {
    auto shutdown_event = mail->event<bool>(mail::shutdown);
    if (!config::audio.stream) {
      shutdown_event->view();
      return;
    }
    auto stream = stream_configs[map_stream(config.channels, config.flags[config_t::HIGH_QUALITY])];
    if (config.flags[config_t::CUSTOM_SURROUND_PARAMS]) {
      apply_surround_params(stream, config.customStreamParams);
    }

    auto ref = get_audio_ctx_ref();
    if (!ref) {
      return;
    }

    auto init_failure_fg = util::fail_guard([&shutdown_event]() {
      BOOST_LOG(error) << "Unable to initialize audio capture. The stream will not have audio."sv;

      // Wait for shutdown to be signalled if we fail init.
      // This allows streaming to continue without audio.
      shutdown_event->view();
    });

    auto &control = ref->control;
    if (!control) {
      return;
    }

    // Order of priority:
    // 1. Virtual sink
    // 2. Audio sink
    // 3. Host
    std::string *sink = &ref->sink.host;
    if (!config::audio.sink.empty()) {
      sink = &config::audio.sink;
    }

    // Prefer the virtual sink if host playback is disabled or there's no other sink
    if (ref->sink.null && (!config.flags[config_t::HOST_AUDIO] || sink->empty())) {
      auto &null = *ref->sink.null;
      switch (stream.channelCount) {
        case 2:
          sink = &null.stereo;
          break;
        case 6:
          sink = &null.surround51;
          break;
        case 8:
          sink = &null.surround71;
          break;
      }
    }

    // Only the first to start a session may change the default sink
    if (!ref->sink_flag->exchange(true, std::memory_order_acquire)) {
      // If the selected sink is different than the current one, change sinks.
      ref->restore_sink = ref->sink.host != *sink;
      if (ref->restore_sink) {
        if (control->set_sink(*sink)) {
          return;
        }
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
    std::jthread thread {encodeThread, samples, config, channel_data};

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
          BOOST_LOG(info) << "Reinitializing audio capture"sv;
          mic.reset();
          do {
            mic = control->microphone(stream.mapping, stream.channelCount, stream.sampleRate, frame_size, continuous_audio, host_audio);
            if (!mic) {
              BOOST_LOG(warning) << "Couldn't re-initialize audio input"sv;
            }
          } while (!mic && !shutdown_event->view(5s));
          continue;
        default:
          return;
      }

      samples->raise(std::move(sample_buffer));
    }
  }

  audio_ctx_ref_t get_audio_ctx_ref() {
    static auto control_shared {safe::make_shared<audio_ctx_t>(start_audio_control, stop_audio_control)};
    return control_shared.ref();
  }

  bool ac3_available() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return ffmpeg_audio_api().encoder_available(AV_CODEC_ID_AC3);
#else
    return false;
#endif
  }

  bool eac3_available() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return ffmpeg_audio_api().encoder_available(AV_CODEC_ID_EAC3);
#else
    return false;
#endif
  }

  int dolby_bitrate_kbps(bool eac3, bool high_quality, int channels) {
    if (channels == 2) {
      return high_quality ? 192 : 96;
    }
    if (channels == 6) {
      if (!high_quality) {
        return 256;
      }
      return eac3 ? 384 : 448;
    }
    return 0;
  }

  bool is_audio_ctx_sink_available(const audio_ctx_t &ctx) {
    if (!ctx.control) {
      return false;
    }

    const std::string &sink = ctx.sink.host.empty() ? config::audio.sink : ctx.sink.host;
    if (sink.empty()) {
      return false;
    }

    return ctx.control->is_sink_available(sink);
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

  int start_audio_control(audio_ctx_t &ctx) {
    auto fg = util::fail_guard([]() {
      BOOST_LOG(warning) << "There will be no audio"sv;
    });

    ctx.sink_flag = std::make_unique<std::atomic_bool>(false);

    // The default sink has not been replaced yet.
    ctx.restore_sink = false;

    if (!(ctx.control = platf::audio_control())) {
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
    const std::string &sink = ctx.sink.host.empty() ? config::audio.sink : ctx.sink.host;
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
