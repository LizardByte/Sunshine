/**
 * @file src/platform/macos/sc_capture.h
 * @brief Declarations for ScreenCaptureKit-based display capture on macOS.
 */
#pragma once

#import <AppKit/AppKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

API_AVAILABLE(macos(12.3))
@interface SCCapture: NSObject <SCStreamDelegate, SCStreamOutput>

#define kMaxDisplays 32

@property (nonatomic, assign) CGDirectDisplayID displayID;
@property (nonatomic, assign) int frameRate;
@property (nonatomic, assign) OSType pixelFormat;
@property (nonatomic, assign) int frameWidth;
@property (nonatomic, assign) int frameHeight;

@property (nonatomic, strong) SCStream *stream;
@property (nonatomic, strong) SCContentFilter *contentFilter;
@property (nonatomic, strong) SCStreamConfiguration *streamConfiguration;
@property (nonatomic, strong) SCShareableContent *shareableContent;
@property (nonatomic, strong) dispatch_queue_t videoQueue;

@property (nonatomic, strong) dispatch_semaphore_t captureSignal;
@property (nonatomic, strong) dispatch_semaphore_t frameSignal;
@property (nonatomic, assign) BOOL stopping;
@property (nonatomic, assign) CMSampleBufferRef latestSampleBuffer;

+ (BOOL)isAvailable;
+ (NSArray<NSDictionary *> *)displayNames;
+ (NSString *)getDisplayName:(CGDirectDisplayID)displayID;

- (instancetype)initWithDisplay:(CGDirectDisplayID)displayID
                      frameRate:(int)frameRate;

- (void)setFrameWidth:(int)frameWidth frameHeight:(int)frameHeight;
- (dispatch_semaphore_t)captureVideo;
- (CMSampleBufferRef)copyLatestSampleBuffer;
- (CMSampleBufferRef)copyScreenshotSampleBuffer;
- (void)stopCapture;

@end
