/**
 * @file src/platform/macos/sc_video.m
 * @brief ScreenCaptureKit-based video capture. Sole macOS capture
 * backend; Sunshine's deployment target (14.2) is well above the SCK
 * minimum (12.3) so the legacy AVCaptureScreenInput-based AVVideo
 * implementation has been retired.
 *
 * Lifecycle: the underlying SCStream is started exactly once during
 * -initWithDisplay:frameRate: and stopped exactly once during -dealloc.
 * -capture: only swaps the active callback / signal; it never touches
 * the stream lifecycle. This avoids the "addStreamOutput called twice"
 * failure mode that SCK exhibits when an output is re-registered on a
 * stream that already retains it across stop/start cycles.
 *
 * Compiled with ARC (-fobjc-arc) for clarity. The other macOS capture
 * files remain MRC; objects flowing from this file to display.mm
 * follow the standard alloc/init +1-retain convention so the boundary
 * works regardless of compile mode on the other side.
 */
#import "sc_video.h"

#import <ScreenCaptureKit/ScreenCaptureKit.h>

// Bounded wait for any SCK completion handler. SCK should always
// invoke these, but a misbehaving system service must not hang the
// whole startup path.
static const int64_t kSCVideoCompletionTimeoutSec = 5;

API_AVAILABLE(macos(12.3))
@interface SCVideo () <SCStreamDelegate, SCStreamOutput>

@property (nonatomic, strong) SCStream *stream;
@property (nonatomic, strong) SCContentFilter *filter;
@property (nonatomic, strong) SCStreamConfiguration *streamConfig;
@property (nonatomic, strong) dispatch_queue_t sampleQueue;

// All four of the following are mutated from multiple threads (the
// caller of -capture:, the SCK sample-handler queue, and the SCStream
// delegate's didStopWithError:) and so are only ever accessed under
// @synchronized(self).
@property (nonatomic, copy) FrameCallbackBlock currentCallback;
@property (nonatomic, strong) dispatch_semaphore_t currentSignal;
@property (nonatomic, assign) BOOL streamRunning;
@property (nonatomic, assign) BOOL streamOutputAdded;

@end

@implementation SCVideo

- (instancetype)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate {
  return [self initWithDisplay:displayID frameRate:frameRate hdrAllowed:NO];
}

