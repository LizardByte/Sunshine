/**
 * @file src/platform/macos/videotoolbox.mm
 * @brief Definitions for the standalone VideoToolbox encoder.
 */
#include "src/platform/macos/coreaudio_helpers.h"
#include "src/platform/macos/sck_video.h"
#include "src/platform/macos/videotoolbox.h"
#include "src/platform/macos/av_img_t.h"
#include "src/platform/macos/cf_helpers.h"
#include "src/platform/macos/qpc_chrono.h"
#include "src/logging.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>

#include <CoreVideo/CoreVideo.h>
#include <Foundation/Foundation.h>

// These are not exposed in header files, but are returned as valid profiles from the API
VT_EXPORT const CFStringRef kVTProfileLevel_HEVC_Main44410_AutoLevel = CFSTR("HEVC_Main44410_AutoLevel");
VT_EXPORT const CFStringRef kVTProfileLevel_HEVC_Main444_AutoLevel = CFSTR("HEVC_Main444_AutoLevel");
VT_EXPORT const CFStringRef kVTProfileLevel_H264_High444Predictive_AutoLevel = CFSTR("H264_High444Predictive_AutoLevel");

/// Annex B
static const uint8_t kAnnexBStartCode[4] = {0x00, 0x00, 0x00, 0x01};

static bool IsKeyframe(CFArrayRef attachments) {
  if (attachments == NULL || CFArrayGetCount(attachments) == 0) {
    return true;
  }

  auto attachment = (CFDictionaryRef) CFArrayGetValueAtIndex(attachments, 0);
  return CFDictionaryGetValue(attachment, kCMSampleAttachmentKey_NotSync) != kCFBooleanTrue;
}

// Appends SPS/PPS (H.264) or VPS/SPS/PPS (HEVC) with start codes.
// Also reports the stream's NAL length-prefix size (usually 4).
static bool AppendParameterSets(CMFormatDescriptionRef format, std::vector<uint8_t> *out, int *nalLengthFieldSize) {
  FourCharCode codec = CMFormatDescriptionGetMediaSubType(format);
  size_t count = 0;
  OSStatus status;

  if (codec == kCMVideoCodecType_H264) {
    status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
      format, 0, NULL, NULL, &count, nalLengthFieldSize);
  }
  else if (codec == kCMVideoCodecType_HEVC) {
    status = CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
      format, 0, NULL, NULL, &count, nalLengthFieldSize);
  }
  else {
    return false;  // unsupported codec
  }
  if (status != noErr) {
    return false;
  }

  if (!out) {
    return true;  // caller only wanted nalLengthFieldSize
  }

  for (size_t i = 0; i < count; i++) {
    const uint8_t *ps = NULL;
    size_t psSize = 0;
    if (codec == kCMVideoCodecType_H264) {
      status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
        format, i, &ps, &psSize, NULL, NULL);
    }
    else {
      status = CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
        format, i, &ps, &psSize, NULL, NULL);
    }
    if (status != noErr || !ps) {
      return false;
    }
    out->insert(out->end(), kAnnexBStartCode, kAnnexBStartCode + sizeof(kAnnexBStartCode));
    out->insert(out->end(), ps, ps + psSize);
  }
  return true;
}

