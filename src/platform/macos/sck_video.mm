/**
 * @file src/platform/macos/sck_video.mm
 * @brief ScreenCaptureKit video capture, heavily inspired by OBS's SCK plugin.
 */

#import "sck_video.h"

#include "bmem.h"
#include "cf_helpers.h"
#include "qpc_chrono.h"
#include "sck_picker.h"
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/macos/coreaudio_helpers.h"

#include <dlfcn.h>
#include <mutex>
#include <pthread.h>

// Avoid conflict between system frameworks and libavutil both defining AVMediaType
#define AVMediaType AVMediaType_FFmpeg
#include "src/video.h"
#undef AVMediaType

using namespace std::literals;

// The active capture, so sck_present_picker() can reach it from the confighttp server
static std::mutex active_capture_mutex;
static struct screen_capture *active_capture = nullptr;

uint32_t FramesIn = 0;
uint32_t FramesIdle = 0;
uint32_t LastFramesIn = 0;
uint32_t FramesOut = 0;
uint32_t FramesDropped = 0;
std::chrono::steady_clock::time_point LastCheckpoint {};

static AVRational get_display_refresh_rate(CGDirectDisplayID displayID) {
  AVRational result {60, 1}; // fallback rate

  if (displayID == 0) {
    displayID = CGMainDisplayID();
  }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  // CVDisplayLink is deprecated but I can't find another API with precise numerator/denominator
  CVDisplayLinkRef displayLink = nullptr;
  CVReturn err = CVDisplayLinkCreateWithCGDisplay(displayID, &displayLink);
  if (err != kCVReturnSuccess || displayLink == nullptr) {
    BOOST_LOG(error) << "Failed to get display refresh rate (couldn't create CVDisplayLink: " << err << ")";
    return result;
  }

  CVTime period = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(displayLink);
  CVDisplayLinkRelease(displayLink);

  if ((period.flags & kCVTimeIsIndefinite) || period.timeValue <= 0 || period.timeScale <= 0) {
    return result;
  }
#pragma clang diagnostic pop

  // NTSC 59.94 is represented on macOS as 24000000/400400, so try to reduce it
  av_reduce(&result.num, &result.den, period.timeScale, period.timeValue, 120000);
  return result;
}

static void destroy_screen_stream(struct screen_capture *sc) {
  if (sc->stream && !sc->capture_failed) {
    [sc->stream stopCaptureWithCompletionHandler:^(NSError *_Nullable nsError) {
      if (nsError && nsError.code != SCStreamErrorAttemptToStopStreamState) {
        BOOST_LOG(error) << "destroy_screen_stream: Failed to stop stream with error "
                         << [[nsError localizedFailureReason] cStringUsingEncoding:NSUTF8StringEncoding];
      }
    }];
  }

  if (sc->stream_properties) {
    [sc->stream_properties release];
    sc->stream_properties = NULL;
  }

  if (sc->current) {
    CFRelease(sc->current);
    sc->current = NULL;
  }

  if (sc->prev) {
    CFRelease(sc->prev);
    sc->prev = NULL;
  }

  if (sc->stream) {
    [sc->stream release];
    sc->stream = NULL;
  }

  if (sc->vsync_disabled) {
    set_quartz_vsync(true);
    sc->vsync_disabled = false;
  }

  os_event_destroy(sc->stream_start_completed);
}

void sck_video_capture_destroy(struct screen_capture *sc) {
  if (!sc) {
    return;
  }

  {
    std::lock_guard lock(active_capture_mutex);
    if (active_capture == sc) {
      active_capture = nullptr;
    }
  }

  if (@available(macOS 14.0, *)) {
    if (sc->picker) {
      if (sc->capture_delegate) {
        [sc->picker removeObserver:sc->capture_delegate];
      }
      sc->picker = nil;
    }
  }

  destroy_screen_stream(sc);

  if (sc->shareable_content) {
    os_sem_wait(sc->shareable_content_available);
    [sc->shareable_content release];
    os_sem_destroy(sc->shareable_content_available);
    sc->shareable_content_available = NULL;
  }

  if (sc->capture_delegate) {
    [sc->capture_delegate release];
  }
  [sc->application_id release];

  if (sc->video_queue) {
    dispatch_release(sc->video_queue);
  }
  if (sc->audio_queue) {
    dispatch_release(sc->audio_queue);
  }

  os_event_destroy(sc->frame_ready);
  pthread_mutex_destroy(&sc->mutex);
  bfree(sc);
}

