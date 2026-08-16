/**
 * @file src/platform/macos/sc_capture.h
 * @brief Declarations for ScreenCaptureKit-based display capture on macOS.
 */
#pragma once

// platform includes
#import <AppKit/AppKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

/**
 * @brief ScreenCaptureKit display capture controller used by the macOS backend.
 *
 * Frames are captured exclusively through SCScreenshotManager polling rather than an
 * SCStream: SCStream misses or delays small screen updates (e.g. a blinking terminal
 * cursor), and mixing stream frames with screenshot frames produces visible flicker on
 * translucent surfaces because the two paths composite slightly differently. Polling a
 * single consistent source avoids both problems.
 */
API_AVAILABLE(macos(14.0))
@interface SCCapture: NSObject

/**
 * @def kMaxDisplays
 * @brief Maximum number of displays queried from CoreGraphics.
 */
#define kMaxDisplays 32

/**
 * @brief Display ID property.
 */
@property (nonatomic, assign) CGDirectDisplayID displayID;
/**
 * @brief Capture frame rate property.
 */
@property (nonatomic, assign) int frameRate;
/**
 * @brief Output pixel format property.
 */
@property (nonatomic, assign) OSType pixelFormat;
/**
 * @brief Output frame width property.
 */
@property (nonatomic, assign) int frameWidth;
/**
 * @brief Output frame height property.
 */
@property (nonatomic, assign) int frameHeight;

/**
 * @brief Content filter selecting the captured display.
 */
@property (nonatomic, strong) SCContentFilter *contentFilter;
/**
 * @brief Stream configuration used for screenshot captures.
 */
@property (nonatomic, strong) SCStreamConfiguration *streamConfiguration;
/**
 * @brief Cached shareable content used for display lookup.
 */
@property (nonatomic, strong) SCShareableContent *shareableContent;

/**
 * @brief Semaphore signalled when capture stops.
 */
@property (nonatomic, assign) dispatch_semaphore_t captureSignal;
/**
 * @brief Semaphore signalled when a new frame is available.
 */
@property (nonatomic, assign) dispatch_semaphore_t frameSignal;
/**
 * @brief Whether the capture is shutting down.
 */
@property (nonatomic, assign) BOOL stopping;
/**
 * @brief Latest captured sample buffer, replaced as newer frames arrive.
 */
@property (nonatomic, assign) CMSampleBufferRef latestSampleBuffer;

/**
 * @brief Check whether ScreenCaptureKit screenshot capture is available on this system.
 *
 * @return `YES` when the running macOS version supports SCScreenshotManager.
 */
+ (BOOL)isAvailable;
/**
 * @brief Enumerate active displays.
 *
 * @return Array of dictionaries describing each active display.
 */
+ (NSArray<NSDictionary *> *)displayNames;
/**
 * @brief Look up the localized name of a display.
 *
 * @param displayID Display to look up.
 * @return Localized display name, or nil when unknown.
 */
+ (NSString *)getDisplayName:(CGDirectDisplayID)displayID;

/**
 * @brief Initialize capture for a display.
 *
 * @param displayID Display to capture.
 * @param frameRate Requested capture frame rate.
 * @return Initialized instance, or nil on failure.
 */
- (instancetype)initWithDisplay:(CGDirectDisplayID)displayID
                      frameRate:(int)frameRate;

/**
 * @brief Set the capture output dimensions.
 *
 * @param frameWidth Output frame width in pixels.
 * @param frameHeight Output frame height in pixels.
 */
- (void)setFrameWidth:(int)frameWidth frameHeight:(int)frameHeight;
/**
 * @brief Prepare the capture session for screenshot polling.
 *
 * @return Semaphore signalled when capture stops, or nil on failure.
 */
- (dispatch_semaphore_t)captureVideo;
/**
 * @brief Take ownership of the most recent captured frame.
 *
 * @return Retained sample buffer, or NULL when no new frame is available.
 */
- (CMSampleBufferRef)copyLatestSampleBuffer;
/**
 * @brief Request the next screenshot frame; at most one request is kept in flight.
 */
- (void)requestScreenshotSampleBuffer;
/**
 * @brief Stop capture and release capture state.
 */
- (void)stopCapture;

@end