/// Converts an encoded sample buffer to an Annex B byte stream, written
/// directly into `out` (avoids an intermediate NSData and its autorelease).
/// Parameter sets are prepended on keyframes. Returns false on failure.
static bool SampleBufferToAnnexB(CMSampleBufferRef sample, bool is_keyframe, std::vector<uint8_t> &out) {
  CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sample);
  CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
  if (!format || !block) {
    return false;
  }

  int nalLengthFieldSize = 4;
  if (!AppendParameterSets(format, nullptr, &nalLengthFieldSize)) {
    return false;
  }
  if (nalLengthFieldSize < 1 || nalLengthFieldSize > 4) {
    return false;
  }

  // Ensure we can read the payload as one contiguous range.
  CMBlockBufferRef contiguous = NULL;
  if (!CMBlockBufferIsRangeContiguous(block, 0, 0)) {
    if (CMBlockBufferCreateContiguous(kCFAllocatorDefault, block, NULL, NULL, 0, 0, 0, &contiguous) != noErr) {
      return false;
    }
    block = contiguous;
  }
  auto contiguous_guard = util::fail_guard([contiguous]() {
    if (contiguous) {
      CFRelease(contiguous);
    }
  });

  size_t totalLength = 0;
  char *data = NULL;
  if (CMBlockBufferGetDataPointer(block, 0, NULL, &totalLength, &data) != noErr) {
    return false;
  }

  out.clear();
  out.reserve(totalLength + 256);

  if (is_keyframe) {
    if (!AppendParameterSets(format, &out, &nalLengthFieldSize)) {
      return false;
    }
  }

  // Rewrite each length-prefixed NAL unit with a start code.
  size_t offset = 0;
  while (offset + (size_t) nalLengthFieldSize <= totalLength) {
    uint64_t nalLength = 0;
    for (int i = 0; i < nalLengthFieldSize; i++) {
      nalLength = (nalLength << 8) | (uint8_t) data[offset + i];
    }
    offset += nalLengthFieldSize;
    if (nalLength == 0 || offset + nalLength > totalLength) {
      // Malformed buffer
      return false;
    }
    out.insert(out.end(), kAnnexBStartCode, kAnnexBStartCode + sizeof(kAnnexBStartCode));
    out.insert(out.end(), (const uint8_t *) data + offset, (const uint8_t *) data + offset + nalLength);
    offset += nalLength;
  }

  return !out.empty();
}

namespace platf {

  videotoolbox_encode_device_t::~videotoolbox_encode_device_t() {
    if (current) {
      CVPixelBufferRelease(current);
      current = NULL;
    }
  }

  int videotoolbox_encode_device_t::init(::screen_capture *sc, pix_fmt_e pix_fmt, bool full_range, resolution_fn_t resolution_fn, const pixel_format_fn_t &pixel_format_fn) {
    switch (pix_fmt) {
      case pix_fmt_e::p010:
        pixel_format = full_range ? kCVPixelFormatType_420YpCbCr10BiPlanarFullRange : kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
        break;
      case pix_fmt_e::nv24:
        pixel_format = full_range ? kCVPixelFormatType_444YpCbCr8BiPlanarFullRange : kCVPixelFormatType_444YpCbCr8BiPlanarVideoRange;
        break;
      case pix_fmt_e::p410:
        pixel_format = full_range ? kCVPixelFormatType_444YpCbCr10BiPlanarFullRange : kCVPixelFormatType_444YpCbCr10BiPlanarVideoRange;
        break;
      case pix_fmt_e::nv12:
      default:
        pixel_format = full_range ? kCVPixelFormatType_420YpCbCr8BiPlanarFullRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        break;
    }
    pixel_format_fn(sc, pixel_format);

    this->sc = sc;
    this->resolution_fn = std::move(resolution_fn);

    return 0;
  }

  int videotoolbox_encode_device_t::convert(img_t &img) {
    auto *av_img = (av_img_t *) &img;
    if (!av_img->pixel_buffer || !av_img->pixel_buffer->buf) {
      return -1;
    }

    auto new_pixel = (CVPixelBufferRef) CFRetain(av_img->pixel_buffer->buf);
    if (current) {
      CVPixelBufferRelease(current);
    }
    current = new_pixel;
    return 0;
  }

  CVPixelBufferRef videotoolbox_encode_device_t::current_frame() const {
    return current;
  }

}  // namespace platf

namespace video {

  static CFStringRef sunshine_to_vt_matrix(const sunshine_colorspace_t &cs) {
    switch (cs.colorspace) {
      case colorspace_e::rec601:
        return kCVImageBufferYCbCrMatrix_ITU_R_601_4;
      case colorspace_e::bt2020:
      case colorspace_e::bt2020sdr:
        return kCVImageBufferYCbCrMatrix_ITU_R_2020;
      default:
        return kCVImageBufferYCbCrMatrix_ITU_R_709_2;
    }
  }

  static CFStringRef sunshine_to_vt_primaries(const sunshine_colorspace_t &cs) {
    switch (cs.colorspace) {
      case colorspace_e::rec601:
        return kCVImageBufferColorPrimaries_SMPTE_C;
      case colorspace_e::bt2020:
      case colorspace_e::bt2020sdr:
        return kCVImageBufferColorPrimaries_ITU_R_2020;
      default:
        return kCVImageBufferColorPrimaries_ITU_R_709_2;
    }
  }