- (instancetype)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate hdrAllowed:(BOOL)hdrAllowed {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.displayID = displayID;
  self.minFrameDuration = CMTimeMake(1, frameRate);
  self.pixelFormat = kCVPixelFormatType_32BGRA;
  self.hdrAllowed = hdrAllowed;

  // Prefer the active display mode's pixel dimensions; fall back to
  // CGDisplayBounds if no mode is currently set (e.g., during display
  // reconfiguration). If both fail we still proceed — SCK will
  // accept the requested SCContentFilter dimensions later.
  CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayID);
  if (mode) {
    self.frameWidth = (int) CGDisplayModeGetPixelWidth(mode);
    self.frameHeight = (int) CGDisplayModeGetPixelHeight(mode);
    CGDisplayModeRelease(mode);
  } else {
    CGRect bounds = CGDisplayBounds(displayID);
    self.frameWidth = (int) CGRectGetWidth(bounds);
    self.frameHeight = (int) CGRectGetHeight(bounds);
  }

  // dispatch_queue_attr_make_with_qos_class's third parameter is a
  // relative priority (range -15..0), NOT one of the legacy global-
  // queue DISPATCH_QUEUE_PRIORITY_* constants. Using 0 keeps the
  // queue at the chosen QoS class's nominal priority.
  dispatch_queue_attr_t qos = dispatch_queue_attr_make_with_qos_class(
    DISPATCH_QUEUE_SERIAL,
    QOS_CLASS_USER_INTERACTIVE,
    0
  );
  self.sampleQueue = dispatch_queue_create("dev.lizardbyte.sunshine.sckCapture", qos);

  // SCK content enumeration is async; block (with a bounded timeout)
  // until we have the SCDisplay matching the requested CGDirectDisplayID
  // so this initializer remains synchronous: callers are not yet block-aware.
  __block SCDisplay *selectedDisplay = nil;
  __block NSError *enumerationError = nil;
  dispatch_semaphore_t ready = dispatch_semaphore_create(0);

  [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                             onScreenWindowsOnly:NO
                                               completionHandler:^(SCShareableContent *_Nullable content, NSError *_Nullable error) {
                                                 if (error || !content) {
                                                   enumerationError = error;
                                                 } else {
                                                   for (SCDisplay *d in content.displays) {
                                                     if (d.displayID == displayID) {
                                                       selectedDisplay = d;
                                                       break;
                                                     }
                                                   }
                                                   // If the requested display wasn't found (display reconfigured,
                                                   // unplugged, etc.) fall back to the first display SCK reports.
                                                   if (!selectedDisplay && content.displays.count > 0) {
                                                     selectedDisplay = content.displays.firstObject;
                                                   }
                                                 }
                                                 dispatch_semaphore_signal(ready);
                                               }];
  if (dispatch_semaphore_wait(ready, dispatch_time(DISPATCH_TIME_NOW, kSCVideoCompletionTimeoutSec * NSEC_PER_SEC)) != 0) {
    NSLog(@"SCVideo: getShareableContent timed out after %lld seconds", kSCVideoCompletionTimeoutSec);
    return nil;
  }

  if (!selectedDisplay) {
    NSLog(@"SCVideo: failed to resolve SCDisplay for id %u: %@", displayID, enumerationError);
    return nil;
  }

  // Empty excluded-windows array: capture everything on the display.
  self.filter = [[SCContentFilter alloc] initWithDisplay:selectedDisplay excludingWindows:@[]];

  self.streamConfig = [[SCStreamConfiguration alloc] init];
  self.streamConfig.width = self.frameWidth;
  self.streamConfig.height = self.frameHeight;
  self.streamConfig.minimumFrameInterval = self.minFrameDuration;
  self.streamConfig.pixelFormat = self.pixelFormat;
  self.streamConfig.queueDepth = 6;  // SCK docs recommend 3-8
  self.streamConfig.showsCursor = YES;

  // If the initial pixel format is already a 10-bit format, flip on EDR
  // immediately so the very first sample buffer carries HDR metadata.
  [self applyDynamicRangeForPixelFormat:self.pixelFormat];

  self.stream = [[SCStream alloc] initWithFilter:self.filter
                                   configuration:self.streamConfig
                                        delegate:self];
  if (!self.stream) {
    NSLog(@"SCVideo: SCStream allocation failed");
    return nil;
  }

  // Register the SCStreamOutput exactly once, here. SCStream retains
  // outputs across stop/start cycles, so re-registering on every
  // -capture: call would fail (or worse, silently duplicate
  // delivery). All subsequent state changes are callback swaps on
  // -capture: rather than stream-lifecycle operations.
  NSError *outputError = nil;
  if (![self.stream addStreamOutput:self
                               type:SCStreamOutputTypeScreen
                 sampleHandlerQueue:self.sampleQueue
                              error:&outputError]) {
    NSLog(@"SCVideo: addStreamOutput failed: %@", outputError);
    return nil;
  }
  self.streamOutputAdded = YES;

  // Start the stream once. Frames begin flowing immediately on the
  // sampleQueue; sample-handler delivery is a no-op until the first
  // -capture: installs a callback (see -stream:didOutputSampleBuffer:ofType:).
  __block NSError *startError = nil;
  dispatch_semaphore_t started = dispatch_semaphore_create(0);
  [self.stream startCaptureWithCompletionHandler:^(NSError *_Nullable error) {
    startError = error;
    dispatch_semaphore_signal(started);
  }];
  if (dispatch_semaphore_wait(started, dispatch_time(DISPATCH_TIME_NOW, kSCVideoCompletionTimeoutSec * NSEC_PER_SEC)) != 0) {
    NSLog(@"SCVideo: startCapture timed out after %lld seconds", kSCVideoCompletionTimeoutSec);
    return nil;
  }
  if (startError) {
    NSLog(@"SCVideo: startCapture failed: %@", startError);
    return nil;
  }
  @synchronized(self) {
    self.streamRunning = YES;
  }

  return self;
}

/**
 * @brief Whether a CVPixelBuffer OSType denotes a 10-bit (or wider) format.
 *
 * Returning YES is the signal that the capture surface is HDR-capable; we
 * use it to drive SCStreamConfiguration.captureDynamicRange on macOS 14+
 * so SCK emits BT.2020 PQ-tagged buffers instead of 10-bit Rec.709.
 */
