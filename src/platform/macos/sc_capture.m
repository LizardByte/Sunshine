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

static BOOL isCompleteScreenFrame(CMSampleBufferRef sampleBuffer) {
  if (sampleBuffer && CMSampleBufferIsValid(sampleBuffer)) {
    CFArrayRef attachmentsArray = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, NO);
    if (attachmentsArray && CFArrayGetCount(attachmentsArray) > 0) {
      CFDictionaryRef attachments = (CFDictionaryRef) CFArrayGetValueAtIndex(attachmentsArray, 0);
      NSNumber *status = (NSNumber *) CFDictionaryGetValue(attachments, SCStreamFrameInfoStatus);
      return status && status.integerValue == SCFrameStatusComplete;
    }
  }

  return NO;
}

static BOOL isUsableImageSampleBuffer(CMSampleBufferRef sampleBuffer) {
  return sampleBuffer && CMSampleBufferIsValid(sampleBuffer) && CMSampleBufferGetImageBuffer(sampleBuffer);
}

API_AVAILABLE(macos(12.3))
@interface SCCapture ()

@property (nonatomic, assign) BOOL screenshotInFlight;

- (void)finishScreenshotSampleBuffer:(CMSampleBufferRef)sampleBuffer
                               error:(NSError *)error
                              filter:(SCContentFilter *)filter
                       configuration:(SCStreamConfiguration *)config;

@end

API_AVAILABLE(macos(12.3))
@implementation SCCapture

+ (BOOL)isAvailable {
  if (@available(macOS 12.3, *)) {
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

    dispatch_queue_attr_t qos = dispatch_queue_attr_make_with_qos_class(
      DISPATCH_QUEUE_SERIAL,
      QOS_CLASS_USER_INITIATED,
      DISPATCH_QUEUE_PRIORITY_HIGH
    );
    self.videoQueue = dispatch_queue_create("dev.lizardbyte.sunshine.sckVideoQueue", qos);

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

  if (self.videoQueue) {
    dispatch_release(self.videoQueue);
    self.videoQueue = NULL;
  }

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

- (void)stopCurrentStream {
  if (!self.stream) {
    return;
  }

  dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

  [self.stream stopCaptureWithCompletionHandler:^(NSError *error) {
    if (error) {
      NSLog(@"[SCCapture] Error stopping capture: %@", error.localizedDescription);
    }

    dispatch_semaphore_signal(semaphore);
  }];

  dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
  dispatch_release(semaphore);
  self.stream = nil;
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
  @synchronized(self) {
    [self stopCurrentStream];
    [self clearLatestSampleBuffer];
    [self releaseCaptureSignals];

    self.stopping = NO;
    self.screenshotInFlight = NO;
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
    config.minimumFrameInterval = CMTimeMake(1, self.frameRate);
    config.pixelFormat = self.pixelFormat;
    config.queueDepth = 5;
    config.showsCursor = YES;
    if (@available(macOS 14.0, *)) {
      config.captureResolution = SCCaptureResolutionBest;
      config.preservesAspectRatio = YES;
    }
    self.streamConfiguration = config;
    [config release];

    NSError *error = nil;
    SCStream *stream = [[SCStream alloc] initWithFilter:self.contentFilter configuration:self.streamConfiguration delegate:self];
    self.stream = stream;
    [stream release];

    if (!self.stream) {
      NSLog(@"[SCCapture] Failed to create SCStream");
      self.contentFilter = nil;
      self.streamConfiguration = nil;
      [self releaseCaptureSignals];
      return nil;
    }

    if (![self.stream addStreamOutput:self type:SCStreamOutputTypeScreen sampleHandlerQueue:self.videoQueue error:&error]) {
      NSLog(@"[SCCapture] Failed to add video output: %@", error.localizedDescription);
      self.stream = nil;
      self.contentFilter = nil;
      self.streamConfiguration = nil;
      [self releaseCaptureSignals];
      return nil;
    }

    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    __block BOOL startSuccess = NO;

    [self.stream startCaptureWithCompletionHandler:^(NSError *error) {
      if (error) {
        NSLog(@"[SCCapture] Failed to start capture: %@", error.localizedDescription);
      } else {
        NSLog(@"[SCCapture] Capture started successfully");
        startSuccess = YES;
      }

      dispatch_semaphore_signal(semaphore);
    }];

    dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
    dispatch_release(semaphore);

    if (!startSuccess) {
      self.stream = nil;
      self.contentFilter = nil;
      self.streamConfiguration = nil;
      [self releaseCaptureSignals];
      return nil;
    }

    return self.captureSignal;
  }
}

- (CMSampleBufferRef)copyLatestSampleBuffer {
  @synchronized(self) {
    CMSampleBufferRef sampleBuffer = self.latestSampleBuffer;
    self.latestSampleBuffer = NULL;
    return sampleBuffer;
  }
}

- (void)finishScreenshotSampleBuffer:(CMSampleBufferRef)sampleBuffer
                               error:(NSError *)error
                              filter:(SCContentFilter *)filter
                       configuration:(SCStreamConfiguration *)config {
  @synchronized(self) {
    self.screenshotInFlight = NO;
  }

  if (!error && isUsableImageSampleBuffer(sampleBuffer)) {
    [self storeSampleBuffer:sampleBuffer];
  }

  [filter release];
  [config release];
}

- (void)requestScreenshotSampleBuffer {
  if (@available(macOS 14.0, *)) {
    SCContentFilter *filter = nil;
    SCStreamConfiguration *config = nil;

    @synchronized(self) {
      if (self.stopping || self.screenshotInFlight || !self.contentFilter || !self.streamConfiguration) {
        return;
      }

      self.screenshotInFlight = YES;
      filter = [self.contentFilter retain];
      config = [self.streamConfiguration retain];
    }

    [SCScreenshotManager captureSampleBufferWithFilter:filter
                                         configuration:config
                                     completionHandler:^(CMSampleBufferRef sampleBuffer, NSError *error) {
                                       [self finishScreenshotSampleBuffer:sampleBuffer error:error filter:filter configuration:config];
                                     }];
  }
}

- (void)stopCapture {
  @synchronized(self) {
    self.stopping = YES;
    self.screenshotInFlight = NO;

    [self stopCurrentStream];

    self.contentFilter = nil;
    self.streamConfiguration = nil;

    [self clearLatestSampleBuffer];
    [self releaseCaptureSignals];
  }
}

#pragma mark - SCStreamDelegate

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
  NSLog(@"[SCCapture] Stream stopped with error: %@", error.localizedDescription);

  if (self.frameSignal) {
    dispatch_semaphore_signal(self.frameSignal);
  }

  if (self.captureSignal) {
    dispatch_semaphore_signal(self.captureSignal);
  }
}

#pragma mark - SCStreamOutput

- (void)stream:(SCStream *)stream
  didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                 ofType:(SCStreamOutputType)type {
  if (type != SCStreamOutputTypeScreen) {
    return;
  }

  if (!isCompleteScreenFrame(sampleBuffer)) {
    return;
  }

  if (!isUsableImageSampleBuffer(sampleBuffer)) {
    return;
  }

  [self storeSampleBuffer:sampleBuffer];
}

@end