static bool init_screen_stream(struct screen_capture *sc) {
  SCContentFilter *content_filter;
  if (sc->capture_failed) {
    sc->capture_failed = false;
  }

  sc->frame = CGRectZero;
  sc->stream_properties = [[SCStreamConfiguration alloc] init];
  os_sem_wait(sc->shareable_content_available);

  SCDisplay * (^get_target_display)(void) = ^SCDisplay * {
    for (SCDisplay *display in sc->shareable_content.displays) {
      if (display.displayID == sc->display_id) {
        return display;
      }
    }
    return nil;
  };

  void (^set_display_mode)(struct screen_capture *, SCDisplay *) =
    ^void(struct screen_capture *capture_data, SCDisplay *target_display) {
      CGDisplayModeRef display_mode = CGDisplayCopyDisplayMode(target_display.displayID);
      size_t pixel_width = CGDisplayModeGetPixelWidth(display_mode);
      size_t pixel_height = CGDisplayModeGetPixelHeight(display_mode);
      CGDisplayModeRelease(display_mode);

      // [capture_data->stream_properties setWidth:pixel_width];
      // [capture_data->stream_properties setHeight:pixel_height];
      capture_data->frame.size.width = pixel_width;
      capture_data->frame.size.height = pixel_height;
      capture_data->display_refresh_rate = get_display_refresh_rate(target_display.displayID);

      BOOST_LOG(debug) << "ScreenCaptureKit: capturing target display " << (int) target_display.displayID
                       << ": " << pixel_width << "x" << pixel_height;
    };

  switch (sc->capture_type) {
    case ScreenCaptureDisplayStream:
      {
        SCDisplay *target_display = get_target_display();
        if (target_display == nil) {
          BOOST_LOG(error) << "init_screen_stream: Invalid target display ID: " << sc->display_id;
          os_sem_post(sc->shareable_content_available);
          sc->stream = NULL;
          os_event_init(&sc->stream_start_completed, OS_EVENT_TYPE_MANUAL);
          return false;
        }
        NSArray *empty = [[NSArray alloc] init];
        content_filter = [[SCContentFilter alloc] initWithDisplay:target_display excludingWindows:empty];
        [empty release];

        set_display_mode(sc, target_display);
      }
      break;
    case ScreenCaptureWindowStream:
      {
        SCWindow *target_window = nil;
        if (sc->window_id != kCGNullWindowID) {
          for (SCWindow *window in sc->shareable_content.windows) {
            if (window.windowID == sc->window_id) {
              target_window = window;
              break;
            }
          }
        }
        if (target_window == nil) {
          BOOST_LOG(error) << "init_screen_stream: Invalid target window ID: " << sc->window_id;
          os_sem_post(sc->shareable_content_available);
          sc->stream = NULL;
          os_event_init(&sc->stream_start_completed, OS_EVENT_TYPE_MANUAL);
          return false;
        } else {
          content_filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:target_window];

          // [sc->stream_properties setWidth:(size_t) target_window.frame.size.width];
          // [sc->stream_properties setHeight:(size_t) target_window.frame.size.height];
          sc->frame.size = target_window.frame.size;

          if (@available(macOS 14.2, *)) {
            [sc->stream_properties setIncludeChildWindows:YES];
          }

          BOOST_LOG(debug) << "ScreenCaptureKit: capturing window " << (int) sc->window_id
                           << ": " << target_window.frame.size.width << "x" << target_window.frame.size.height;
        }
      }
      break;
    case ScreenCaptureApplicationStream:
      {
        SCDisplay *target_display = get_target_display();
        if (target_display == nil) {
          BOOST_LOG(error) << "init_screen_stream: Invalid target display ID: " << sc->display_id;
          os_sem_post(sc->shareable_content_available);
          sc->stream = NULL;
          os_event_init(&sc->stream_start_completed, OS_EVENT_TYPE_MANUAL);
          return false;
        }
        SCRunningApplication *target_application = nil;
        for (SCRunningApplication *application in sc->shareable_content.applications) {
          BOOST_LOG(debug) << "  " << application.bundleIdentifier.UTF8String;
          if ([application.bundleIdentifier isEqualToString:sc->application_id]) {
            target_application = application;
            break;
          }
        }
        NSArray *target_application_array = [[NSArray alloc] initWithObjects:target_application, nil];
        NSArray *empty_array = [[NSArray alloc] init];
        content_filter = [[SCContentFilter alloc] initWithDisplay:target_display
                                            includingApplications:target_application_array
                                                 exceptingWindows:empty_array];
        if (@available(macOS 14.2, *)) {
          content_filter.includeMenuBar = YES;
        }

        [target_application_array release];
        [empty_array release];

        set_display_mode(sc, target_display);
      }
      break;
  }
  os_sem_post(sc->shareable_content_available);

  CGColorRef background = CGColorGetConstantColor(kCGColorClear);
  [sc->stream_properties setWidth:sc->width];
  [sc->stream_properties setHeight:sc->height];
  // queueDepth refers to how many buffers we can retain, and is not related to latency
  [sc->stream_properties setQueueDepth:5];
  [sc->stream_properties setShowsCursor:sc->show_cursor];
  [sc->stream_properties setBackgroundColor:background];
  if (@available(macOS 14.0, *)) {
    [sc->stream_properties setCaptureResolution:SCCaptureResolutionNominal];
  }
  [sc->stream_properties setPreservesAspectRatio:YES];
  [sc->stream_properties setScalesToFit:YES];

  CMTime interval;
  if (sc->capture_interval_is_refresh_rate) {
    // This needs more testing, setting capture rate to refresh rate.
    interval = CMTimeMake((int64_t) sc->display_refresh_rate.den, sc->display_refresh_rate.num);
    [sc->stream_properties setMinimumFrameInterval:interval];
  } else {
    // Default is to capture at 110% of the requested FPS. This is borrowed from OBS, a little headroom is needed
    // to ensure the capture doesn't miss frames.
    interval = CMTimeMake((int64_t) sc->fps.den, sc->fps.num);
    CMTime minimumUpdateTime = CMTimeMultiplyByFloat64(interval, 0.9);
    [sc->stream_properties setMinimumFrameInterval:minimumUpdateTime];
  }

  BOOST_LOG(info) << "ScreenCaptureKit capturing with minimumFrameInterval "
                  << std::setprecision(3) << (CMTimeGetSeconds(interval) * 1000.0)
                  << "ms (" << (1.0 / CMTimeGetSeconds(interval)) << " fps)";

  if (sc->software_encoder) {
    [sc->stream_properties setPixelFormat:kCVPixelFormatType_32BGRA];
  } else {
    if (sc->chroma444) {
      [sc->stream_properties setPixelFormat:sc->colorspace.full_range
        ? (sc->colorspace.bit_depth == 10 ? kCVPixelFormatType_444YpCbCr10BiPlanarFullRange : kCVPixelFormatType_444YpCbCr8BiPlanarFullRange)
        : (sc->colorspace.bit_depth == 10 ? kCVPixelFormatType_444YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_444YpCbCr8BiPlanarVideoRange)];
    } else {
      [sc->stream_properties setPixelFormat:sc->colorspace.full_range
        ? (sc->colorspace.bit_depth == 10 ? kCVPixelFormatType_420YpCbCr10BiPlanarFullRange : kCVPixelFormatType_420YpCbCr8BiPlanarFullRange)
        : (sc->colorspace.bit_depth == 10 ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)];
    }
  }

  if (sc->colorspace.colorspace == video::colorspace_e::bt2020) {
    // "Capture HDR content with ScreenCaptureKit" https://developer.apple.com/videos/play/wwdc2024/10088
    SCCaptureDynamicRange dynamic_range;
    switch (config::video.macos_capture_dynamic_range) {
      case config::video_t::macos_capture_dynamic_range_e::sdr:
        dynamic_range = SCCaptureDynamicRangeSDR;
        break;
      case config::video_t::macos_capture_dynamic_range_e::hdr_local:
        dynamic_range = SCCaptureDynamicRangeHDRLocalDisplay;
        break;
      default:
        dynamic_range = SCCaptureDynamicRangeHDRCanonicalDisplay;
        break;
    }
    if (@available(macOS 15.0, *)) {
      [sc->stream_properties setCaptureDynamicRange:dynamic_range];
    }
    [sc->stream_properties setColorSpaceName:kCGColorSpaceDisplayP3_PQ];
    [sc->stream_properties setColorMatrix:kCVImageBufferYCbCrMatrix_ITU_R_709_2];
  } else {
    if (@available(macOS 15.0, *)) {
      [sc->stream_properties setCaptureDynamicRange:SCCaptureDynamicRangeSDR];
    }
    [sc->stream_properties setColorSpaceName:kCGColorSpaceITUR_709];
    [sc->stream_properties setColorMatrix:kCVImageBufferYCbCrMatrix_ITU_R_709_2];
  }

  BOOST_LOG(info) << "ScreenCaptureKit using pixel format " << ca::Status(sc->stream_properties.pixelFormat)
                  << " for chroma " << (sc->chroma444 ? "4:4:4" : "4:2:0");

  if (config::video.macos_disable_vsync) {
    set_quartz_vsync(false);
    sc->vsync_disabled = true;
  }

  if (sc->capture_audio) {
    [sc->stream_properties setCapturesAudio:YES];
    [sc->stream_properties setExcludesCurrentProcessAudio:YES];
    [sc->stream_properties setChannelCount:2]; // SCK is stereo only
    [sc->stream_properties setSampleRate:48000];

    if (sc->audio_only) {
      // We still have to capture some video, but we can make it very lightweight
      [sc->stream_properties setMinimumFrameInterval:CMTimeMake(5, 1)];
      if (@available(macOS 15.0, *)) {
        [sc->stream_properties setCaptureDynamicRange:SCCaptureDynamicRangeSDR];
      }
      [sc->stream_properties setWidth:1280];
      [sc->stream_properties setHeight:720];
    }
  }

  sc->stream = [[SCStream alloc] initWithFilter:content_filter
                                configuration:sc->stream_properties
                                     delegate:sc->capture_delegate];
  [content_filter release];

  os_event_init(&sc->stream_start_completed, OS_EVENT_TYPE_MANUAL);

  NSError *addStreamOutputError = nil;
  BOOL did_add_output = [sc->stream addStreamOutput:sc->capture_delegate
                                               type:SCStreamOutputTypeScreen
                                 sampleHandlerQueue:sc->video_queue
                                              error:&addStreamOutputError];
  if (!did_add_output) {
    BOOST_LOG(error) << "ScreenCaptureKit: Failed to add video stream output with error: "
                     << [[addStreamOutputError localizedFailureReason] cStringUsingEncoding:NSUTF8StringEncoding];
    return !did_add_output;
  }

  if (sc->capture_audio) {
    did_add_output = [sc->stream addStreamOutput:sc->capture_delegate
                                            type:SCStreamOutputTypeAudio
                              sampleHandlerQueue:sc->audio_queue
                                           error:&addStreamOutputError];
    if (!did_add_output) {
      BOOST_LOG(error) << "ScreenCaptureKit: Failed to add audio stream output with error: "
                       << [[addStreamOutputError localizedFailureReason] cStringUsingEncoding:NSUTF8StringEncoding];
      return !did_add_output;
    }
  }

  if (@available(macOS 14.0, *)) {
    // A picker needs to be configured so the user can change captured content from the menu bar.
    // This picker is also used to display the picker UI from the web UI.
    sc->picker = [SCContentSharingPicker sharedPicker];
    [sc->picker addObserver:sc->capture_delegate];
    sc->picker.active = YES;

    SCContentSharingPickerConfiguration* picker_config = [sc->picker defaultConfiguration];
    picker_config.allowsChangingSelectedContent = YES;

    // prevent capture of Sunshine itself
    NSMutableArray<NSString*>* arr = [NSMutableArray array];
    [arr addObject:[NSString stringWithFormat:@"%s", PROJECT_FQDN]];
    picker_config.excludedBundleIDs = arr;
    [sc->picker setConfiguration:picker_config forStream:sc->stream];
  }

  __block BOOL did_stream_start = NO;
  [sc->stream startCaptureWithCompletionHandler:^(NSError *_Nullable nsError) {
    did_stream_start = (BOOL) (nsError == nil);
    if (!did_stream_start) {
      BOOST_LOG(error) << "init_screen_stream: Failed to start capture with error: "
                       << [[nsError localizedFailureReason] cStringUsingEncoding:NSUTF8StringEncoding];
      [sc->stream release];
      sc->stream = NULL;
    }
    os_event_signal(sc->stream_start_completed);
  }];
  os_event_wait(sc->stream_start_completed);

  return did_stream_start;
}

