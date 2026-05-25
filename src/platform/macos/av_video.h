/**
 * @file src/platform/macos/av_video.h
 * @brief Declarations for video capture on macOS.
 */
#pragma once

// platform includes
#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>

struct CaptureSession {
  AVCaptureVideoDataOutput *output;
  NSCondition *captureStopped;
};

static const int kMaxDisplays = 32;

typedef bool (^FrameCallbackBlock)(CMSampleBufferRef);

/**
 * @brief Shared interface for macOS screen capture backends.
 *
 * Both the legacy AVCaptureScreenInput-based implementation (AVVideo) and
 * the modern ScreenCaptureKit-based implementation (SCVideo) conform to
 * this protocol so display.mm can hold either behind a single pointer
 * type and branch on macOS version at construction.
 */
@protocol SunshineVideoCapture <NSObject>

@property (nonatomic, assign) CGDirectDisplayID displayID;
@property (nonatomic, assign) CMTime minFrameDuration;
@property (nonatomic, assign) OSType pixelFormat;
@property (nonatomic, assign) int frameWidth;
@property (nonatomic, assign) int frameHeight;

- (void)setFrameWidth:(int)frameWidth frameHeight:(int)frameHeight;
- (dispatch_semaphore_t)capture:(FrameCallbackBlock)frameCallback;

@end

@interface AVVideo: NSObject <AVCaptureVideoDataOutputSampleBufferDelegate, SunshineVideoCapture>

@property (nonatomic, assign) CGDirectDisplayID displayID;
@property (nonatomic, assign) CMTime minFrameDuration;
@property (nonatomic, assign) OSType pixelFormat;
@property (nonatomic, assign) int frameWidth;
@property (nonatomic, assign) int frameHeight;

@property (nonatomic, assign) AVCaptureSession *session;
@property (nonatomic, assign) NSMapTable<AVCaptureConnection *, AVCaptureVideoDataOutput *> *videoOutputs;
@property (nonatomic, assign) NSMapTable<AVCaptureConnection *, FrameCallbackBlock> *captureCallbacks;
@property (nonatomic, assign) NSMapTable<AVCaptureConnection *, dispatch_semaphore_t> *captureSignals;

+ (NSArray<NSDictionary *> *)displayNames;
+ (NSString *)getDisplayName:(CGDirectDisplayID)displayID;

- (id)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate;

- (void)setFrameWidth:(int)frameWidth frameHeight:(int)frameHeight;
- (dispatch_semaphore_t)capture:(FrameCallbackBlock)frameCallback;

@end
