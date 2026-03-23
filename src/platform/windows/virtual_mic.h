/**
 * @file src/platform/windows/virtual_mic.h
 * @brief Windows WASAPI-based virtual microphone output.
 *
 * Finds a virtual audio cable device (e.g. VB-Audio Virtual Cable) and
 * renders decoded PCM audio to it so host applications see a microphone.
 */
#pragma once

// standard includes
#include <cstdint>
#include <string>

// lib includes
#include <opus/opus.h>

namespace platf::virtual_mic {

  /**
   * @brief Writes 16-bit PCM audio to a WASAPI render device (virtual cable input).
   *
   * Lifecycle: construct → init() → write_pcm() repeatedly → destroy.
   * The class is NOT thread-safe; external locking is required if called from
   * multiple threads.
   */
  class virtual_mic_output_t {
  public:
    virtual_mic_output_t() = default;
    ~virtual_mic_output_t();

    /**
     * @brief Open the virtual audio device and prepare the WASAPI client.
     * @param device_name  Friendly name substring to search for (empty = auto-detect VB-Cable).
     * @param channels     Number of channels (1 = mono, 2 = stereo).
     * @param sample_rate  Sample rate in Hz (e.g. 48000).
     * @return 0 on success, -1 on failure.
     */
    int init(const std::string &device_name, int channels, int sample_rate);

    /**
     * @brief Write a block of 16-bit signed PCM samples to the device.
     * @param data    Pointer to interleaved 16-bit signed samples.
     * @param frames  Number of audio frames (samples per channel).
     * @return 0 on success, -1 on failure.
     */
    int write_pcm(const opus_int16 *data, int frames);

    bool is_active() const { return active_; }

  private:
    /** @brief Enumerate render devices and return the first name-matching one. */
    void *find_device(const std::string &name);

    // Raw COM interface pointers — managed manually to avoid unique_ptr<COM> pitfalls
    void *device_ = nullptr;        // IMMDevice*
    void *audio_client_ = nullptr;  // IAudioClient*
    void *render_client_ = nullptr; // IAudioRenderClient*

    bool active_ = false;
    int channels_ = 1;
    int sample_rate_ = 48000;
    uint32_t buffer_frames_ = 0;  ///< Total WASAPI shared-mode buffer size in frames
  };

}  // namespace platf::virtual_mic
