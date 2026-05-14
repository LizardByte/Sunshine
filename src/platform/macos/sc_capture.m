/**
 * @file src/platform/macos/sc_capture.m
 * @brief ScreenCaptureKit-based display capture implementation.
 */
#import "sc_capture.h"

API_AVAILABLE(macos(12.3))
@implementation SCCapture

static BOOL isCompleteScreenFrame(CMSampleBufferRef sampleBuffer) {
  if (!CMSampleBufferIsValid(sampleBuffer)) {
    return NO;
  }

  CFArrayRef attachmentsArray = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, NO);
  if (!attachmentsArray || CFArrayGetCount(attachmentsArray) == 0) {
    return NO;
  }

  CFDictionaryRef attachments = (CFDictionaryRef) CFArrayGetValueAtIndex(attachmentsArray, 0);
  if (!attachments) {
    return NO;
  }

  NSNumber *status = (NSNumber *) CFDictionaryGetValue(attachments, SCStreamFrameInfoStatus);
  if (!status) {
    return NO;
  }

  return status.integerValue == SCFrameStatusComplete;
}

static BOOL isUsableImageSampleBuffer(CMSampleBufferRef sampleBuffer) {
  return sampleBuffer && CMSampleBufferIsValid(sampleBuffer) && CMSampleBufferGetImageBuffer(sampleBuffer);
}

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

    dispatch_semaphore_t initSemaphore = dispatch_semaphore_create(0);
    __block BOOL initSuccess = NO;

    [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *content, NSError *error) {
      if (error) {
        NSLog(@"[SCCapture] Failed to get shareable content: %@", error.localizedDescription);
      } else {
        self.shareableContent = content;
        initSuccess = YES;
      }
      dispatch_semaphore_signal(initSemaphore);
    }];

    dispatch_semaphore_wait(initSemaphore, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

    if (!initSuccess) {
      return nil;
    }
  }
  return self;
}

- (void)dealloc {
  [self stopCapture];
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

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block BOOL success = NO;

    [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *content, NSError *error) {
      if (!error && content) {
        self.shareableContent = content;
        success = YES;
      }
      dispatch_semaphore_signal(sem);
    }];

    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

    if (success) {
      display = [self findDisplayWithID:displayID];
      if (display) {
        NSLog(@"[SCCapture] Found display %u after refresh", displayID);
        return display;
      }
    }
  }

  return nil;
}

- (dispatch_semaphore_t)captureVideo {
  @synchronized(self) {
    if (self.stream) {
      dispatch_semaphore_t stopSem = dispatch_semaphore_create(0);
      [self.stream stopCaptureWithCompletionHandler:^(NSError *error) {
        dispatch_semaphore_signal(stopSem);
      }];
      dispatch_semaphore_wait(stopSem, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
      self.stream = nil;
    }

    if (self.latestSampleBuffer) {
      CFRelease(self.latestSampleBuffer);
      self.latestSampleBuffer = NULL;
    }

    self.stopping = NO;
    self.captureSignal = dispatch_semaphore_create(0);
    self.frameSignal = dispatch_semaphore_create(0);

    SCDisplay *display = [self findDisplayWithIDRetrying:self.displayID];
    if (!display) {
      NSLog(@"[SCCapture] Display not found after retries: %u", self.displayID);
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
    self.streamConfiguration = config;
    [config release];

    NSError *error = nil;
    SCStream *stream = [[SCStream alloc] initWithFilter:self.contentFilter configuration:self.streamConfiguration delegate:self];
    self.stream = stream;
    [stream release];

    if (!self.stream) {
      NSLog(@"[SCCapture] Failed to create SCStream");
      return nil;
    }

    if (![self.stream addStreamOutput:self type:SCStreamOutputTypeScreen sampleHandlerQueue:self.videoQueue error:&error]) {
      NSLog(@"[SCCapture] Failed to add video output: %@", error.localizedDescription);
      return nil;
    }

    dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);
    __block BOOL startSuccess = NO;

    [self.stream startCaptureWithCompletionHandler:^(NSError *error) {
      if (error) {
        NSLog(@"[SCCapture] Failed to start capture: %@", error.localizedDescription);
      } else {
        NSLog(@"[SCCapture] Capture started successfully");
        startSuccess = YES;
      }
      dispatch_semaphore_signal(startSemaphore);
    }];

    dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

    if (!startSuccess) {
      self.captureSignal = nil;
      self.frameSignal = nil;
      self.contentFilter = nil;
      self.streamConfiguration = nil;
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

- (CMSampleBufferRef)copyScreenshotSampleBuffer {
  if (@available(macOS 14.0, *)) {
    SCContentFilter *filter = nil;
    SCStreamConfiguration *config = nil;

    @synchronized(self) {
      if (self.stopping || !self.contentFilter || !self.streamConfiguration) {
        return NULL;
      }

      filter = [self.contentFilter retain];
      config = [self.streamConfiguration retain];
    }

    dispatch_semaphore_t screenshotSemaphore = dispatch_semaphore_create(0);
    __block BOOL timedOut = NO;
    __block CMSampleBufferRef screenshotSampleBuffer = NULL;

    [SCScreenshotManager captureSampleBufferWithFilter:filter
                                         configuration:config
                                     completionHandler:^(CMSampleBufferRef sampleBuffer, NSError *error) {
                                       if (!timedOut && !error && isUsableImageSampleBuffer(sampleBuffer)) {
                                         screenshotSampleBuffer = (CMSampleBufferRef) CFRetain(sampleBuffer);
                                       }

                                       dispatch_semaphore_signal(screenshotSemaphore);
                                     }];

    if (dispatch_semaphore_wait(screenshotSemaphore, dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC)) != 0) {
      timedOut = YES;
    }

    [filter release];
    [config release];

    return screenshotSampleBuffer;
  }

  return NULL;
}

- (void)stopCapture {
  @synchronized(self) {
    self.stopping = YES;

    if (self.stream) {
      dispatch_semaphore_t stopSemaphore = dispatch_semaphore_create(0);

      [self.stream stopCaptureWithCompletionHandler:^(NSError *error) {
        if (error) {
          NSLog(@"[SCCapture] Error stopping capture: %@", error.localizedDescription);
        }
        dispatch_semaphore_signal(stopSemaphore);
      }];

      dispatch_semaphore_wait(stopSemaphore, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
      self.stream = nil;
    }

    self.contentFilter = nil;
    self.streamConfiguration = nil;

    if (self.latestSampleBuffer) {
      CFRelease(self.latestSampleBuffer);
      self.latestSampleBuffer = NULL;
    }

    if (self.frameSignal) {
      dispatch_semaphore_signal(self.frameSignal);
    }

    if (self.captureSignal) {
      dispatch_semaphore_signal(self.captureSignal);
    }
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

  @synchronized(self) {
    if (self.stopping) {
      return;
    }

    BOOL shouldSignal = self.latestSampleBuffer == NULL;
    if (self.latestSampleBuffer) {
      CFRelease(self.latestSampleBuffer);
    }
    self.latestSampleBuffer = (CMSampleBufferRef) CFRetain(sampleBuffer);

    if (shouldSignal && self.frameSignal) {
      dispatch_semaphore_signal(self.frameSignal);
    }
  }
}

@end