  static CFStringRef sunshine_to_vt_transfer(const sunshine_colorspace_t &cs) {
    switch (cs.colorspace) {
      case colorspace_e::rec601:
        return kCVImageBufferTransferFunction_sRGB;
      case colorspace_e::bt2020:
        return kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ;
      case colorspace_e::bt2020sdr:
      default:
        return kCVImageBufferTransferFunction_ITU_R_709_2;
    }
  }

  /* Adapted from Chromium GenerateMasteringDisplayColorVolume */
  static CFDataRef sunshine_to_vt_masteringdisplay(uint32_t hdr_nominal_peak_level) {
    struct mastering_display_colour_volume {
      uint16_t display_primaries[3][2];
      uint16_t white_point[2];
      uint32_t max_display_mastering_luminance;
      uint32_t min_display_mastering_luminance;
    };

    static_assert(sizeof(struct mastering_display_colour_volume) == 24, "May need to adjust struct packing");

    struct mastering_display_colour_volume mdcv;
    mdcv.display_primaries[0][0] = __builtin_bswap16(13250);
    mdcv.display_primaries[0][1] = __builtin_bswap16(34500);
    mdcv.display_primaries[1][0] = __builtin_bswap16(7500);
    mdcv.display_primaries[1][1] = __builtin_bswap16(3000);
    mdcv.display_primaries[2][0] = __builtin_bswap16(34000);
    mdcv.display_primaries[2][1] = __builtin_bswap16(16000);
    mdcv.white_point[0] = __builtin_bswap16(15635);
    mdcv.white_point[1] = __builtin_bswap16(16450);
    mdcv.max_display_mastering_luminance = __builtin_bswap32(hdr_nominal_peak_level * 10000);
    mdcv.min_display_mastering_luminance = 0;

    UInt8 bytes[sizeof(struct mastering_display_colour_volume)];
    memcpy(bytes, &mdcv, sizeof(bytes));
    return CFDataCreate(kCFAllocatorDefault, bytes, sizeof(bytes));
  }

  /* Adapted from Chromium GenerateContentLightLevelInfo */
  static CFDataRef sunshine_to_vt_contentlightlevelinfo(uint16_t hdr_nominal_peak_level) {
    struct content_light_level_info {
      uint16_t max_content_light_level;
      uint16_t max_pic_average_light_level;
    };

    static_assert(sizeof(struct content_light_level_info) == 4, "May need to adjust struct packing");

    struct content_light_level_info clli;
    clli.max_content_light_level = __builtin_bswap16(hdr_nominal_peak_level);
    clli.max_pic_average_light_level = __builtin_bswap16(hdr_nominal_peak_level);

    UInt8 bytes[sizeof(struct content_light_level_info)];
    memcpy(bytes, &clli, sizeof(bytes));
    return CFDataCreate(kCFAllocatorDefault, bytes, sizeof(bytes));
  }

  videotoolbox_encode_session_t::videotoolbox_encode_session_t(const config_t &config, std::unique_ptr<platf::videotoolbox_encode_device_t> device):
      config(config),
      device(std::move(device))
  {
    last_pts = CMTimeMake(0, 90000);
  }

  videotoolbox_encode_session_t::~videotoolbox_encode_session_t() {
    if (supported_keys) {
      CFRelease(supported_keys);
      supported_keys = NULL;
    }

    if (session) {
      BOOST_LOG(debug) << "VideoToolbox: completing session";

      // CompleteFrames blocks until every pending output callback has run and
      // Invalidate prevents any further ones, so no callback (and no in-flight
      // frame_ref_t) can outlive this session
      VTCompressionSessionCompleteFrames(session, kCMTimeIndefinite);
      VTCompressionSessionInvalidate(session);
      CFRelease(session);
      session = NULL;
    }
  }