struct screen_capture *sck_video_capture_create(platf::mem_type_e hwdevice_type, const std::string &capture_target, const video::config_t &vconfig) {
  struct screen_capture *sc = (struct screen_capture *) bzalloc(sizeof(struct screen_capture));

  sc->show_cursor = true;
  sc->show_empty_names = false;
  sc->show_hidden_windows = false;
  // Default to full-display capture of the main display. build_display_list() will
  // override display_id if capture_target matches a specific display.
  sc->display_id = CGMainDisplayID();
  sc->window_id = kCGNullWindowID;
  sc->application_id = nil;
  sc->capture_type = ScreenCaptureDisplayStream;
  sc->software_encoder = hwdevice_type == platf::mem_type_e::system;
  sc->width = vconfig.width;
  sc->height = vconfig.height;
  sc->capture_interval_is_refresh_rate = false; // when false, captures at the requested FPS

  sc->capture_delegate = [[ScreenCaptureDelegate alloc] init];
  sc->capture_delegate.sc = sc;

  sc->capture_audio = false; // for future use
  sc->audio_only = false; // for future use

  os_sem_init(&sc->shareable_content_available, 1);
  os_event_init(&sc->frame_ready, OS_EVENT_TYPE_AUTO);
  pthread_mutex_init(&sc->mutex, NULL);

