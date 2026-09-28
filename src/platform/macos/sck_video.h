/**
 * @file src/platform/macos/sck_video.h
 * @brief ScreenCaptureKit video capture, heavily inspired by OBS's SCK plugin.
 */
#pragma once

#import <AppKit/AppKit.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <atomic>
#include <chrono>
#include "threading.h"

// Avoid conflict between system frameworks and libavutil both defining AVMediaType
#define AVMediaType AVMediaType_FFmpeg
#include "src/video.h"
#undef AVMediaType

typedef enum {
  ScreenCaptureDisplayStream = 0,
  ScreenCaptureWindowStream = 1,
  ScreenCaptureApplicationStream = 2,
} ScreenCaptureStreamType;

typedef enum {
  ScreenCaptureAudioDesktopStream = 0,
  ScreenCaptureAudioApplicationStream = 1,
} ScreenCaptureAudioStreamType;

@interface ScreenCaptureDelegate : NSObject <SCStreamOutput, SCStreamDelegate, SCContentSharingPickerObserver>

@property struct screen_capture *sc;

@end

// This data structure is inspired by OBS
struct screen_capture {
  NSRect frame;
  bool show_cursor;
  bool show_hidden_windows;
  bool show_empty_names;

  bool capture_audio;
  bool audio_only;
  int audio_channels;

  SCStream *stream;
  SCStreamConfiguration *stream_properties;
  SCShareableContent *shareable_content;
  SCContentSharingPicker *picker;
  ScreenCaptureDelegate *capture_delegate;

  os_event_t *stream_start_completed;
  os_event_t *frame_ready;
  os_sem_t *shareable_content_available;
  dispatch_queue_t video_queue;
  dispatch_queue_t audio_queue;
  CMSampleBufferRef current, prev;
  bool capture_failed;
  bool vsync_disabled;

  pthread_mutex_t mutex;

  ScreenCaptureStreamType capture_type;
  ScreenCaptureAudioStreamType audio_capture_type;
  bool software_encoder;
  int width;
  int height;
  video::sunshine_colorspace_t colorspace;
  bool chroma444;
  bool capture_interval_is_refresh_rate;
  AVRational fps;
  NSString *application_id;
  CGDirectDisplayID display_id;
  AVRational display_refresh_rate;
  CGWindowID window_id;

  std::atomic<bool> is_hdr{true};
};

void sck_video_capture_destroy(struct screen_capture *sc);
struct screen_capture *sck_video_capture_create(platf::mem_type_e hwdevice_type, const std::string &capture_target, const video::config_t &vconfig);
void screen_capture_build_content_list(struct screen_capture *sc, bool display_capture);
bool build_display_list(struct screen_capture *sc, const std::string &display_name);
bool build_window_list(struct screen_capture *sc, const std::string &window_name);
bool build_application_list(struct screen_capture *sc, const std::string &app_name);
void screen_stream_video_update(struct screen_capture *sc, CMSampleBufferRef sample_buffer);
void screen_stream_audio_update(struct screen_capture *sc, CMSampleBufferRef sample_buffer);
CMSampleBufferRef sck_get_latest_sample_buffer(struct screen_capture *sc, std::chrono::milliseconds timeout);
void sck_set_show_cursor(struct screen_capture *sc, bool visible);
void sck_set_frame_size(struct screen_capture *sc, int width, int height);
void set_quartz_vsync(bool enable) ;