  bool videotoolbox_encode_session_t::configure_session(const encoder_t::codec_t &codec) {
    const bool chroma444 = config.chromaSamplingType == 1;

    CFStringRef profile = NULL;
    switch (config.videoFormat) {
      case 1:
        if (chroma444) {
          profile = device->colorspace.bit_depth == 10
            ? kVTProfileLevel_HEVC_Main44410_AutoLevel
            : kVTProfileLevel_HEVC_Main444_AutoLevel;
        } else {
          profile = device->colorspace.bit_depth == 10
            ? kVTProfileLevel_HEVC_Main10_AutoLevel
            : kVTProfileLevel_HEVC_Main_AutoLevel;
        }
        break;
      default:
        // let h264 choose the profile
        break;
    }

    if (profile) {
      set_vt_property(kVTCompressionPropertyKey_ProfileLevel, profile);
    }

    // Set simple properties defined in video.cpp
    {
      static const std::unordered_map<std::string, CFStringRef> vt_property_keys {
        {"AllowFrameReordering",               kVTCompressionPropertyKey_AllowFrameReordering},
        {"AllowTemporalCompression",           kVTCompressionPropertyKey_AllowTemporalCompression},
        {"AllowOpenGOP",                       kVTCompressionPropertyKey_AllowOpenGOP},
        {"H264EntropyMode",                    kVTCompressionPropertyKey_H264EntropyMode},
        {"MaxKeyFrameInterval",                kVTCompressionPropertyKey_MaxKeyFrameInterval},
        {"MaxKeyFrameIntervalDuration",        kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration},
        {"PrioritizeEncodingSpeedOverQuality", kVTCompressionPropertyKey_PrioritizeEncodingSpeedOverQuality},
        {"RealTime",                           kVTCompressionPropertyKey_RealTime},
        {"ReferenceBufferCount",               kVTCompressionPropertyKey_ReferenceBufferCount},
      };

      auto handle_option = [this](const encoder_t::option_t &option) {
        const auto it = vt_property_keys.find(option.name);
        if (it == vt_property_keys.end()) {
          BOOST_LOG(error) << "VideoToolbox: unknown property " << option.name;
          return;
        }

        const CFStringRef key = it->second;

        std::visit(
          util::overloaded {
            [this, key](bool value) {
              set_vt_property(key, value);
            },
            [this, key](int value) {
              set_vt_property(key, static_cast<int32_t>(value));
            },
            [this, key](int *value) {
              if (key == kVTCompressionPropertyKey_H264EntropyMode) {
                // if auto, don't set and let the encoder choose one
                if (*value == vt::coder_e::_auto) return;
                CFStringRef entropy = (*value == vt::coder_e::cabac)
                  ? kVTH264EntropyMode_CABAC
                  : kVTH264EntropyMode_CAVLC;
                set_vt_property(key, entropy);
              } else {
                set_vt_property(key, *value);
              }
            },
            [&option](const auto &) {
              BOOST_LOG(error) << "VideoToolbox: unsupported value type for property " << option.name;
            }
          },
          option.value
        );
      };

      for (const auto &option : codec.common_options) {
        handle_option(option);
      }
    }

    int32_t bitrate = ((config::video.max_bitrate > 0) ? std::min(config.bitrate, config::video.max_bitrate) : config.bitrate) * 1000;
    set_vt_property(kVTCompressionPropertyKey_AverageBitRate, bitrate);
    set_vt_property(kVTCompressionPropertyKey_ExpectedFrameRate, av_q2d(device->sc->fps));
    set_vt_property(kVTCompressionPropertyKey_MaximumRealTimeFrameRate, av_q2d(device->sc->fps));
    set_vt_property(kVTCompressionPropertyKey_ColorPrimaries, sunshine_to_vt_primaries(device->colorspace));
    set_vt_property(kVTCompressionPropertyKey_TransferFunction, sunshine_to_vt_transfer(device->colorspace));
    set_vt_property(kVTCompressionPropertyKey_YCbCrMatrix, sunshine_to_vt_matrix(device->colorspace));

    if (device->colorspace.colorspace == colorspace_e::bt2020) {
      const uint16_t hdr_nominal_peak_level = 1000;
      CFDataRef mdcv = sunshine_to_vt_masteringdisplay(hdr_nominal_peak_level);
      CFDataRef cll = sunshine_to_vt_contentlightlevelinfo(hdr_nominal_peak_level);
      set_vt_property(kVTCompressionPropertyKey_MasteringDisplayColorVolume, mdcv);
      set_vt_property(kVTCompressionPropertyKey_ContentLightLevelInfo, cll);
      CFRelease(mdcv);
      CFRelease(cll);
    }

    return true;
  }