  // Match user's choice of capture to display/window/application
  screen_capture_build_content_list(sc, sc->capture_type == ScreenCaptureDisplayStream);
  build_display_list(sc, capture_target);
  // Don't log a list of all windows or applications
  //build_window_list(sc, capture_target);
  build_application_list(sc, capture_target);

  BOOST_LOG(debug) << "SC capture_type " << sc->capture_type
                   << " display_id " << sc->display_id
                   << " application_id " << sc->application_id ? sc->application_id.UTF8String : "<nil>";

  sc->chroma444 = vconfig.chromaSamplingType == 1;
  sc->capture_interval_is_refresh_rate = false;
  sc->colorspace = video::colorspace_from_client_config(vconfig, true);
  sc->fps = AVRational {vconfig.framerate, 1};
  if (vconfig.framerateX100 > 0) {
    sc->fps = video::framerateX100_to_rational(vconfig.framerateX100);
  }

  // audio is on a higher priority queue
  sc->video_queue = dispatch_queue_create("dev.lizardbyte.app.Sunshine.video", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, -1));
  sc->audio_queue = dispatch_queue_create("dev.lizardbyte.app.Sunshine.audio", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, -1));

  if (!init_screen_stream(sc)) {
    goto fail;
  }

  BOOST_LOG(debug) << "ScreenCaptureKit created: "
                   << sc->stream_properties.width << "x" << sc->stream_properties.height
                   << " @ " << av_q2d(sc->fps);

  {
    std::lock_guard lock(active_capture_mutex);
    active_capture = sc;
  }
  return sc;

fail:
  sck_video_capture_destroy(sc);
  return NULL;
}

@implementation ScreenCaptureDelegate

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
  if (self.sc != NULL) {
    if (type == SCStreamOutputTypeScreen && !self.sc->audio_only) {
      screen_stream_video_update(self.sc, sampleBuffer);
    } else if (@available(macOS 13.0, *)) {
      if (type == SCStreamOutputTypeAudio) {
        screen_stream_audio_update(self.sc, sampleBuffer);
      }
    }

  }
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)nsError {
  NSString *errorMessage;
  switch (nsError.code) {
    case SCStreamErrorUserStopped:
      errorMessage = @"ScreenCaptureKit: user stopped stream.";
      break;
    case SCStreamErrorNoCaptureSource:
      errorMessage = @"ScreenCaptureKit: stopped because no capture source was found.";
      break;
    default:
      errorMessage = [NSString stringWithFormat:@"ScreenCaptureKit: stopped with error %ld (\"%s\")", nsError.code, nsError.localizedDescription.UTF8String];
      break;
  }

  BOOST_LOG(warning) << errorMessage.UTF8String;

  pthread_mutex_lock(&self.sc->mutex);
  self.sc->capture_failed = true;
  os_event_signal(self.sc->frame_ready);
  pthread_mutex_unlock(&self.sc->mutex);
}

