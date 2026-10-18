/**
 * @file src/platform/macos/sc_video.m
 * @brief Definitions for ScreenCaptureKit video capture on macOS.
 */
// local includes
#import "sc_video.h"

/**
 * While the screen is static ScreenCaptureKit sends no frames, but Sunshine's capture loop
 * only notices a stop request when it handles a frame (AVCaptureScreenInput streamed at a
 * constant rate). Re-deliver the last frame at this interval while idle.
 */
static const uint64_t kIdleRepeatNs = 100 * NSEC_PER_MSEC;

// Documented in sc_video.h. ScreenCaptureKit owns at most 8 surfaces and stalls once they are all
// held, while Sunshine keeps up to 12 captured images alive, so each frame is copied into our own
// pool and the capture surface goes back to ScreenCaptureKit right away. The copy is a CPU plane
// copy (about 2.6 ms per 1440p P010 frame on an M4).
CMSampleBufferRef sc_copy_sample_buffer(CMSampleBufferRef source, CVPixelBufferPoolRef *pool) {
  CVPixelBufferRef src = CMSampleBufferGetImageBuffer(source);
  const OSType format = CVPixelBufferGetPixelFormatType(src);
  const size_t width = CVPixelBufferGetWidth(src), height = CVPixelBufferGetHeight(src);

  if (*pool) {
    NSDictionary *attrs = (NSDictionary *) CVPixelBufferPoolGetPixelBufferAttributes(*pool);
    if ([attrs[(id) kCVPixelBufferPixelFormatTypeKey] unsignedIntValue] != format || [attrs[(id) kCVPixelBufferWidthKey] unsignedLongValue] != width || [attrs[(id) kCVPixelBufferHeightKey] unsignedLongValue] != height) {
      CVPixelBufferPoolRelease(*pool);
      *pool = NULL;
    }
  }
  if (!*pool) {
    NSDictionary *attrs = @{
      (id) kCVPixelBufferPixelFormatTypeKey: @(format),
      (id) kCVPixelBufferWidthKey: @(width),
      (id) kCVPixelBufferHeightKey: @(height),
      (id) kCVPixelBufferIOSurfacePropertiesKey: @ {},
    };
    if (CVPixelBufferPoolCreate(kCFAllocatorDefault, NULL, (CFDictionaryRef) attrs, pool) != kCVReturnSuccess) {
      return NULL;
    }
  }

  CVPixelBufferRef dst = NULL;
  if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, *pool, &dst) != kCVReturnSuccess) {
    return NULL;
  }

  CVPixelBufferLockBaseAddress(src, kCVPixelBufferLock_ReadOnly);
  CVPixelBufferLockBaseAddress(dst, 0);
  const size_t planes = CVPixelBufferGetPlaneCount(src);
  for (size_t p = 0; p < (planes ? planes : 1); p++) {
    const uint8_t *s = planes ? CVPixelBufferGetBaseAddressOfPlane(src, p) : CVPixelBufferGetBaseAddress(src);
    uint8_t *d = planes ? CVPixelBufferGetBaseAddressOfPlane(dst, p) : CVPixelBufferGetBaseAddress(dst);
    const size_t s_stride = planes ? CVPixelBufferGetBytesPerRowOfPlane(src, p) : CVPixelBufferGetBytesPerRow(src);
    const size_t d_stride = planes ? CVPixelBufferGetBytesPerRowOfPlane(dst, p) : CVPixelBufferGetBytesPerRow(dst);
    const size_t rows = planes ? CVPixelBufferGetHeightOfPlane(src, p) : height;
    if (s_stride == d_stride) {
      memcpy(d, s, s_stride * rows);
    } else {
      for (size_t r = 0; r < rows; r++) {
        memcpy(d + r * d_stride, s + r * s_stride, MIN(s_stride, d_stride));
      }
    }
  }
  CVPixelBufferUnlockBaseAddress(dst, 0);
  CVPixelBufferUnlockBaseAddress(src, kCVPixelBufferLock_ReadOnly);
  CVBufferPropagateAttachments(src, dst);  // Keeps the BT.2020 / PQ color tags.

  CMSampleTimingInfo timing;
  if (CMSampleBufferGetSampleTimingInfo(source, 0, &timing) != noErr) {
    timing = (CMSampleTimingInfo) {kCMTimeInvalid, kCMTimeInvalid, kCMTimeInvalid};
  }
  CMVideoFormatDescriptionRef desc = NULL;
  CMSampleBufferRef copy = NULL;
  if (CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault, dst, &desc) == noErr) {
    CMSampleBufferCreateReadyWithImageBuffer(kCFAllocatorDefault, dst, desc, &timing, &copy);
    CFRelease(desc);
  }
  CVPixelBufferRelease(dst);
  return copy;
}

/**
 * @brief One running capture: a ScreenCaptureKit stream feeding one Sunshine callback.
 *
 * Sunshine can run several captures on the same display at once (the capture loop plus
 * dummy_img() probes), just like AVVideo's per-connection outputs, so each gets its own stream.
 */
