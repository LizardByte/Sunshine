/**
 * @file src/mic_stream.cpp
 * @brief Microphone passthrough stream management.
 *
 * Receives Opus-encoded packets from the Moonlight client (via input.cpp),
 * decodes them, and routes the raw PCM audio to the platform virtual device.
 */

// standard includes
#include <cstring>

// local includes
#include "config.h"
#include "logging.h"
#include "mic_stream.h"

#ifdef _WIN32
  #include "platform/windows/virtual_mic.h"
#endif

namespace mic_stream {
  using namespace std::literals;

  // -----------------------------------------------------------------
  // Global singleton manager
  // -----------------------------------------------------------------
  mic_stream_manager_t g_mic_stream_manager;

  // -----------------------------------------------------------------
  // mic_stream_t
  // -----------------------------------------------------------------

  mic_stream_t::mic_stream_t(const config_t &cfg)
      : config_(cfg) {
    // Pre-allocate PCM scratch buffer for the maximum expected frame size
    pcm_buffer_.resize(SAMPLES_PER_FRAME * cfg.channels);
  }

  mic_stream_t::~mic_stream_t() {
    stop();
  }

  int mic_stream_t::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (active_) {
      BOOST_LOG(warning) << "mic_stream_t::start() called while already active"sv;
      return 0;
    }

    // Create Opus decoder
    int opus_error = 0;
    opus_decoder_ = opus_decoder_create(
      static_cast<opus_int32>(config_.sample_rate),
      static_cast<int>(config_.channels),
      &opus_error);

    if (opus_error != OPUS_OK || !opus_decoder_) {
      BOOST_LOG(error) << "Opus decoder creation failed: "sv << opus_strerror(opus_error);
      return -1;
    }

#ifdef _WIN32
    // Initialise the WASAPI virtual mic output
    virtual_output_ = std::make_unique<platf::virtual_mic::virtual_mic_output_t>();
    if (virtual_output_->init(config::audio.mic_virtual_device,
                              static_cast<int>(config_.channels),
                              static_cast<int>(config_.sample_rate)) != 0) {
      BOOST_LOG(error) << "Failed to initialise virtual mic output. "
                          "Install VB-Cable from https://vb-audio.com/Cable/ or set mic_virtual_device in config."sv;
      opus_decoder_destroy(opus_decoder_);
      opus_decoder_ = nullptr;
      return -1;
    }
#endif

    active_ = true;
    BOOST_LOG(info) << "Mic stream started — channels=" << static_cast<int>(config_.channels)
                    << ", sample_rate=" << config_.sample_rate
                    << ", bitrate=" << config_.bitrate;
    return 0;
  }

  void mic_stream_t::stop() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!active_) {
      return;
    }

    active_ = false;

#ifdef _WIN32
    virtual_output_.reset();
#endif

    if (opus_decoder_) {
      opus_decoder_destroy(opus_decoder_);
      opus_decoder_ = nullptr;
    }

    BOOST_LOG(info) << "Mic stream stopped"sv;
  }

  int mic_stream_t::process_opus_data(const uint8_t *data, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!active_ || !opus_decoder_) {
      return 0;  // silently discard if not yet active
    }

    // Decode Opus → 16-bit PCM
    int decoded_samples = opus_decode(
      opus_decoder_,
      data,
      static_cast<opus_int32>(size),
      pcm_buffer_.data(),
      SAMPLES_PER_FRAME,
      0 /* decode_fec */);

    if (decoded_samples < 0) {
      BOOST_LOG(error) << "Opus decode error: "sv << opus_strerror(decoded_samples);
      return -1;
    }

    BOOST_LOG(verbose) << "Mic: decoded "sv << decoded_samples
                       << " samples from "sv << size << " bytes"sv;

#ifdef _WIN32
    if (virtual_output_ && virtual_output_->is_active()) {
      if (virtual_output_->write_pcm(pcm_buffer_.data(), decoded_samples) != 0) {
        BOOST_LOG(warning) << "Failed to write PCM to virtual mic output"sv;
      }
    }
#else
    BOOST_LOG(verbose) << "Mic PCM output not implemented on this platform"sv;
#endif

    return 0;
  }

  // -----------------------------------------------------------------
  // mic_stream_manager_t
  // -----------------------------------------------------------------

  std::shared_ptr<mic_stream_t> mic_stream_manager_t::start_stream(const config_t &cfg) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = streams_.find(cfg.audio_input_id);
    if (it != streams_.end()) {
      BOOST_LOG(warning) << "Mic stream "sv << static_cast<int>(cfg.audio_input_id) << " already exists"sv;
      return it->second;
    }

    auto stream = std::make_shared<mic_stream_t>(cfg);
    if (stream->start() != 0) {
      BOOST_LOG(error) << "Failed to start mic stream "sv << static_cast<int>(cfg.audio_input_id);
      return nullptr;
    }

    streams_.emplace(cfg.audio_input_id, stream);
    return stream;
  }

  void mic_stream_manager_t::stop_stream(uint8_t audio_input_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = streams_.find(audio_input_id);
    if (it != streams_.end()) {
      it->second->stop();
      streams_.erase(it);
    }
  }

  std::shared_ptr<mic_stream_t> mic_stream_manager_t::get_stream(uint8_t audio_input_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = streams_.find(audio_input_id);
    return (it != streams_.end()) ? it->second : nullptr;
  }

  void mic_stream_manager_t::stop_all() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto &pair : streams_) {
      pair.second->stop();
    }
    streams_.clear();
    BOOST_LOG(info) << "All mic streams stopped"sv;
  }

}  // namespace mic_stream