- (void) contentSharingPicker:(SCContentSharingPicker *)picker didCancelForStream:(SCStream *)stream API_AVAILABLE(macos(14.0)) {
  BOOST_LOG(info) << "SharingPicker: user cancelled, no changes were made";
}

- (void) contentSharingPicker:(SCContentSharingPicker *)picker
          didUpdateWithFilter:(SCContentFilter *)content_filter
                    forStream:(SCStream *)stream API_AVAILABLE(macos(14.0)) {

  [stream updateContentFilter:content_filter
            completionHandler:^(NSError *_Nullable nsError) {
              // Update capture_type so the correct handling is applied by screen_stream_video_update
              pthread_mutex_lock(&self.sc->mutex);
              switch (content_filter.style) {
                case SCShareableContentStyleApplication:
                  self.sc->capture_type = ScreenCaptureApplicationStream;
                  BOOST_LOG(info) << "ScreenCaptureKit: switching to Application capture mode";
                  break;
                case SCShareableContentStyleWindow:
                  self.sc->capture_type = ScreenCaptureWindowStream;
                  BOOST_LOG(info) << "ScreenCaptureKit: switching to Window capture mode";
                  break;
                case SCShareableContentStyleDisplay:
                default:
                  self.sc->capture_type = ScreenCaptureDisplayStream;
                  BOOST_LOG(info) << "ScreenCaptureKit: switching to Display capture mode";
                  break;
              }
              pthread_mutex_unlock(&self.sc->mutex);

              if (nsError) {
                BOOST_LOG(error) << "SCStream updateContentFilter: Failed to update content filter with error "sv
                                 << [[nsError localizedFailureReason] cStringUsingEncoding:NSUTF8StringEncoding];
              }
            }];
}

- (void) contentSharingPickerStartDidFailWithError:(NSError *)nsError API_AVAILABLE(macos(14.0)) {
  BOOST_LOG(error) << "SharingPicker: Failed with error "sv
                   << [[nsError localizedFailureReason] cStringUsingEncoding:NSUTF8StringEncoding];
}

@end

