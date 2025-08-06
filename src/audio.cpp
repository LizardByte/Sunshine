/**
 * @file src/audio.cpp
 * @brief Definitions for audio capture and encoding.
 */
// standard includes
#include <thread>

// lib includes
#include <opus/opus_multistream.h>

// local includes
#include "audio.h"
#include "config.h"
#include "crypto.h"
#include "globals.h"
#include "logging.h"
#include "platform/common.h"
#include "stream.h"
#include "thread_safe.h"
#include "utility.h"

namespace audio {
  using namespace std::literals;
  using opus_t = util::safe_ptr<OpusMSEncoder, opus_multistream_encoder_destroy>;
  using sample_queue_t = std::shared_ptr<safe::queue_t<std::vector<float>>>;

  static int start_audio_control(audio_ctx_t &ctx);
  static void stop_audio_control(audio_ctx_t &);
  static void apply_surround_params(opus_stream_config_t &stream, const stream_params_t &params);

  int map_stream(int channels, bool quality);

  constexpr auto SAMPLE_RATE = 48000;

  // NOTE: If you adjust the bitrates listed here, make sure to update the
  // corresponding bitrate adjustment logic in rtsp_stream::cmd_announce()
  opus_stream_config_t stream_configs[MAX_STREAM_CONFIG] {
    {
      SAMPLE_RATE,
      2,
      1,
      1,
      platf::speaker::map_stereo,
      96000,
    },
    {
      SAMPLE_RATE,
      2,
      1,
      1,
      platf::speaker::map_stereo,
      512000,
    },
    {
      SAMPLE_RATE,
      6,
      4,
      2,
      platf::speaker::map_surround51,
      256000,
    },
    {
      SAMPLE_RATE,
      6,
      6,
      0,
      platf::speaker::map_surround51,
      1536000,
    },
    {
      SAMPLE_RATE,
      8,
      5,
      3,
      platf::speaker::map_surround71,
      450000,
    },
    {
      SAMPLE_RATE,
      8,
      8,
      0,
      platf::speaker::map_surround71,
      2048000,
    },
  };

  void encodeThread(sample_queue_t samples, const config_t& config, void *channel_data) {
    auto packets = mail::man->queue<packet_t>(mail::audio_packets);
    auto stream = stream_configs[map_stream(config.channels, config.flags[config_t::HIGH_QUALITY])];
    if (config.flags[config_t::CUSTOM_SURROUND_PARAMS]) {
      apply_surround_params(stream, config.customStreamParams);
    }

    // Encoding takes place on this thread
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

    BOOST_LOG(info) << "Opus initialized: "sv << stream.sampleRate / 1000 << " kHz, "sv
                    << stream.channelCount << " channels, "sv
                    << stream.bitrate / 1000 << " kbps (total), LOWDELAY"sv;

    auto frame_size = config.packetDuration * stream.sampleRate / 1000;
    while (auto sample = samples->pop()) {
      buffer_t packet {1400};

      int bytes = opus_multistream_encode_float(opus.get(), sample->data(), frame_size, std::begin(packet), packet.size());
      if (bytes < 0) {
        BOOST_LOG(error) << "Couldn't encode audio: "sv << opus_strerror(bytes);
        packets->stop();

        return;
      }

      packet.fake_resize(bytes);
      packets->raise(channel_data, std::move(packet));
    }
  }

  void capture(safe::mail_t mail, config_t config, void *channel_data) {
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
    auto mic = control->microphone(stream.mapping, stream.channelCount, stream.sampleRate, frame_size);
    if (!mic) {
      return;
    }

    // Audio is initialized, so we don't want to print the failure message
    init_failure_fg.disable();

    // Capture takes place on this thread
    platf::adjust_thread_priority(platf::thread_priority_e::critical);

    auto samples = std::make_shared<sample_queue_t::element_type>(30);
    std::thread thread {encodeThread, samples, config, channel_data};

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
            mic = control->microphone(stream.mapping, stream.channelCount, stream.sampleRate, frame_size);
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

    // Create virtual microphone for lobby-style chat
    BOOST_LOG(info) << "Setting up virtual microphone for lobby chat";
    ctx.control->create_virtual_microphone("sunshine-virtual-mic");

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
    stream.mapping = params.mapping;
  }