  void output_callback(void *outputCallbackRefCon,
                       void *sourceFrameRefCon,
                       OSStatus status,
                       VTEncodeInfoFlags infoFlags,
                       CMSampleBufferRef sampleBuffer
  ) {
    auto *me = reinterpret_cast<videotoolbox_encode_session_t *>(outputCallbackRefCon);
    std::unique_ptr<vt::frame_ref_t> frame_ref {static_cast<vt::frame_ref_t *>(sourceFrameRefCon)};
    if (!me || !frame_ref) {
      return;
    }

    if (status != noErr) {
      BOOST_LOG(error) << "VideoToolbox error: encode of frame " << frame_ref->frame_nr << " failed: " << ca::Status(status);
      return;
    }

    // infoFlags is a bitmask (kVTEncodeInfo_Asynchronous may be set as well),
    // and dropped frames carry no sample buffer
    if (infoFlags & kVTEncodeInfo_FrameDropped) {
      BOOST_LOG(warning) << "VideoToolbox encoder dropped frame " << frame_ref->frame_nr;
      return;
    }

    if (!sampleBuffer || !CMSampleBufferDataIsReady(sampleBuffer)) {
      BOOST_LOG(error) << "VideoToolbox error: no sample data for frame " << frame_ref->frame_nr;
      return;
    }

    // BOOST_LOG(verbose) << "encoded frame: " << cf_desc_to_std_string(sampleBuffer);

    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    bool is_keyframe = IsKeyframe(attachments); // it's ok if attachments is NULL
    std::vector<uint8_t> annex_b;
    if (!SampleBufferToAnnexB(sampleBuffer, is_keyframe, annex_b)) {
      BOOST_LOG(error) << "VideoToolbox error: couldn't package frame " << frame_ref->frame_nr;
      return;
    }

    auto packet = std::make_unique<packet_raw_generic>(std::move(annex_b), frame_ref->frame_nr, is_keyframe);
    packet->channel_data = frame_ref->channel_data;
    packet->capture_pacing_timestamp = frame_ref->capture_pacing_timestamp;
    packet->frame_timestamp = frame_ref->frame_timestamp;

    BOOST_LOG(verbose) << "VideoToolbox:   end encode #" << frame_ref->frame_nr
                       << " took" << std::fixed << std::setprecision(3)
                       << " capture=" << std::chrono::duration<double, std::milli>(*frame_ref->frame_timestamp - *frame_ref->capture_pacing_timestamp).count()
                       << " enc=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - *frame_ref->frame_timestamp).count()
                       << " size " << packet->data_size();

    frame_ref->packets->raise(std::move(packet));
  }