// based on screen_stream_video_update from OBS
void screen_stream_video_update(struct screen_capture *sc, CMSampleBufferRef sample_buffer) {
  bool frame_detail_errored = false;
  float scale_factor = 1.0f;
  CGRect window_rect = {};
  CMSampleBufferRef prev_current = NULL;

  if (!CMSampleBufferIsValid(sample_buffer)) {
    BOOST_LOG(debug) << "ScreenCaptureKit: invalid CMSampleBuffer, ignoring frame";
    return;
  }

  CFArrayRef attachments_array = CMSampleBufferGetSampleAttachmentsArray(sample_buffer, false);
  if (attachments_array == NULL || !CFArrayGetCount(attachments_array)) {
    BOOST_LOG(debug) << "ScreenCaptureKit: CMSampleBuffer attachments array is empty, ignoring frame";
    return;
  }
  CFDictionaryRef attachments_dict = (CFDictionaryRef) CFArrayGetValueAtIndex(attachments_array, 0);
  if (attachments_dict == NULL) {
    BOOST_LOG(debug) << "ScreenCaptureKit: CMSampleBuffer attachments dict is null, ignoring frame";
    return;
  }

  CFTypeRef frame_info_status = CFDictionaryGetValue(attachments_dict, SCStreamFrameInfoStatus);
  if (frame_info_status != NULL) {
    SCFrameStatus frame_status;
    Boolean result = CFNumberGetValue((CFNumberRef) frame_info_status, kCFNumberNSIntegerType, &frame_status);
    if (result == false) {
      BOOST_LOG(debug) << "ScreenCaptureKit: SCFrameStatus not found in CMSampleBuffer, ignoring frame";
      return;
    }
    switch (frame_status) {
      case SCFrameStatusStarted:
      case SCFrameStatusComplete:
        break;
      case SCFrameStatusIdle:
        ++FramesIdle;
        return;
      case SCFrameStatusBlank:
      case SCFrameStatusSuspended:
      case SCFrameStatusStopped:
      default:
        return;
    }
  }

  CGRect content_rect = {};
  CFTypeRef content_rect_dict = CFDictionaryGetValue(attachments_dict, SCStreamFrameInfoContentRect);
  if (content_rect_dict != NULL) {
    Boolean result = CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef) content_rect_dict, &content_rect);
    if (result == false) {
      content_rect = CGRectZero;
      frame_detail_errored = true;
    }
  }

  if (sc->capture_type == ScreenCaptureWindowStream) {
    CFTypeRef frame_scale_factor = CFDictionaryGetValue(attachments_dict, SCStreamFrameInfoScaleFactor);
    if (frame_scale_factor != NULL) {
      Boolean result = CFNumberGetValue((CFNumberRef) frame_scale_factor, kCFNumberFloatType, &scale_factor);
      if (result == false) {
        scale_factor = 1.0f;
        frame_detail_errored = true;
      }
    }

    CFTypeRef content_scale_factor = CFDictionaryGetValue(attachments_dict, SCStreamFrameInfoContentScale);
    if ((content_rect_dict != NULL) && (content_scale_factor != NULL)) {
      float points_to_pixels = 0.0f;
      Boolean result = CFNumberGetValue((CFNumberRef) content_scale_factor, kCFNumberFloatType, &points_to_pixels);
      if (result == false) {
        points_to_pixels = 1.0f;
        frame_detail_errored = true;
      }

      window_rect.origin = content_rect.origin;
      window_rect.size.width = content_rect.size.width / points_to_pixels * scale_factor;
      window_rect.size.height = content_rect.size.height / points_to_pixels * scale_factor;
    }
  }

  CVImageBufferRef image_buffer = CMSampleBufferGetImageBuffer(sample_buffer);
  if (image_buffer && !pthread_mutex_lock(&sc->mutex)) {
    bool needs_to_update_properties = false;

    if (!frame_detail_errored) {
      if (sc->capture_type == ScreenCaptureWindowStream) {
        if ((sc->frame.size.width != window_rect.size.width) || (sc->frame.size.height != window_rect.size.height)) {
          sc->frame.size.width = window_rect.size.width;
          sc->frame.size.height = window_rect.size.height;
          needs_to_update_properties = true;

          BOOST_LOG(info) << "ScreenCaptureKit: updating window capture, dimensions changed to "
                           << sc->frame.size.width << "x" << sc->frame.size.height;
        }
      }
      else if (sc->capture_type == ScreenCaptureDisplayStream) {
        // In Display mode, we expect to get a frame matching ScreenRect. If not, we probably switched
        // from a window capture mode
        CFTypeRef frame_screen_rect = CFDictionaryGetValue(attachments_dict, SCStreamFrameInfoScreenRect);
        if (frame_screen_rect != NULL) {
          CGRect screen_rect = {};
          Boolean result = CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef) frame_screen_rect, &screen_rect);
          if (result == false) {
            screen_rect = CGRectZero;
            frame_detail_errored = true;
          }
          window_rect.origin = content_rect.origin;
          if ((sc->frame.size.width != screen_rect.size.width) || (sc->frame.size.height != screen_rect.size.height)) {
            sc->frame.size.width = screen_rect.size.width;
            sc->frame.size.height = screen_rect.size.height;
            needs_to_update_properties = true;

            BOOST_LOG(info) << "ScreenCaptureKit: updating display capture, dimensions changed to "
                             << sc->frame.size.width << "x" << sc->frame.size.height;
          }
        }
      }
    }

    if (needs_to_update_properties) {
      // [sc->stream_properties setWidth:(size_t) sc->frame.size.width];
      // [sc->stream_properties setHeight:(size_t) sc->frame.size.height];

      [sc->stream updateConfiguration:sc->stream_properties
                    completionHandler:^(NSError *_Nullable nsError) {
                      if (nsError) {
                        BOOST_LOG(error) << "screen_stream_video_update: Failed to update stream properties with error "sv
                                       << [[nsError localizedFailureReason] cStringUsingEncoding:NSUTF8StringEncoding];
                      }
                    }];
    }

    prev_current = sc->current;
    sc->current = sample_buffer;
    CFRetain(sc->current);
    ++FramesIn;

    if (prev_current) {
      // if we have to remove an old frame from the queue, it wasn't consumed in time
      // and it's a dropped frame.
      ++FramesDropped;
    }

    // Wake any consumer blocked waiting for the next frame.
    os_event_signal(sc->frame_ready);

    if ((FramesIn % 240) == 0) {
      double framesInFPS = 0.0;
      auto now = std::chrono::steady_clock::now();
      if (LastCheckpoint != std::chrono::steady_clock::time_point {}) {
        framesInFPS = (FramesIn - LastFramesIn) / std::chrono::duration<double>(now - LastCheckpoint).count();
      }
      LastCheckpoint = now;

      BOOST_LOG(debug) << std::fixed << std::setprecision(2)
                       << "ScreenCaptureKit framesIn/Out/Idle/Dropped " << FramesIn << "/" << FramesOut << "/" << FramesIdle << "/" << FramesDropped
                       << " rateIn " << framesInFPS;

      LastFramesIn = FramesIn;
    }

    pthread_mutex_unlock(&sc->mutex);
  }

  if (prev_current) {
    CFRelease(prev_current);
  }
}

CMSampleBufferRef sck_get_latest_sample_buffer(struct screen_capture *sc, std::chrono::milliseconds timeout) {
  if (pthread_mutex_lock(&sc->mutex)) {
    return NULL;
  }
  bool ready = sc->current != NULL || sc->capture_failed;
  pthread_mutex_unlock(&sc->mutex);

  if (!ready) {
    os_event_timedwait(sc->frame_ready, timeout.count());
  }

  if (pthread_mutex_lock(&sc->mutex)) {
    return NULL;
  }

  if (!sc->current) {
    pthread_mutex_unlock(&sc->mutex);
    return NULL;
  }

  CMSampleBufferRef prev_prev = sc->prev;
  sc->prev = sc->current;
  sc->current = NULL;

  ++FramesOut;

  pthread_mutex_unlock(&sc->mutex);

  if (prev_prev) {
    CFRelease(prev_prev);
  }

  return sc->prev;
}