  // Multi-client microphone session management
  struct mic_client_session_t {
    uint32_t client_id;
    std::unique_ptr<opus_decoder_impl_t> opus_decoder;
    std::unordered_map<uint16_t, std::unique_ptr<mic_output_t>> audio_streams;
    std::chrono::steady_clock::time_point last_activity;
    
    // Packet sequencing and timing
    std::unordered_map<uint16_t, uint16_t> expected_sequence;  // stream_id -> next expected sequence
    std::unordered_map<uint16_t, uint32_t> last_timestamp;     // stream_id -> last received timestamp
    std::unordered_map<uint16_t, std::chrono::steady_clock::time_point> stream_start_time; // stream_id -> start time
  };

  static std::unordered_map<uint32_t, std::unique_ptr<mic_client_session_t>> mic_clients;
  static std::mutex mic_clients_mutex;

  void mic_receive(safe::mail_t mail, const config_t& config, void *channel_data, stream::session_t *session) {
    if (!config::audio.enable_mic_passthrough) {
      BOOST_LOG(warning) << "Microphone pass-through requested but disabled in config"sv;
      return;
    }

    auto shutdown_event = mail->event<bool>(mail::shutdown);
    auto packets = mail->queue<packet_t>(mail::mic_packets);

    // Create microphone output device
    auto audio_ctx = get_audio_ctx_ref();
    if (!audio_ctx || !audio_ctx->control) {
      BOOST_LOG(error) << "No audio control context available for microphone output"sv;
      return;
    }

    const std::string &mic_sink = config::audio.mic_sink.empty() ? "default" : config::audio.mic_sink;
    auto mic_output = audio_ctx->control->mic_output(1, 48000, mic_sink);
    if (!mic_output) {
      BOOST_LOG(error) << "Failed to initialize microphone output device: "sv << mic_sink;
      return;
    }

    if (mic_output->start()) {
      BOOST_LOG(error) << "Failed to start microphone output device"sv;
      return;
    }

    // Create virtual microphone output for lobby-style chat
    auto virtual_mic_output = audio_ctx->control->mic_output(1, 48000, "sunshine-virtual-mic");
    bool virtual_mic_available = false;
    if (virtual_mic_output && virtual_mic_output->start() == 0) {
      virtual_mic_available = true;
      BOOST_LOG(info) << "Virtual microphone output enabled for lobby chat";
    } else {
      BOOST_LOG(warning) << "Virtual microphone output not available - lobby chat disabled";
    }

    BOOST_LOG(info) << "Started microphone receiver thread"sv;

    auto opus_dec = opus_decoder_create(48000, 1, nullptr);
    if (!opus_dec) {
      BOOST_LOG(error) << "Failed to create Opus decoder for microphone"sv;
      return;
    }

    std::vector<float> audio_decode_buffer(960); // 20ms at 48kHz mono

    while (auto packet = packets->pop()) {
      if (shutdown_event->peek()) {
        break;
      }

      // Parse microphone packet header for client identification
      auto packet_data = packet->second.data();
      auto packet_size = packet->second.size();
      
      if (packet_size < sizeof(stream::mic_packet_header_t)) {
        BOOST_LOG(warning) << "Received undersized microphone packet: " << packet_size << " bytes"sv;
        continue;
      }

      auto header = reinterpret_cast<const stream::mic_packet_header_t*>(packet_data);
      
      // Validate packet header
      if (header->version != stream::MIC_PROTOCOL_VERSION) {
        BOOST_LOG(warning) << "Unsupported microphone protocol version: " << header->version;
        continue;
      }

      if (header->packet_type != stream::MIC_PACKET_AUDIO) {
        BOOST_LOG(debug) << "Skipping non-audio microphone packet type: " << header->packet_type;
        continue;
      }

      if (header->payload_size + sizeof(stream::mic_packet_header_t) != packet_size) {
        BOOST_LOG(warning) << "Microphone packet size mismatch: expected " 
                          << header->payload_size + sizeof(stream::mic_packet_header_t) 
                          << ", got " << packet_size;
        continue;
      }

      // Log client and stream info for debugging
      BOOST_LOG(debug) << "Processing microphone packet from client " << header->client_id 
                      << ", stream " << header->stream_id 
                      << ", sequence " << header->sequence
                      << ", timestamp " << header->timestamp
                      << ", encrypted " << ((header->flags & stream::MIC_FLAG_ENCRYPTED) ? "yes" : "no");

      // Handle encrypted packets
      std::vector<uint8_t> decrypted_payload;
      const unsigned char* opus_data;
      size_t opus_size;
      
      if (header->flags & stream::MIC_FLAG_ENCRYPTED) {
        // Check if session has encryption enabled
        if (!session || !session->microphone.cipher) {
          BOOST_LOG(warning) << "Received encrypted microphone packet but encryption not enabled for session";
          continue;
        }
        
        // Construct IV using NIST SP 800-38D deterministic method
        crypto::aes_t iv(12);
        std::copy_n((uint8_t *) &header->sequence, sizeof(header->sequence), std::begin(iv));
        iv[10] = 'M';  // Microphone
        iv[11] = 'C';  // Client originated
        
        // Decrypt the payload
        auto encrypted_payload = packet_data + sizeof(stream::mic_packet_header_t);
        std::string_view ciphertext_with_tag{
          reinterpret_cast<const char*>(header->tag), 
          sizeof(header->tag) + header->payload_size
        };
        
        if (session->microphone.cipher->decrypt(ciphertext_with_tag, decrypted_payload, &iv)) {
          BOOST_LOG(warning) << "Failed to decrypt microphone packet from client " << header->client_id;
          continue;
        }
        
        opus_data = decrypted_payload.data();
        opus_size = decrypted_payload.size();
      } else {
        // Unencrypted packet - use payload directly  
        opus_data = reinterpret_cast<const unsigned char*>(packet_data + sizeof(stream::mic_packet_header_t));
        opus_size = header->payload_size;
      }

      // Get or create client session
      std::lock_guard<std::mutex> lock(mic_clients_mutex);
      
      auto client_it = mic_clients.find(header->client_id);
      if (client_it == mic_clients.end()) {
        // Check client limit (hardcoded to 4 for now, will be made configurable per client request)
        constexpr uint32_t MAX_MIC_CLIENTS = 4;
        if (mic_clients.size() >= MAX_MIC_CLIENTS) {
          BOOST_LOG(warning) << "Microphone client limit reached (" << MAX_MIC_CLIENTS 
                            << "), rejecting client " << header->client_id;
          continue;
        }
        
        // Create new client session
        auto client_session = std::make_unique<mic_client_session_t>();
        client_session->client_id = header->client_id;
        client_session->opus_decoder = std::unique_ptr<opus_decoder_impl_t>(
          reinterpret_cast<opus_decoder_impl_t*>(opus_decoder_create(48000, 1, nullptr))
        );
        
        if (!client_session->opus_decoder) {
          BOOST_LOG(error) << "Failed to create Opus decoder for client " << header->client_id;
          continue;
        }
        
        client_it = mic_clients.emplace(header->client_id, std::move(client_session)).first;
        BOOST_LOG(info) << "Created new microphone session for client " << header->client_id;
      }
      
      auto& client_session = client_it->second;
      client_session->last_activity = std::chrono::steady_clock::now();
      
      // Validate packet sequence and timestamp for this stream
      auto stream_id = header->stream_id;
      auto expected_seq_it = client_session->expected_sequence.find(stream_id);
      
      if (expected_seq_it != client_session->expected_sequence.end()) {
        // Check sequence number for existing stream
        uint16_t expected_seq = expected_seq_it->second;
        if (header->sequence != expected_seq) {
          if (header->sequence < expected_seq) {
            // Duplicate or out-of-order packet - ignore
            BOOST_LOG(debug) << "Ignoring duplicate/old packet from client " << header->client_id 
                            << ", stream " << stream_id 
                            << ", got sequence " << header->sequence 
                            << ", expected " << expected_seq;
            continue;
          } else {
            // Missing packets detected
            uint16_t missed_packets = header->sequence - expected_seq;
            BOOST_LOG(warning) << "Missed " << missed_packets << " packet(s) from client " 
                              << header->client_id << ", stream " << stream_id 
                              << ", expected " << expected_seq << ", got " << header->sequence;
          }
        }
        
        // Validate timestamp progression (should be monotonically increasing)
        auto last_timestamp_it = client_session->last_timestamp.find(stream_id);
        if (last_timestamp_it != client_session->last_timestamp.end()) {
          uint32_t last_ts = last_timestamp_it->second;
          if (header->timestamp <= last_ts) {
            BOOST_LOG(warning) << "Non-monotonic timestamp from client " << header->client_id 
                              << ", stream " << stream_id 
                              << ", current " << header->timestamp 
                              << ", last " << last_ts;
          }
        }
      } else {
        // First packet for this stream - initialize tracking
        BOOST_LOG(info) << "Starting sequence tracking for client " << header->client_id 
                       << ", stream " << stream_id 
                       << ", starting sequence " << header->sequence;
        client_session->stream_start_time[stream_id] = std::chrono::steady_clock::now();
      }
      
      // Update sequence and timestamp tracking
      client_session->expected_sequence[stream_id] = header->sequence + 1;
      client_session->last_timestamp[stream_id] = header->timestamp;
      
      // Get or create audio stream for this client/stream combination
      auto stream_it = client_session->audio_streams.find(header->stream_id);
      if (stream_it == client_session->audio_streams.end()) {
        // Check stream limit per client (hardcoded to 2 for now, will be made configurable per client request)
        constexpr uint32_t MAX_STREAMS_PER_CLIENT = 2;
        if (client_session->audio_streams.size() >= MAX_STREAMS_PER_CLIENT) {
          BOOST_LOG(warning) << "Stream limit reached for client " << header->client_id 
                            << " (" << MAX_STREAMS_PER_CLIENT << "), ignoring stream " << header->stream_id;
          continue;
        }
        
        // Create new audio output stream
        auto client_audio_ctx = get_audio_ctx_ref();
        if (!client_audio_ctx || !client_audio_ctx->control) {
          BOOST_LOG(error) << "No audio control context available for client " << header->client_id;
          continue;
        }
        
        const std::string client_mic_sink = config::audio.mic_sink.empty() ? "default" : config::audio.mic_sink;
        auto client_mic_output = client_audio_ctx->control->mic_output(1, 48000, client_mic_sink);
        if (!client_mic_output || client_mic_output->start()) {
          BOOST_LOG(error) << "Failed to create audio output for client " << header->client_id 
                          << ", stream " << header->stream_id;
          continue;
        }
        
        stream_it = client_session->audio_streams.emplace(header->stream_id, std::move(client_mic_output)).first;
        BOOST_LOG(info) << "Created audio stream " << header->stream_id << " for client " << header->client_id;
      }

      // Decode and output audio
      std::vector<float> packet_decode_buffer(960); // 20ms at 48kHz mono
      int decoded_samples = opus_decode_float(
        reinterpret_cast<OpusDecoder*>(client_session->opus_decoder.get()), 
        opus_data, opus_size, packet_decode_buffer.data(), packet_decode_buffer.size(), 0
      );
      
      if (decoded_samples > 0) {
        packet_decode_buffer.resize(decoded_samples);
        stream_it->second->output_samples(packet_decode_buffer);
        
        // Also output to virtual microphone for lobby chat
        if (virtual_mic_available) {
          virtual_mic_output->output_samples(packet_decode_buffer);
        }
      } else {
        BOOST_LOG(warning) << "Failed to decode Opus data from client " << header->client_id 
                          << ", stream " << header->stream_id << ": " << opus_strerror(decoded_samples);
      }
    }

    // Cleanup all client sessions
    {
      std::lock_guard<std::mutex> lock(mic_clients_mutex);
      for (const auto& [client_id, client_session] : mic_clients) {
        for (auto& [stream_id, cleanup_mic_output] : client_session->audio_streams) {
          cleanup_mic_output->stop();
        }
        if (client_session->opus_decoder) {
          opus_decoder_destroy(reinterpret_cast<OpusDecoder*>(client_session->opus_decoder.release()));
        }
      }
      mic_clients.clear();
    }
    
    // Stop virtual microphone output
    if (virtual_mic_available) {
      virtual_mic_output->stop();
    }
    
    BOOST_LOG(info) << "Stopped microphone receiver thread"sv;
  }
}  // namespace audio