API_AVAILABLE(macos(15.0))
@interface SCCapture: NSObject <SCStreamOutput, SCStreamDelegate>
@end

/**
 * @brief Private methods of SCVideo used by SCCapture.
 */
@interface SCVideo ()
/**
 * @brief Drop a finished capture from the active set.
 *
 * @param capture Capture that ended.
 */
- (void)forgetCapture:(SCCapture *)capture;
@end

@implementation SCCapture {
@public
  SCStream *_stream;  ///< Stream for this capture; nil once stopped.
  FrameCallbackBlock _callback;  ///< Sunshine callback that receives each frame.
  dispatch_semaphore_t _signal;  ///< Signaled when the capture ends; Sunshine waits on it.
  dispatch_queue_t _queue;  ///< Serial queue for samples and the idle timer.
  dispatch_source_t _idleTimer;  ///< Re-delivers the last frame while the screen is idle.
  CMSampleBufferRef _lastFrame;  ///< Last delivered frame (our copy); only touched on `_queue`.
  uint64_t _lastFrameTime;  ///< Uptime of `_lastFrame` in nanoseconds; only touched on `_queue`.
  CVPixelBufferPoolRef _pool;  ///< Pool for frame copies; only touched on `_queue`.
  BOOL _stopped;  ///< Set once the capture ended; guarded by `@synchronized(self)`.
  SCVideo *_owner;  ///< Unretained owner, cleared on stop.
}

/**
 * @brief Release the frame copy, pool, callback and queue.
 */
- (void)dealloc {
  if (_lastFrame) {
    CFRelease(_lastFrame);
  }
  if (_pool) {
    CVPixelBufferPoolRelease(_pool);
  }
  [_callback release];
  dispatch_release(_queue);
  [super dealloc];
}

/**
 * @brief End this capture: release Sunshine's capture loop and stop the stream asynchronously.
 */
- (void)stop {
  dispatch_semaphore_t signal = nil;
  dispatch_source_t timer = nil;
  SCStream *stream = nil;
  SCVideo *owner = nil;
  @synchronized(self) {
    if (_stopped) {
      return;
    }
    _stopped = YES;
    signal = _signal;
    timer = _idleTimer;
    stream = _stream;
    owner = _owner;
    _idleTimer = nil;
    _stream = nil;
    _owner = nil;
  }
  [self retain];  // The owner may drop the last reference below.
  [owner forgetCapture:self];
  if (timer) {
    dispatch_source_cancel(timer);
    dispatch_release(timer);
  }
  if (signal) {
    dispatch_semaphore_signal(signal);
  }
  if (stream) {
    // stopCapture drains the sample handler queue, which may be the caller, so never wait for it.
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      [stream stopCaptureWithCompletionHandler:^(NSError *error) {
        [stream release];
        [self release];
      }];
    });
  } else {
    [self release];
  }
}

/**
 * @brief Hand a frame to Sunshine and stop when it asks to. Runs on `_queue`.
 *
 * @param sampleBuffer Frame to deliver.
 */
- (void)deliver:(CMSampleBufferRef)sampleBuffer {
  @synchronized(self) {
    if (_stopped) {
      return;
    }
  }
  if (!_callback(sampleBuffer)) {
    [self stop];
  }
}

/**
 * @brief Copy and deliver each complete screen frame.
 *
 * @param stream Stream that produced the sample.
 * @param sampleBuffer Captured sample.
 * @param type Output type; only screen samples are used.
 */
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
  if (type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sampleBuffer)) {
    return;
  }

  // Skip idle/blank status frames: they carry no pixel buffer.
  CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
  if (attachments && CFArrayGetCount(attachments) > 0) {
    NSDictionary *info = (NSDictionary *) CFArrayGetValueAtIndex(attachments, 0);
    NSNumber *status = info[SCStreamFrameInfoStatus];
    if (status && status.integerValue != SCFrameStatusComplete) {
      return;
    }
  }
  if (!CMSampleBufferGetImageBuffer(sampleBuffer)) {
    return;
  }

  CMSampleBufferRef copy = sc_copy_sample_buffer(sampleBuffer, &_pool);
  if (!copy) {
    return;
  }
  if (_lastFrame) {
    CFRelease(_lastFrame);
  }
  _lastFrame = copy;
  _lastFrameTime = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);

  [self deliver:copy];
}

/**
 * @brief End the capture when the stream fails.
 *
 * @param stream Stream that stopped.
 * @param error Reason it stopped.
 */
- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
  NSLog(@"ScreenCaptureKit stream stopped: %@", error);
  [self stop];
}

@end

@implementation SCVideo {
  NSMutableSet *_captures;  ///< Active captures, stopped on dealloc.
}