  bool videotoolbox_encode_session_t::init_encoder(const encoder_t::codec_t &codec) {
    bool ret = true;

    // Ask the capture stream to deliver frames at the encode resolution
    if (device->resolution_fn && device->sc) {
      device->resolution_fn(device->sc, config.width, config.height);
    }

    CFTypeRef keys[3] = {kVTVideoEncoderSpecification_EnableLowLatencyRateControl,
                         kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder,
                         kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder};
    CFTypeRef values[3] = {LowLatencyRateControl ? kCFBooleanTrue : kCFBooleanFalse,
                           config::video.vt.vt_require_sw ? kCFBooleanFalse : kCFBooleanTrue,
                           config::video.vt.vt_allow_sw ? kCFBooleanFalse : kCFBooleanTrue};
    CFDictionaryRef encoder_spec = CFDictionaryCreate(
      kCFAllocatorDefault, keys, values, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    CMVideoCodecType codecType;
    switch (config.videoFormat) {
      case 2:
        codecType = kCMVideoCodecType_AV1;
        break;
      case 1:
        codecType = kCMVideoCodecType_HEVC;
        break;
      case 0:
      default:
        codecType = kCMVideoCodecType_H264;
        break;
    }

    OSStatus err = VTCompressionSessionCreate(
      kCFAllocatorDefault,
      config.width,
      config.height,
      codecType,
      encoder_spec,
      NULL,
      NULL,
      output_callback,
      this,
      &session
    );
    if (err != noErr || !session) {
      BOOST_LOG(error) << "VideoToolbox error: VTCompressionSessionCreate failed, error = " << ca::Status(err);
      ret = false;
      goto out;
    }

    if (!configure_session(codec)) {
      BOOST_LOG(error) << "VideoToolbox error: couldn't configure encoder";
      ret = false;
      goto out;
    }

    err = VTCompressionSessionPrepareToEncodeFrames(session);
    if (err != noErr) {
      BOOST_LOG(error) << "VideoToolbox error: VTCompressionSessionPrepareToEncodeFrames failed: " << ca::Status(err);
      ret = false;
      goto out;
    }

    BOOST_LOG(info) << "VideoToolbox encode session prepared: " << cf_desc_to_std_string(session);

out:
    CFRelease(encoder_spec);

    return ret;
  }

  int videotoolbox_encode_session_t::convert(platf::img_t &img) {
    if (!device) {
      return -1;
    }
    return device->convert(img);
  }

  void videotoolbox_encode_session_t::request_idr_frame() {
    force_idr.store(true);
  }

  void videotoolbox_encode_session_t::request_normal_frame() {
    force_idr.store(false);
  }

  void videotoolbox_encode_session_t::invalidate_ref_frames(int64_t first_frame, int64_t last_frame) {
    BOOST_LOG(error) << "VideoToolbox doesn't support reference frame invalidation";
    request_idr_frame();
  }

  int videotoolbox_encode_session_t::encode_frame(std::unique_ptr<vt::frame_ref_t> frame_ref) {
    if (!session || !device) {
      return -1;
    }

    CVPixelBufferRef pixbuf = device->current_frame();
    if (!pixbuf) {
      BOOST_LOG(error) << "VideoToolbox error: no frame to encode";
      return -1;
    }

    bool idr = force_idr.load();

    CFMutableDictionaryRef frame_properties = NULL;
    auto set_frame_property = [&frame_properties](CFStringRef key, CFTypeRef value) {
      if (!frame_properties) {
        frame_properties = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
      }
      CFDictionarySetValue(frame_properties, key, value);
    };

    if (idr) {
      set_frame_property(kVTEncodeFrameOptionKey_ForceKeyFrame, kCFBooleanTrue);
    }

    CMTime pts = kCMTimeInvalid;
    if (frame_ref->capture_pacing_timestamp) {
      using vt_tick = std::chrono::duration<int64_t, std::ratio<1, 90000>>;
      pts = CMTimeMake(std::chrono::round<vt_tick>(*frame_ref->capture_pacing_timestamp - video::video_epoch()).count(), 90000);
    }

    // Handle duplicate frames with no timestamp by synthesizing the next one based on framerate
    // Also check that we don't go backwards
    bool pts_is_real = true;
    if (CMTIME_IS_VALID(last_pts)) {
      if (CMTIME_IS_INVALID(pts) || CMTimeCompare(pts, last_pts) <= 0) {
        // Fake PTS advances by the elapsed time since last_frame_tick
        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - last_frame_tick);
        pts = CMTimeConvertScale(
          CMTimeAdd(last_pts, CMTimeMakeWithSeconds(elapsed.count(), NSEC_PER_SEC)),
          90000,
          kCMTimeRoundingMethod_RoundHalfAwayFromZero);
        pts_is_real = false;
      }
    }

    BOOST_LOG(verbose) << "VideoToolbox: start encode #" << frame_ref->frame_nr
                     << std::fixed << std::setprecision(3)
                     << " pts " << CMTimeGetSeconds(pts)
                     << " (+" << ((double)CMTimeGetSeconds(CMTimeSubtract(pts, last_pts)) * 1000.0) << " ms"
                     << (pts_is_real ? "" : ", synthetic") << ")"
                     << (idr ? " req IDR" : "");

    last_pts = pts;
    last_frame_tick = std::chrono::steady_clock::now();

    // The callback owns frame_ref from the moment the frame is accepted; it may
    // run before EncodeFrame even returns, so release before the call
    auto *ref = frame_ref.release();

    VTEncodeInfoFlags info_flags = 0;
    OSStatus err = VTCompressionSessionEncodeFrame(session,
                                                   pixbuf,
                                                   pts,
                                                   kCMTimeInvalid,
                                                   frame_properties,
                                                   ref,
                                                   &info_flags);
    if (frame_properties) {
      CFRelease(frame_properties);
    }
    if (err != noErr) {
      // The output callback is not invoked for frames that fail submission
      delete ref;
      BOOST_LOG(error) << "VideoToolbox error: VTCompressionSessionEncodeFrame failed: " << ca::Status(err);
      return -1;
    }

    force_idr.store(false);
    return 0;
  }

