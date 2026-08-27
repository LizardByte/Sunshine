/**
 * @file src/platform/macos/sc_capture.m
 * @brief ScreenCaptureKit-based display capture implementation.
 */
#import "sc_capture.h"

static SCShareableContent *copyShareableContent(void) {
  __block SCShareableContent *shareableContent = nil;
  dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

  [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *content, NSError *error) {
    if (error) {
      NSLog(@"[SCCapture] Failed to get shareable content: %@", error.localizedDescription);
    } else {
      shareableContent = [content retain];
    }

    dispatch_semaphore_signal(semaphore);
  }];

  dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
  dispatch_release(semaphore);

  return shareableContent;
}

static BOOL isUsableImageSampleBuffer(CMSampleBufferRef sampleBuffer) {
  return sampleBuffer && CMSampleBufferIsValid(sampleBuffer) && CMSampleBufferGetImageBuffer(sampleBuffer);
}

API_AVAILABLE(macos(14.0))
@implementation SCCapture

+ (BOOL)isAvailable {
  if (@available(macOS 14.0, *)) {
    return YES;
  }
  return NO;
}

+ (NSArray<NSDictionary *> *)displayNames {
  CGDirectDisplayID displays[kMaxDisplays];
  uint32_t count;
  if (CGGetActiveDisplayList(kMaxDisplays, displays, &count) != kCGErrorSuccess) {
    return [NSArray array];
  }

  NSMutableArray *result = [NSMutableArray array];

  for (uint32_t i = 0; i < count; i++) {
    [result addObject:@{
      @"id": [NSNumber numberWithUnsignedInt:displays[i]],
      @"name": [NSString stringWithFormat:@"%d", displays[i]],
      @"displayName": [self getDisplayName:displays[i]] ?: @"Unknown Display",
    }];
  }

  return [NSArray arrayWithArray:result];
}

+ (NSString *)getDisplayName:(CGDirectDisplayID)displayID {
  for (NSScreen *screen in [NSScreen screens]) {
    if ([screen.deviceDescription[@"NSScreenNumber"] isEqualToNumber:[NSNumber numberWithUnsignedInt:displayID]]) {
      return screen.localizedName;
    }
  }

  return nil;
}

- (instancetype)initWithDisplay:(CGDirectDisplayID)displayID
                      frameRate:(int)frameRate {
  self = [super init];
  if (self) {
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayID);

    self.displayID = displayID;
    self.frameRate = frameRate;
    self.pixelFormat = kCVPixelFormatType_32BGRA;

    if (mode) {
      self.frameWidth = (int) CGDisplayModeGetPixelWidth(mode);
      self.frameHeight = (int) CGDisplayModeGetPixelHeight(mode);
      CFRelease(mode);
    } else {
      self.frameWidth = (int) CGDisplayPixelsWide(displayID);
      self.frameHeight = (int) CGDisplayPixelsHigh(displayID);
    }

    SCShareableContent *content = copyShareableContent();
    if (!content) {
      [self release];
      return nil;
    }

    self.shareableContent = content;
    [content release];
  }

  return self;
}

- (void)dealloc {
  [self stopCapture];
  self.shareableContent = nil;

  [super dealloc];
}

- (void)setFrameWidth:(int)frameWidth frameHeight:(int)frameHeight {
  self.frameWidth = frameWidth;
  self.frameHeight = frameHeight;
}

- (SCDisplay *)findDisplayWithID:(CGDirectDisplayID)displayID {
  for (SCDisplay *display in self.shareableContent.displays) {
    if (display.displayID == displayID) {
      return display;
    }
  }

  return nil;
}

- (SCDisplay *)findDisplayWithIDRetrying:(CGDirectDisplayID)displayID {
  SCDisplay *display = [self findDisplayWithID:displayID];
  if (display) {
    return display;
  }

  for (int attempt = 1; attempt <= 3; attempt++) {
    NSLog(@"[SCCapture] Display %u not found in SCShareableContent, refreshing (attempt %d/3)", displayID, attempt);
    [NSThread sleepForTimeInterval:1.0];

    SCShareableContent *content = copyShareableContent();
    if (!content) {
      continue;
    }

    self.shareableContent = content;
    [content release];

    display = [self findDisplayWithID:displayID];
    if (display) {
      NSLog(@"[SCCapture] Found display %u after refresh", displayID);
      return display;
    }
  }

  return nil;
}

- (void)releaseCaptureSignals {
  if (self.frameSignal) {
    dispatch_semaphore_signal(self.frameSignal);
    dispatch_release(self.frameSignal);
    self.frameSignal = NULL;
  }

  if (self.captureSignal) {
    dispatch_semaphore_signal(self.captureSignal);
    dispatch_release(self.captureSignal);
    self.captureSignal = NULL;
  }
}

- (void)clearLatestSampleBuffer {
  if (self.latestSampleBuffer) {
    CFRelease(self.latestSampleBuffer);
    self.latestSampleBuffer = NULL;
  }
}

