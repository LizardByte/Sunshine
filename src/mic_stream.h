/**
 * @file src/mic_stream.h
 * @brief Declarations for microphone passthrough stream handling.
 *
 * Receives Opus-encoded audio from a Moonlight client and routes it to a
 * virtual audio device on the host (e.g. VB-Cable on Windows).
 */
#pragma once

// standard includes
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

// lib includes
#include <opus/opus.h>

// Platform-specific forward declarations
#ifdef _WIN32
namespace platf::virtual_mic {
  class virtual_mic_output_t;
}
#endif

namespace mic_stream {
  constexpr int SAMPLE_RATE = 48000;
  constexpr int FRAME_DURATION_MS = 20;
  constexpr int SAMPLES_PER_FRAME = SAMPLE_RATE * FRAME_DURATION_MS / 1000;  // 960

  /**
   * @brief Per-stream configuration negotiated during RTSP setup.
   */
  struct config_t {
    uint8_t audio_input_id = 0;
    uint8_t channels = 1;
    uint32_t sample_rate = SAMPLE_RATE;
    uint32_t bitrate = 64000;
  };

  /**
   * @brief Manages a single active microphone stream from one client.
   *
   * Decodes Opus-encoded frames and writes PCM to the platform virtual device.
   * Thread-safe: process_opus_data() may be called from any thread.
   */
  class mic_stream_t {
  public:
    explicit mic_stream_t(const config_t &cfg);
    ~mic_stream_t();

    /** @brief Initialize Opus decoder and platform output. @return 0 on success. */
    int start();

    /** @brief Tear down decoder and platform output. */
    void stop();

    /**
     * @brief Decode one Opus frame and send PCM to the virtual device.
     * @param data  Pointer to Opus-encoded bytes.
     * @param size  Number of bytes.
     * @return 0 on success, non-zero on failure.
     */
    int process_opus_data(const uint8_t *data, size_t size);

    bool is_active() const { return active_; }
    const config_t &config() const { return config_; }

  private:
    config_t config_;
    OpusDecoder *opus_decoder_ = nullptr;
    bool active_ = false;
    std::vector<opus_int16> pcm_buffer_;  ///< Scratch buffer for decoded PCM (16-bit)
    std::mutex mutex_;

#ifdef _WIN32
    std::unique_ptr<platf::virtual_mic::virtual_mic_output_t> virtual_output_;
#endif
  };

  /**
   * @brief Global registry of active mic streams, keyed by audioInputId.
   *
   * All public methods are thread-safe.
   */
  class mic_stream_manager_t {
  public:
    mic_stream_manager_t() = default;
    ~mic_stream_manager_t() = default;

    /** @brief Create and start a new stream. Returns nullptr on failure. */
    std::shared_ptr<mic_stream_t> start_stream(const config_t &cfg);

    /** @brief Stop and remove a stream by ID. */
    void stop_stream(uint8_t audio_input_id);

    /** @brief Return an existing stream or nullptr. */
    std::shared_ptr<mic_stream_t> get_stream(uint8_t audio_input_id);

    /** @brief Stop all active streams (called on session teardown). */
    void stop_all();

  private:
    std::unordered_map<uint8_t, std::shared_ptr<mic_stream_t>> streams_;
    std::mutex mutex_;
  };

  /** @brief Process-wide singleton manager. */
  extern mic_stream_manager_t g_mic_stream_manager;

}  // namespace mic_stream