  bool videotoolbox_encode_session_t::is_vt_property_supported(CFStringRef key) {
    if (!supported_keys) {
      if (VTSessionCopySupportedPropertyDictionary(session, &supported_keys) != noErr) {
        BOOST_LOG(error) << "VideoToolbox: couldn't get supported properties";
        return false;
      }
    }
    return supported_keys && CFDictionaryContainsKey(supported_keys, key);
  }

  bool videotoolbox_encode_session_t::set_vt_property(CFStringRef key, int32_t value) {
    CFNumberRef cfvalue = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
    OSStatus err = VTSessionSetProperty(session, key, cfvalue);
    if (err != noErr) {
      BOOST_LOG(debug) << "VTSessionSetProperty failed for " << cf_string_to_std_string(key) << "(" << value << "): " << ca::Status(err);
    } else {
      BOOST_LOG(debug) << cf_string_to_std_string(key) << ": " << value;
    }
    CFRelease(cfvalue);
    return err == noErr;
  }

  bool videotoolbox_encode_session_t::set_vt_property(CFStringRef key, bool value) {
    CFBooleanRef cfvalue = (value) ? kCFBooleanTrue : kCFBooleanFalse;
    OSStatus err = VTSessionSetProperty(session, key, cfvalue);
    if (err != noErr) {
      BOOST_LOG(debug) << "VTSessionSetProperty failed for " << cf_string_to_std_string(key) << "(" << value << "): " << ca::Status(err);
    } else {
      BOOST_LOG(debug) << cf_string_to_std_string(key) << ": " << (value ? "true" : "false");
    }
    return err == noErr;
  }

  bool videotoolbox_encode_session_t::set_vt_property(CFStringRef key, double value) {
    CFNumberRef cfvalue = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType , &value);
    OSStatus err = VTSessionSetProperty(session, key, cfvalue);
    if (err != noErr) {
      BOOST_LOG(debug) << "VTSessionSetProperty failed for " << cf_string_to_std_string(key)
                       << "(" << std::fixed << std::setprecision(3) << value << "): " << ca::Status(err);
    } else {
      BOOST_LOG(debug) << cf_string_to_std_string(key) << ": " << std::fixed << std::setprecision(3) << value;
    }
    CFRelease(cfvalue);
    return err == noErr;
  }

  bool videotoolbox_encode_session_t::set_vt_property(CFStringRef key, CFStringRef value) {
    OSStatus err = VTSessionSetProperty(session, key, value);
    if (err != noErr) {
      BOOST_LOG(debug) << "VTSessionSetProperty failed for " << cf_string_to_std_string(key) << "(" << cf_string_to_std_string(value) << "): " << ca::Status(err);
    } else {
      BOOST_LOG(debug) << cf_string_to_std_string(key) << ": " << cf_string_to_std_string(value);
    }
    return err == noErr;
  }

  bool videotoolbox_encode_session_t::set_vt_property(CFStringRef key, CFDataRef value) {
    CFTypeRef keys[1] = {key};
    CFTypeRef values[1] = {value};
    CFDictionaryRef session_properties = CFDictionaryCreate(
      kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    OSStatus err = VTSessionSetProperties(session, session_properties);
    if (err != noErr) {
      BOOST_LOG(debug) << "VTSessionSetProperties failed for " << cf_string_to_std_string(key) << "(" << cf_desc_to_std_string(value) << "): " << ca::Status(err);
    } else {
      BOOST_LOG(debug) << cf_string_to_std_string(key) << ": " << cf_desc_to_std_string(value);
    }
    CFRelease(session_properties);
    return err == noErr;
  }

}  // namespace video
