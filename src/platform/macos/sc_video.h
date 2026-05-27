/**
 * @file src/platform/macos/sc_video.h
 * @brief Declarations for ScreenCaptureKit-based video capture on macOS.
 *
 * SCVideo is now Sunshine's only macOS capture backend. The deployment
 * target (MACOSX_DEPLOYMENT_TARGET=14.2) is well above the macOS 12.3
 * minimum where ScreenCaptureKit became available, so the legacy
 * AVCaptureScreenInput-based AVVideo path has been removed entirely.
 */
#pragma once

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

// Block signature used to deliver captured sample buffers back to the
// platform-agnostic capture loop. Returning NO from the block stops
// further deliveries on this capture session.
typedef bool (^FrameCallbackBlock)(CMSampleBufferRef);

@interface SCVideo : NSObject

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

// Enumerate the currently-active CGDisplays as an array of dictionaries
// with keys @"id" (NSNumber, the CGDirectDisplayID), @"name" (NSString,
// the numeric id as a string for legacy callers), and @"displayName"
// (NSString, the user-facing name from NSScreen.localizedName).
+ (NSArray<NSDictionary *> *)displayNames;

@end