- (void)storeSampleBuffer:(CMSampleBufferRef)sampleBuffer {
  @synchronized(self) {
    if (self.stopping) {
      return;
    }

    BOOL shouldSignal = self.latestSampleBuffer == NULL;

    [self clearLatestSampleBuffer];
    self.latestSampleBuffer = (CMSampleBufferRef) CFRetain(sampleBuffer);

    if (shouldSignal && self.frameSignal) {
      dispatch_semaphore_signal(self.frameSignal);
    }
  }
}

- (dispatch_semaphore_t)captureVideo {
  dispatch_semaphore_t capture_signal = nil;

  @synchronized(self) {
    [self clearLatestSampleBuffer];
    [self releaseCaptureSignals];

    self.stopping = NO;
    self.captureSignal = dispatch_semaphore_create(0);
    self.frameSignal = dispatch_semaphore_create(0);

    SCDisplay *display = [self findDisplayWithIDRetrying:self.displayID];
    if (!display) {
      NSLog(@"[SCCapture] Display not found after retries: %u", self.displayID);
      [self releaseCaptureSignals];
      return nil;
    }

    SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
    self.contentFilter = filter;
    [filter release];

    SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
    config.width = self.frameWidth;
    config.height = self.frameHeight;
    config.pixelFormat = self.pixelFormat;
    config.showsCursor = YES;
    config.captureResolution = SCCaptureResolutionBest;
    config.preservesAspectRatio = YES;
    // Slightly below the target interval: an interval of exactly 1/fps beats against
    // WindowServer's delivery clock and suppresses frames that arrive marginally early.
    const int fps = MAX(self.frameRate, 1);
    config.minimumFrameInterval = CMTimeMake(9, fps * 10);
    // Must exceed the number of sample buffers the pipeline holds at once (latest-wins
    // slot, encoder in-flight, transient retains), or WindowServer runs out of surfaces
    // and stops delivering until one is returned.
    config.queueDepth = 8;
    self.streamConfiguration = config;
    [config release];

    capture_signal = self.captureSignal;
  }

  // Start the stream outside the lock: delivery callbacks synchronize on self.
  SCStream *stream = [[SCStream alloc] initWithFilter:self.contentFilter configuration:self.streamConfiguration delegate:self];
  NSError *output_error = nil;
  if (!stream || ![stream addStreamOutput:self type:SCStreamOutputTypeScreen sampleHandlerQueue:nil error:&output_error]) {
    NSLog(@"[SCCapture] Failed to create stream output: %@", output_error.localizedDescription);
    [stream release];
    @synchronized(self) {
      [self releaseCaptureSignals];
    }
    return nil;
  }

  __block NSError *start_error = nil;
  dispatch_semaphore_t started = dispatch_semaphore_create(0);
  [stream startCaptureWithCompletionHandler:^(NSError *error) {
    start_error = [error retain];
    dispatch_semaphore_signal(started);
  }];
  dispatch_semaphore_wait(started, DISPATCH_TIME_FOREVER);
  dispatch_release(started);

  if (start_error) {
    NSLog(@"[SCCapture] Failed to start capture stream: %@", start_error.localizedDescription);
    [start_error release];
    [stream release];
    @synchronized(self) {
      [self releaseCaptureSignals];
    }
    return nil;
  }

  self.stream = stream;
  [stream release];

  NSLog(@"[SCCapture] SCStream capture configured: %dx%d @ %d fps", self.frameWidth, self.frameHeight, self.frameRate);

  return capture_signal;
}

- (CMSampleBufferRef)copyLatestSampleBuffer {
  @synchronized(self) {
    CMSampleBufferRef sampleBuffer = self.latestSampleBuffer;
    self.latestSampleBuffer = NULL;
    return sampleBuffer;
  }
}

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
  if (type != SCStreamOutputTypeScreen) {
    return;
  }

  // No SCFrameStatus filtering: any sample buffer carrying pixels is a real update.
  if (!isUsableImageSampleBuffer(sampleBuffer)) {
    return;
  }

  [self storeSampleBuffer:sampleBuffer];
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
  NSLog(@"[SCCapture] Stream stopped with error: %@", error.localizedDescription);
  @synchronized(self) {
    if (!self.stopping && self.captureSignal) {
      // Wake the capture loop so the pipeline tears down and reinitializes.
      dispatch_semaphore_signal(self.captureSignal);
    }
  }
}

- (void)stopCapture {
  SCStream *stream = nil;

  @synchronized(self) {
    self.stopping = YES;

    stream = [self.stream retain];
    self.stream = nil;
    self.contentFilter = nil;
    self.streamConfiguration = nil;

    [self clearLatestSampleBuffer];
    [self releaseCaptureSignals];
  }

  if (stream) {
    [stream stopCaptureWithCompletionHandler:^(NSError *error) {
    }];
    [stream release];
  }
}

@end