void sck_set_show_cursor(struct screen_capture *sc, bool visible) {
  if (sc->show_cursor == visible) {
    return;
  }

  if (pthread_mutex_lock(&sc->mutex)) {
    return;
  }

  sc->show_cursor = visible;
  [sc->stream_properties setShowsCursor:sc->show_cursor];

  BOOST_LOG(debug) << "sck_set_show_cursor " << visible;

  [sc->stream updateConfiguration:sc->stream_properties
                completionHandler:^(NSError *_Nullable nsError) {
                  if (nsError) {
                    BOOST_LOG(error) << "sck_set_show_cursor: Failed to update stream properties with error "sv
                                     << [[nsError localizedFailureReason] cStringUsingEncoding:NSUTF8StringEncoding];
                  }
                }];

  pthread_mutex_unlock(&sc->mutex);
}

void sck_set_frame_size(struct screen_capture *sc, int width, int height) {
  if (pthread_mutex_lock(&sc->mutex)) {
    return;
  }

  BOOST_LOG(debug) << "sck_set_frame_size " << width << "x" << height;

  if (sc->frame.size.width != width || sc->frame.size.height != height) {
    sc->frame.size.width = width;
    sc->frame.size.height = height;

    // To avoid a race condition, the updateConfiguration call is made by screen_stream_video_update
  }

  pthread_mutex_unlock(&sc->mutex);
}

bool sck_picker_available() {
  if (@available(macOS 14.0, *)) {
    return true;
  }
  return false;
}

bool sck_present_picker() {
  if (@available(macOS 14.0, *)) {
    {
      std::lock_guard lock(active_capture_mutex);
      if (!active_capture || !active_capture->stream) {
        BOOST_LOG(warning) << "sck_present_picker: no active capture stream";
        return false;
      }
    }

    BOOST_LOG(info) << "sck_present_picker: presenting content sharing picker";

    // I don't know if the picker can run on any thread, so let's play it safe
    dispatch_async(dispatch_get_main_queue(), ^{
      std::lock_guard lock(active_capture_mutex);
      struct screen_capture *sc = active_capture;
      if (!sc || !sc->stream) {
        BOOST_LOG(warning) << "sck_present_picker: no active capture";
        return;
      }

      [sc->picker presentPickerForStream:sc->stream];

      BOOST_LOG(info) << "sck_present_picker: now active";
    });

    return true;
  } else {
    return false;
  }
}

/// audio capture

void screen_stream_audio_update(struct screen_capture *sc, CMSampleBufferRef sample_buffer) {
  // TODO: this is the only way to capture audio from only the streamed apps/windows. The
  // Tap API works well but always captures everything.

  // log_sample_buffer(sample_buffer);
}

/// list handling for available capture targets

void screen_capture_build_content_list(struct screen_capture *sc, bool display_capture) {
  typedef void (^shareable_content_callback)(SCShareableContent *, NSError *);
  shareable_content_callback new_content_received = ^void(SCShareableContent *shareable_content, NSError *nsError) {
    if (nsError == nil && sc->shareable_content_available != NULL) {
      sc->shareable_content = [shareable_content retain];
    } else {
      BOOST_LOG(error) << "screen_capture_build_content_list: Failed to get shareable content with error "
                       << [[nsError localizedFailureReason] cStringUsingEncoding:NSUTF8StringEncoding];
    }
    os_sem_post(sc->shareable_content_available);
  };

  os_sem_wait(sc->shareable_content_available);
  [sc->shareable_content release];
  BOOL onScreenWindowsOnly = (display_capture) ? NO : !sc->show_hidden_windows;
  [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                             onScreenWindowsOnly:onScreenWindowsOnly
                                               completionHandler:new_content_received];
}

bool build_display_list(struct screen_capture *sc, const std::string &capture_target) {
  os_sem_wait(sc->shareable_content_available);

  BOOST_LOG(info) << "ScreenCaptureKit Shareable Displays:"sv;
  BOOST_LOG(debug) << "capture_target: " << capture_target;

  for (SCDisplay *display in sc->shareable_content.displays) {
    NSScreen *display_screen = nil;
    for (NSScreen *screen in NSScreen.screens) {
      NSNumber *screen_num = screen.deviceDescription[@"NSScreenNumber"];
      CGDirectDisplayID screen_display_id = (CGDirectDisplayID) screen_num.intValue;
      if (screen_display_id == display.displayID) {
        display_screen = screen;
        break;
      }
    }
    if (!display_screen) {
      continue;
    }

    CFUUIDRef display_uuid = CGDisplayCreateUUIDFromDisplayID(display.displayID);
    CFStringRef uuid_string = display_uuid ? CFUUIDCreateString(kCFAllocatorDefault, display_uuid) : NULL;

    char uuid_buffer[128] = {};
    if (uuid_string != NULL) {
      CFStringGetCString(uuid_string, uuid_buffer, sizeof(uuid_buffer), kCFStringEncodingUTF8);
    }

    char display_id_buffer[32] = {};
    snprintf(display_id_buffer, sizeof(display_id_buffer), "%u", (uint32_t) display.displayID);

    bool canHDR = display_screen.maximumPotentialExtendedDynamicRangeColorComponentValue > 1.0f;

    bool found_match = false;
    if (!capture_target.empty()) {
      found_match =
        capture_target == uuid_buffer ||
        capture_target == display_id_buffer ||
        capture_target == display_screen.localizedName.UTF8String;

      if (found_match) {
        sc->capture_type = ScreenCaptureDisplayStream;
        sc->display_id = display.displayID;
      }
    }

    BOOST_LOG(info) << display_screen.localizedName.UTF8String
                    << " (id: " << display.displayID << ") "
                    << (uint32_t) display_screen.frame.size.width << "x"
                    << (uint32_t) display_screen.frame.size.height << " @ "
                    << (int32_t) display_screen.frame.origin.x << ","
                    << (int32_t) display_screen.frame.origin.y
                    << " supports HDR: " << canHDR;

    if (uuid_string != NULL) {
      CFRelease(uuid_string);
    }

    if (display_uuid != NULL) {
      CFRelease(display_uuid);
    }
  }

  os_sem_post(sc->shareable_content_available);
  return true;
}

