/**
 * @file src/platform/macos/sc_video.h
 * @brief Declarations for ScreenCaptureKit-based video capture on macOS.
 *
 * Modern replacement for AVCaptureScreenInput (which was deprecated in
 * macOS 13). SCVideo conforms to the same SunshineVideoCapture protocol
 * as the legacy AVVideo class so callers can swap implementations at
 * runtime based on @available(macOS 12.3, *) without other code changes.
 */
#pragma once

#import "av_video.h"

#import <AppKit/AppKit.h>

API_AVAILABLE(macos(12.3))
@interface SCVideo: NSObject <SunshineVideoCapture>

@property (nonatomic, assign) CGDirectDisplayID displayID;
@property (nonatomic, assign) CMTime minFrameDuration;
@property (nonatomic, assign) OSType pixelFormat;
@property (nonatomic, assign) int frameWidth;
@property (nonatomic, assign) int frameHeight;

- (instancetype)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate;

- (void)setFrameWidth:(int)frameWidth frameHeight:(int)frameHeight;
- (dispatch_semaphore_t)capture:(FrameCallbackBlock)frameCallback;

@end
