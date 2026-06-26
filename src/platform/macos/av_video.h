/**
 * @file src/platform/macos/av_video.h
 * @brief Declarations for video capture on macOS.
 */
#pragma once

// platform includes
#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>

/**
 * @brief macOS capture session and video output handles.
 */
struct CaptureSession {
  AVCaptureVideoDataOutput *output;  ///< Output.
  NSCondition *captureStopped;  ///< Capture stopped.
};

static const int kMaxDisplays = 32;

/**
 * @brief AVFoundation video capture controller used by the macOS backend.
 */
@interface AVVideo: NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>

/**
 * @brief Display ID property.
 */
@property (nonatomic, assign) CGDirectDisplayID displayID;
/**
 * @brief Min frame duration property.
 */
@property (nonatomic, assign) CMTime minFrameDuration;
/**
 * @brief Pixel format property.
 */
@property (nonatomic, assign) OSType pixelFormat;
/**
 * @brief Frame width property.
 */
@property (nonatomic, assign) int frameWidth;
/**
 * @brief Frame height property.
 */
@property (nonatomic, assign) int frameHeight;

/**
 * @brief Objective-C block invoked for each captured sample buffer.
 */
typedef bool (^FrameCallbackBlock)(CMSampleBufferRef);

/**
 * @brief Capture session that owns the active AVFoundation inputs and outputs.
 */
@property (nonatomic, assign) AVCaptureSession *session;
/**
 * @brief Video outputs property.
 */
@property (nonatomic, assign) NSMapTable<AVCaptureConnection *, AVCaptureVideoDataOutput *> *videoOutputs;
/**
 * @brief Capture callbacks property.
 */
@property (nonatomic, assign) NSMapTable<AVCaptureConnection *, FrameCallbackBlock> *captureCallbacks;
/**
 * @brief Capture signals property.
 */
@property (nonatomic, assign) NSMapTable<AVCaptureConnection *, dispatch_semaphore_t> *captureSignals;

/**
 * @brief List display names accepted by the selected capture backend.
 *
 * @return Display names accepted by the selected capture backend.
 */
+ (NSArray<NSDictionary *> *)displayNames;
/**
 * @brief Return the user-visible name for a CoreGraphics display.
 *
 * @param displayID Display ID.
 * @return Display name for the supplied CoreGraphics display ID.
 */
+ (NSString *)getDisplayName:(CGDirectDisplayID)displayID;

/**
 * @brief Initialize AVFoundation capture for a display and frame rate.
 *
 * @param displayID Display ID.
 * @param frameRate Frame rate.
 * @return Initialized AVVideo instance, or nil on failure.
 */
- (id)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate;

/**
 * @brief Set frame width frame height.
 *
 * @param frameWidth Frame width.
 * @param frameHeight Frame height.
 */
- (void)setFrameWidth:(int)frameWidth frameHeight:(int)frameHeight;
/**
 * @brief Run the capture loop for this backend.
 *
 * @param frameCallback Frame callback.
 * @return Capture status reported to the streaming pipeline.
 */
- (dispatch_semaphore_t)capture:(FrameCallbackBlock)frameCallback;

@end