bool build_window_list(struct screen_capture *sc, const std::string &capture_target) {
  os_sem_wait(sc->shareable_content_available);

  BOOST_LOG(info) << "ScreenCaptureKit Shareable Windows:"sv;

  NSPredicate *filteredWindowPredicate =
    [NSPredicate predicateWithBlock:^BOOL(SCWindow *window, NSDictionary *bindings __unused) {
      NSString *app_name = window.owningApplication.applicationName;
      NSString *title = window.title;
      if (!sc->show_empty_names) {
        return (app_name.length > 0) && (title.length > 0);
      } else {
        return YES;
      }
    }];
  NSArray<SCWindow *> *filteredWindows;
  filteredWindows = [sc->shareable_content.windows filteredArrayUsingPredicate:filteredWindowPredicate];

  NSArray<SCWindow *> *sortedWindows;
  sortedWindows = [filteredWindows sortedArrayUsingComparator:^NSComparisonResult(SCWindow *window, SCWindow *other) {
    NSComparisonResult appNameCmp = [window.owningApplication.applicationName
      compare:other.owningApplication.applicationName
      options:NSCaseInsensitiveSearch];
    if (appNameCmp == NSOrderedAscending) {
      return NSOrderedAscending;
    } else if (appNameCmp == NSOrderedSame) {
      return [window.title compare:other.title options:NSCaseInsensitiveSearch];
    } else {
      return NSOrderedDescending;
    }
  }];

  for (SCWindow *window in sortedWindows) {
    NSString *app_name = window.owningApplication.applicationName;
    NSString *title = window.title;
    const char *list_text = [[NSString stringWithFormat:@"[%@] %@", app_name, title] UTF8String];
    BOOST_LOG(info) << "windowID="sv << window.windowID << " "sv << list_text;
  }

  os_sem_post(sc->shareable_content_available);
  return true;
}

bool build_application_list(struct screen_capture *sc, const std::string &capture_target) {
  os_sem_wait(sc->shareable_content_available);

  //BOOST_LOG(debug) << "ScreenCaptureKit Shareable Applications:"sv;

  NSArray<SCRunningApplication *> *filteredApplications;
  filteredApplications = [sc->shareable_content.applications
    filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(SCRunningApplication *app, NSDictionary *bindings __unused) {
      return app.applicationName.length > 0;
    }]];

  NSArray<SCRunningApplication *> *sortedApplications;
  sortedApplications = [filteredApplications
    sortedArrayUsingComparator:^NSComparisonResult(SCRunningApplication *app, SCRunningApplication *other) {
      return [app.applicationName compare:other.applicationName options:NSCaseInsensitiveSearch];
    }];

  for (SCRunningApplication *application in sortedApplications) {
    const char *bundle_id = [application.bundleIdentifier UTF8String];

    if (!capture_target.empty()) {
      if (capture_target == bundle_id) {
        sc->capture_type = ScreenCaptureApplicationStream;
        sc->application_id = application.bundleIdentifier;
        break;
      }
    }
  }

  os_sem_post(sc->shareable_content_available);
  return true;
}

typedef void (*set_int_t)(int);

void set_quartz_vsync(bool enable) {
  static bool initialized = false;
  static bool valid = false;
  static set_int_t set_debug_options = nullptr;
  static set_int_t deferred_updates = nullptr;

  if (!initialized) {
    void *quartzCore = dlopen(
      "/System/Library/Frameworks/"
      "QuartzCore.framework/QuartzCore",
      RTLD_LAZY
    );
    if (quartzCore) {
      set_debug_options = (set_int_t) dlsym(quartzCore, "CGSSetDebugOptions");
      deferred_updates = (set_int_t) dlsym(quartzCore, "CGSDeferredUpdates");

      valid = set_debug_options && deferred_updates;
    }

    if (!valid) {
      BOOST_LOG(warning) << "Unable to change macOS V-sync, stream may have reduced performancee.";
    }

    initialized = true;
  }

  if (valid) {
    set_debug_options(enable ? 0 : 0x08000000);
    deferred_updates(enable ? 1 : 0);

    BOOST_LOG(info) << "macOS V-sync " << (enable ? "enabled" : "disabled");
  }
}
