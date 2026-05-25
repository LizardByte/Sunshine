/**
 * @file src/platform/macos/sc_video.m
 * @brief ScreenCaptureKit-based video capture for macOS 12.3+.
 *
 * Drop-in replacement for the legacy AVCaptureScreenInput path in
 * av_video.m. This first-pass implementation preserves the original
 * pixel format (BGRA8) and selection semantics; HDR / 10-bit pixel
 * format selection and EDR color metadata propagation are layered on
 * top in subsequent commits.
 *
 * Compiled with ARC (-fobjc-arc) for clarity. The other macOS capture
 * files remain MRC; objects flowing from this file to display.mm
 * follow the standard alloc/init +1-retain convention so the boundary
 * works regardless of compile mode on the other side.
 */
#import "sc_video.h"

#import <ScreenCaptureKit/ScreenCaptureKit.h>

API_AVAILABLE(macos(12.3))
@interface SCVideo () <SCStreamDelegate, SCStreamOutput>

@property (nonatomic, strong) SCStream *stream;
@property (nonatomic, strong) SCContentFilter *filter;
@property (nonatomic, strong) SCStreamConfiguration *streamConfig;
@property (nonatomic, strong) dispatch_queue_t sampleQueue;
@property (nonatomic, copy) FrameCallbackBlock currentCallback;
@property (nonatomic, strong) dispatch_semaphore_t currentSignal;
@property (nonatomic, assign) BOOL streamRunning;

@end

@implementation SCVideo

- (instancetype)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.displayID = displayID;
  self.minFrameDuration = CMTimeMake(1, frameRate);
  self.pixelFormat = kCVPixelFormatType_32BGRA;

  CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayID);
  if (mode) {
    self.frameWidth = (int) CGDisplayModeGetPixelWidth(mode);
    self.frameHeight = (int) CGDisplayModeGetPixelHeight(mode);
    CGDisplayModeRelease(mode);
  }

  self.sampleQueue = dispatch_queue_create("dev.lizardbyte.sunshine.sckCapture", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, DISPATCH_QUEUE_PRIORITY_HIGH));

  // SCK content enumeration is async; block until we have the SCDisplay
  // matching the requested CGDirectDisplayID so this initializer remains
  // synchronous (matching AVVideo's contract).
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
  dispatch_semaphore_wait(ready, DISPATCH_TIME_FOREVER);

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

  self.stream = [[SCStream alloc] initWithFilter:self.filter
                                   configuration:self.streamConfig
                                        delegate:self];

  return self;
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
  if (!self.streamRunning || !self.stream) {
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
  @synchronized(self) {
    // Signal and clear any previous capture; SCK streams support one
    // logical consumer in this wrapper. Matches single-callback use in
    // display.mm.
    if (self.currentSignal) {
      dispatch_semaphore_signal(self.currentSignal);
    }

    self.currentCallback = frameCallback;
    self.currentSignal = dispatch_semaphore_create(0);

    if (!self.streamRunning) {
      NSError *outputError = nil;
      if (![self.stream addStreamOutput:self
                                   type:SCStreamOutputTypeScreen
                     sampleHandlerQueue:self.sampleQueue
                                  error:&outputError]) {
        NSLog(@"SCVideo: addStreamOutput failed: %@", outputError);
        dispatch_semaphore_signal(self.currentSignal);
        return self.currentSignal;
      }

      __block NSError *startError = nil;
      dispatch_semaphore_t started = dispatch_semaphore_create(0);
      [self.stream startCaptureWithCompletionHandler:^(NSError *_Nullable error) {
        startError = error;
        dispatch_semaphore_signal(started);
      }];
      dispatch_semaphore_wait(started, DISPATCH_TIME_FOREVER);

      if (startError) {
        NSLog(@"SCVideo: startCapture failed: %@", startError);
        dispatch_semaphore_signal(self.currentSignal);
        return self.currentSignal;
      }
      self.streamRunning = YES;
    }

    return self.currentSignal;
  }
}

- (void)dealloc {
  if (self.streamRunning && self.stream) {
    // Best-effort synchronous stop. The completion handler may not fire
    // before dealloc returns; SCStream itself will tear down cleanly.
    dispatch_semaphore_t stopped = dispatch_semaphore_create(0);
    [self.stream stopCaptureWithCompletionHandler:^(NSError *_Nullable error) {
      (void) error;
      dispatch_semaphore_signal(stopped);
    }];
    // Bounded wait so a misbehaving SCK doesn't hang teardown.
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
    return;
  }

  if (!callback(sampleBuffer)) {
    // Consumer signalled stop. Tear down the stream and unblock the
    // semaphore the caller is waiting on.
    @synchronized(self) {
      self.currentCallback = nil;
    }
    if (self.streamRunning) {
      [self.stream stopCaptureWithCompletionHandler:^(NSError *_Nullable error) {
        (void) error;
      }];
      self.streamRunning = NO;
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
  self.streamRunning = NO;
  dispatch_semaphore_t signal;
  @synchronized(self) {
    signal = self.currentSignal;
    self.currentCallback = nil;
  }
  if (signal) {
    dispatch_semaphore_signal(signal);
  }
}

@end