- (id)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate {
  self = [super init];

  CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayID);
  if (!mode) {
    [self release];
    return nil;
  }

  self.displayID = displayID;
  self.pixelFormat = kCVPixelFormatType_32BGRA;
  self.frameWidth = (int) CGDisplayModeGetPixelWidth(mode);
  self.frameHeight = (int) CGDisplayModeGetPixelHeight(mode);
  self.minFrameDuration = CMTimeMake(1, frameRate);
  CFRelease(mode);

  _captures = [[NSMutableSet alloc] init];
  return self;
}

/**
 * @brief Stop all active captures.
 */
- (void)dealloc {
  NSArray *captures = nil;
  @synchronized(self) {
    captures = [_captures allObjects];
  }
  for (SCCapture *capture in captures) {
    [capture stop];
  }
  [_captures release];
  [super dealloc];
}

/**
 * @brief Look up the ScreenCaptureKit display for `displayID`.
 *
 * @return Matching display, or nil if it is not shareable within 5 seconds.
 */
- (SCDisplay *)shareableDisplay {
  __block SCDisplay *found = nil;
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                             onScreenWindowsOnly:YES
                                               completionHandler:^(SCShareableContent *content, NSError *error) {
                                                 for (SCDisplay *display in content.displays) {
                                                   if (display.displayID == self.displayID) {
                                                     found = [display retain];
                                                   }
                                                 }
                                                 dispatch_semaphore_signal(done);
                                               }];
  dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
  dispatch_release(done);
  return [found autorelease];
}

/**
 * @brief Report whether the configured pixel format is 10-bit.
 *
 * @return `YES` for 10-bit 4:2:0 formats.
 */
- (BOOL)isTenBit {
  return self.pixelFormat == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange ||
         self.pixelFormat == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
}

- (dispatch_semaphore_t)capture:(FrameCallbackBlock)frameCallback {
  SCDisplay *display = [self shareableDisplay];
  if (!display) {
    return nil;
  }

  SCContentFilter *filter = [[[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]] autorelease];
  SCStreamConfiguration *config = [[[SCStreamConfiguration alloc] init] autorelease];
  config.width = self.frameWidth;
  config.height = self.frameHeight;
  config.minimumFrameInterval = self.minFrameDuration;
  config.pixelFormat = self.pixelFormat;
  config.queueDepth = 8;
  config.showsCursor = YES;
  if (self.hdr && [self isTenBit]) {
    // Canonical tone mapping keeps EDR 1.0 at 100 nits (reference white) and maps headroom above it.
    config.captureDynamicRange = SCCaptureDynamicRangeHDRCanonicalDisplay;
    config.colorSpaceName = kCGColorSpaceITUR_2100_PQ;
  }

  SCCapture *capture = [[[SCCapture alloc] init] autorelease];
  dispatch_queue_attr_t qos = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0);
  capture->_queue = dispatch_queue_create("sunshine.sck.capture", qos);
  capture->_callback = [frameCallback copy];
  capture->_signal = dispatch_semaphore_create(0);
  capture->_stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:capture];

  NSError *error = nil;
  if (![capture->_stream addStreamOutput:capture type:SCStreamOutputTypeScreen sampleHandlerQueue:capture->_queue error:&error]) {
    NSLog(@"ScreenCaptureKit addStreamOutput failed: %@", error);
    [capture->_stream release];
    capture->_stream = nil;
    return nil;
  }

  dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, capture->_queue);
  dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, kIdleRepeatNs), kIdleRepeatNs, kIdleRepeatNs / 10);
  __block SCCapture *blockCapture = capture;  // __block: the timer must not retain the capture (MRC).
  dispatch_source_set_event_handler(timer, ^{
    if (blockCapture->_lastFrame && clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - blockCapture->_lastFrameTime >= kIdleRepeatNs) {
      [blockCapture deliver:blockCapture->_lastFrame];
    }
  });
  dispatch_resume(timer);  // Resume now: releasing a never-resumed source crashes.
  capture->_idleTimer = timer;

  // The capture never releases its semaphore (as AVVideo), so Sunshine can wait on it after the capture ends.
  dispatch_semaphore_t signal = capture->_signal;
  capture->_owner = self;

  @synchronized(self) {
    [_captures addObject:capture];
  }

  dispatch_semaphore_t started = dispatch_semaphore_create(0);
  __block BOOL ok = YES;
  [capture->_stream startCaptureWithCompletionHandler:^(NSError *startError) {
    if (startError) {
      NSLog(@"ScreenCaptureKit failed to start: %@", startError);
      ok = NO;
    }
    dispatch_semaphore_signal(started);
  }];
  dispatch_semaphore_wait(started, DISPATCH_TIME_FOREVER);
  dispatch_release(started);

  if (!ok) {
    [capture stop];
    return nil;
  }
  return signal;
}

- (void)forgetCapture:(SCCapture *)capture {
  @synchronized(self) {
    [_captures removeObject:capture];
  }
}

@end