+ (BOOL)pixelFormatIsHighBitDepth:(OSType)pixelFormat {
  switch (pixelFormat) {
    case kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange:
    case kCVPixelFormatType_420YpCbCr10BiPlanarFullRange:
    case kCVPixelFormatType_422YpCbCr10BiPlanarVideoRange:
    case kCVPixelFormatType_422YpCbCr10BiPlanarFullRange:
    case kCVPixelFormatType_444YpCbCr10BiPlanarVideoRange:
    case kCVPixelFormatType_444YpCbCr10BiPlanarFullRange:
    case kCVPixelFormatType_ARGB2101010LEPacked:
    case kCVPixelFormatType_64ARGB:
    case kCVPixelFormatType_64RGBALE:
      return YES;
    default:
      return NO;
  }
}

- (void)applyDynamicRangeForPixelFormat:(OSType)pixelFormat {
  // captureDynamicRange / SCCaptureDynamicRange* are macOS 14 (Sonoma)
  // SDK symbols. The compile-time guard ensures this block is preprocessed
  // away entirely when building against an older SDK that lacks the
  // declarations; the runtime @available guard prevents using the
  // symbols at runtime on pre-14 systems even with a newer SDK. On
  // 12.3-13.x SCK still honours a requested 10-bit pixel format, but
  // the OS won't tag buffers with BT.2020 PQ metadata automatically;
  // downstream code falls back to Sunshine's existing colorspace logic.
  //
  // Gating: EDR is only enabled when BOTH (a) the chosen pixel format
  // is 10-bit, AND (b) the session was actually negotiated as HDR
  // (`hdrAllowed`). The pixel format on its own is necessary but not
  // sufficient — a 10-bit format may be selected for codec reasons
  // (e.g., a ProRes profile) without the client ever requesting HDR
  // ingest, and silently emitting BT.2020 PQ-tagged buffers into a
  // stream the control plane describes as SDR causes the decoder to
  // tone-map undefined content. Defaulting hdrAllowed to NO keeps the
  // legacy/SDR semantics intact when callers don't opt in.
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
  if (@available(macOS 14.0, *)) {
    if (self.hdrAllowed && [SCVideo pixelFormatIsHighBitDepth:pixelFormat]) {
      // hdrLocalDisplay matches the host display's HDR characteristics,
      // which is what we want for game-streaming: stream what the user
      // would see locally, including the local panel's PQ peak luminance.
      self.streamConfig.captureDynamicRange = SCCaptureDynamicRangeHDRLocalDisplay;
    } else {
      self.streamConfig.captureDynamicRange = SCCaptureDynamicRangeSDR;
    }
  }
#else
  (void) pixelFormat;
#endif
}

- (void)setFrameWidth:(int)frameWidth frameHeight:(int)frameHeight {
  _frameWidth = frameWidth;
  _frameHeight = frameHeight;

  if (self.streamConfig) {
    self.streamConfig.width = frameWidth;
    self.streamConfig.height = frameHeight;
    [self applyConfigurationIfRunning];
  }
}

- (void)setPixelFormat:(OSType)pixelFormat {
  _pixelFormat = pixelFormat;

  if (self.streamConfig) {
    self.streamConfig.pixelFormat = pixelFormat;
    [self applyDynamicRangeForPixelFormat:pixelFormat];
    [self applyConfigurationIfRunning];
  }
}

- (void)setMinFrameDuration:(CMTime)minFrameDuration {
  _minFrameDuration = minFrameDuration;

  if (self.streamConfig) {
    self.streamConfig.minimumFrameInterval = minFrameDuration;
    [self applyConfigurationIfRunning];
  }
}

- (void)applyConfigurationIfRunning {
  BOOL running;
  @synchronized(self) {
    running = self.streamRunning;
  }
  if (!running || !self.stream) {
    return;
  }
  [self.stream updateConfiguration:self.streamConfig
                 completionHandler:^(NSError *_Nullable error) {
                   if (error) {
                     NSLog(@"SCVideo: updateConfiguration failed: %@", error);
                   }
                 }];
}

- (dispatch_semaphore_t)capture:(FrameCallbackBlock)frameCallback {
  // Swap in the new callback. The SCStream output and frame flow are
  // already running from -init; this method is purely a callback
  // installation, not a stream-lifecycle operation. That avoids the
  // double-add failure mode and makes -capture: cheap enough to be
  // called multiple times across the SCVideo's lifetime (e.g., the
  // encoder probe path's dummy_img followed by the real capture).
  dispatch_semaphore_t newSignal = dispatch_semaphore_create(0);
  dispatch_semaphore_t previousSignal = nil;

  @synchronized(self) {
    previousSignal = self.currentSignal;
    self.currentCallback = frameCallback;
    self.currentSignal = newSignal;
  }

  // Unblock any prior caller still waiting on the old semaphore.
  // They will observe their callback was cleared and return.
  if (previousSignal) {
    dispatch_semaphore_signal(previousSignal);
  }

  return newSignal;
}

