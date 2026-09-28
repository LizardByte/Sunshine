/**
 * @file src/platform/macos/videotoolbox.h
 * @brief Declarations for the standalone VideoToolbox encoder.
 */
#pragma once

#include <CoreMedia/CoreMedia.h>
#include <VideoToolbox/VideoToolbox.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "src/video.h"

struct screen_capture;

namespace vt {

  /**
   * @brief Per-frame context handed to the VTCompressionSession output callback.
   * Ownership transfers to the callback when the frame is accepted by the encoder.
   */
  struct frame_ref_t {
    safe::mail_raw_t::queue_t<video::packet_t> packets;
    void *channel_data;
    int64_t frame_nr;
    uint64_t encode_start_qpc;
    std::optional<std::chrono::steady_clock::time_point> capture_pacing_timestamp; // capture time
    std::optional<std::chrono::steady_clock::time_point> frame_timestamp; // start of encode
  };

  /**
   * @brief Enumerates supported coder options.
   */
  enum coder_e : int {
    _auto = 0,  ///< Auto
    cabac,  ///< CABAC
    cavlc  ///< CAVLC
  };

}  // namespace vt

namespace platf {

  /**
   * @brief Capture-side half of the encoder: bridges the ScreenCaptureKit
   * pipeline to VideoToolbox by retaining the latest captured pixel buffer.
   */
  class videotoolbox_encode_device_t: public encode_device_t {
  public:
    using resolution_fn_t = std::function<void(screen_capture *sc, int width, int height)>;
    using pixel_format_fn_t = std::function<void(screen_capture *sc, OSType pixelFormat)>;

    ~videotoolbox_encode_device_t() override;

    int init(screen_capture *sc, pix_fmt_e pix_fmt, bool full_range, resolution_fn_t resolution_fn, const pixel_format_fn_t &pixel_format_fn);

    int convert(img_t &img) override;

    /**
     * @brief The most recently converted frame, retained until the next convert() or destruction.
     */
    CVPixelBufferRef current_frame() const;

    screen_capture *sc = nullptr;
    resolution_fn_t resolution_fn;
    OSType pixel_format = 0;  ///< CVPixelBuffer format negotiated with the capture stream in init()

  private:
    CVPixelBufferRef current = NULL;
  };

}  // namespace platf

namespace video {

  class videotoolbox_encode_session_t: public encode_session_t {
  public:
    videotoolbox_encode_session_t(const config_t &config, std::unique_ptr<platf::videotoolbox_encode_device_t> device);

    ~videotoolbox_encode_session_t() override;

    /**
     * @brief Create, configure and prepare the VTCompressionSession.
     * @return `true` on success. The session must not be used if this fails.
     */
    bool init_encoder(const encoder_t::codec_t &codec);

    int convert(platf::img_t &img) override;

    void request_idr_frame() override;

    void request_normal_frame() override;

    void invalidate_ref_frames(int64_t first_frame, int64_t last_frame) override;

    /**
     * @brief Submit the device's current frame to the compression session.
     * The encoded packet is raised asynchronously by the output callback.
     * @return 0 if the frame was accepted by the encoder.
     */
    int encode_frame(std::unique_ptr<vt::frame_ref_t> frame_ref);

    friend void output_callback(void *, void *, OSStatus, VTEncodeInfoFlags, CMSampleBufferRef);

  private:
    bool configure_session(const encoder_t::codec_t &codec);

    bool is_vt_property_supported(CFStringRef key);
    bool set_vt_property(CFStringRef key, int32_t value);
    bool set_vt_property(CFStringRef key, bool value);
    bool set_vt_property(CFStringRef key, double value);
    bool set_vt_property(CFStringRef key, CFStringRef value);
    bool set_vt_property(CFStringRef key, CFDataRef value);

    config_t config;
    std::unique_ptr<platf::videotoolbox_encode_device_t> device;
    VTCompressionSessionRef session = NULL;
    CFDictionaryRef supported_keys = NULL;
    std::atomic<bool> force_idr{false};
    CMTime last_pts = kCMTimeInvalid;
    std::chrono::steady_clock::time_point last_frame_tick {};

    bool LowLatencyRateControl = true;
  };

}  // namespace video
