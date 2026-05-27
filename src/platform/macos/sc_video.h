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

// YES iff the negotiated streaming session enabled HDR (Moonlight's
// hdrMode flag). Required (in combination with a 10-bit pixel format)
// before SCK is allowed to flip captureDynamicRange to HDRLocalDisplay
// on macOS 14+. Defaults to NO; the SDR capture path is always safe.
@property (nonatomic, assign) BOOL hdrAllowed;

- (instancetype)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate;
- (instancetype)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate hdrAllowed:(BOOL)hdrAllowed;

- (void)setFrameWidth:(int)frameWidth frameHeight:(int)frameHeight;
- (dispatch_semaphore_t)capture:(FrameCallbackBlock)frameCallback;

@end
