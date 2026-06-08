/**
 * @file src/platform/macos/sckit_video.h
 * @brief Declarations for ScreenCaptureKit display capture on macOS.
 */
#pragma once

// platform includes
#import <AppKit/AppKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

static const int kMaxSCKitDisplays = 32;

typedef bool (^SCKitFrameCallbackBlock)(CMSampleBufferRef);

@interface SCKitVideo: NSObject <SCStreamOutput>

@property(nonatomic, assign) CGDirectDisplayID displayID;
@property(nonatomic, assign) OSType pixelFormat;
@property(nonatomic, assign) int frameWidth;
@property(nonatomic, assign) int frameHeight;
@property(nonatomic, assign) int requestedFrameRate;

@property(nonatomic, retain) SCDisplay *display;
@property(nonatomic, retain) SCStream *stream;
@property(nonatomic, copy) SCKitFrameCallbackBlock captureCallback;
@property(nonatomic, assign) dispatch_semaphore_t captureSignal;
@property(nonatomic, retain) NSError *captureError;
@property(nonatomic, assign) NSUInteger deliveredFrames;
@property(nonatomic, assign) NSUInteger completeFrames;
@property(nonatomic, assign) NSUInteger startedFrames;
@property(nonatomic, assign) NSUInteger idleFrames;
@property(nonatomic, assign) NSUInteger blankFrames;
@property(nonatomic, assign) NSUInteger suspendedFrames;
@property(nonatomic, assign) NSUInteger stoppedFrames;
@property(nonatomic, assign) NSUInteger unknownStatusFrames;
@property(nonatomic, assign) NSUInteger noImageFrames;
@property(nonatomic, assign) NSUInteger invalidFrames;
@property(nonatomic, assign) NSTimeInterval lastFrameReportTime;

+ (NSArray<NSDictionary *> *)displayNames;
+ (NSString *)getDisplayName:(CGDirectDisplayID)displayID;

- (id)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate;

- (void)setFrameWidth:(int)frameWidth frameHeight:(int)frameHeight;
- (dispatch_semaphore_t)capture:(SCKitFrameCallbackBlock)frameCallback;

@end