- (void)dealloc {
  BOOL running;
  SCStream *stream;
  @synchronized(self) {
    running = self.streamRunning;
    stream = self.stream;
    self.streamRunning = NO;
    self.currentCallback = nil;
  }
  if (running && stream) {
    // Best-effort synchronous stop with a bounded wait so a
    // misbehaving SCK doesn't hang teardown.
    dispatch_semaphore_t stopped = dispatch_semaphore_create(0);
    [stream stopCaptureWithCompletionHandler:^(NSError *_Nullable error) {
      (void) error;
      dispatch_semaphore_signal(stopped);
    }];
    dispatch_semaphore_wait(stopped, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
  }
}

#pragma mark - SCStreamOutput

- (void)stream:(SCStream *)stream
  didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                 ofType:(SCStreamOutputType)type {
  if (type != SCStreamOutputTypeScreen) {
    return;
  }
  if (!CMSampleBufferIsValid(sampleBuffer)) {
    return;
  }

  // Drop frames whose status array says they aren't ready. SCK delivers
  // a status attachment on every sample buffer indicating idle vs
  // complete vs blank — we want only complete frames downstream.
  CFArrayRef attachmentsArray = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, NO);
  if (attachmentsArray && CFArrayGetCount(attachmentsArray) > 0) {
    CFDictionaryRef attachments = CFArrayGetValueAtIndex(attachmentsArray, 0);
    CFNumberRef statusNum = CFDictionaryGetValue(attachments, (__bridge CFStringRef) SCStreamFrameInfoStatus);
    if (statusNum) {
      int status = 0;
      CFNumberGetValue(statusNum, kCFNumberSInt32Type, &status);
      if (status != SCFrameStatusComplete) {
        return;
      }
    }
  }

  FrameCallbackBlock callback;
  dispatch_semaphore_t signal;
  @synchronized(self) {
    callback = self.currentCallback;
    signal = self.currentSignal;
  }

  if (!callback) {
    // No active consumer. Drop the frame; the stream keeps running
    // so subsequent -capture: calls can pick up immediately.
    return;
  }

  if (!callback(sampleBuffer)) {
    // Consumer signalled stop. Clear the callback and wake the
    // caller; the underlying SCStream stays alive for any future
    // -capture: caller (cheaper than tearing down and restarting).
    @synchronized(self) {
      if (self.currentCallback == callback) {
        self.currentCallback = nil;
        self.currentSignal = nil;
      }
    }
    if (signal) {
      dispatch_semaphore_signal(signal);
    }
  }
}

#pragma mark - SCStreamDelegate

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
  if (error) {
    NSLog(@"SCVideo: stream stopped with error: %@", error);
  }
  dispatch_semaphore_t signal;
  @synchronized(self) {
    self.streamRunning = NO;
    signal = self.currentSignal;
    self.currentCallback = nil;
    self.currentSignal = nil;
  }
  if (signal) {
    dispatch_semaphore_signal(signal);
  }
}

#pragma mark - Display enumeration

// Active-display upper bound. We just need a buffer size that comfortably
// exceeds any plausible attached-display count.
static const int kMaxDisplays = 32;

+ (NSString *)getDisplayName:(CGDirectDisplayID)displayID {
  for (NSScreen *screen in [NSScreen screens]) {
    if ([screen.deviceDescription[@"NSScreenNumber"] isEqualToNumber:[NSNumber numberWithUnsignedInt:displayID]]) {
      return screen.localizedName;
    }
  }
  return nil;
}

+ (NSArray<NSDictionary *> *)displayNames {
  CGDirectDisplayID displays[kMaxDisplays];
  uint32_t count = 0;
  if (CGGetActiveDisplayList(kMaxDisplays, displays, &count) != kCGErrorSuccess) {
    return @[];
  }

  NSMutableArray *result = [NSMutableArray array];
  for (uint32_t i = 0; i < count; i++) {
    [result addObject:@{
      @"id": [NSNumber numberWithUnsignedInt:displays[i]],
      @"name": [NSString stringWithFormat:@"%u", displays[i]],
      @"displayName": [SCVideo getDisplayName:displays[i]] ?: [NSString stringWithFormat:@"Display %u", displays[i]],
    }];
  }
  return [NSArray arrayWithArray:result];
}

@end
