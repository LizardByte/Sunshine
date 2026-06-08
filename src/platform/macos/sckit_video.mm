/**
 * @file src/platform/macos/sckit_video.mm
 * @brief Definitions for ScreenCaptureKit display capture on macOS.
 */

#import "sckit_video.h"

#include "src/logging.h"

@implementation SCKitVideo

+ (NSArray<NSDictionary *> *)displayNames {
  CGDirectDisplayID displays[kMaxSCKitDisplays];
  uint32_t count;
  if (CGGetActiveDisplayList(kMaxSCKitDisplays, displays, &count) != kCGErrorSuccess) {
    return [NSArray array];
  }

  NSMutableArray *result = [NSMutableArray array];

  for (uint32_t i = 0; i < count; i++) {
    [result addObject:@{
      @"id": [NSNumber numberWithUnsignedInt:displays[i]],
      @"name": [NSString stringWithFormat:@"%d", displays[i]],
      @"displayName": [self getDisplayName:displays[i]],
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

+ (SCDisplay *)screenCaptureKitDisplayForID:(CGDirectDisplayID)displayID {
  __block SCShareableContent *content = nil;
  __block NSError *contentError = nil;
  dispatch_semaphore_t contentSignal = dispatch_semaphore_create(0);

  [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                             onScreenWindowsOnly:YES
                                               completionHandler:^(SCShareableContent *_Nullable shareableContent, NSError *_Nullable error) {
    content = [shareableContent retain];
    contentError = [error retain];
    dispatch_semaphore_signal(contentSignal);
  }];

  dispatch_semaphore_wait(contentSignal, DISPATCH_TIME_FOREVER);

  if (contentError != nil) {
    [contentError release];
    [content release];
    return nil;
  }

  SCDisplay *result = nil;
  for (SCDisplay *display in content.displays) {
    if (display.displayID == displayID) {
      result = [display retain];
      break;
    }
  }

  [content release];
  return [result autorelease];
}

- (id)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate {
  self = [super init];
  if (self == nil) {
    return nil;
  }

  CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayID);
  if (mode == nullptr) {
    [self release];
    return nil;
  }

  self.displayID = displayID;
  self.pixelFormat = kCVPixelFormatType_32BGRA;
  self.frameWidth = (int) CGDisplayModeGetPixelWidth(mode);
  self.frameHeight = (int) CGDisplayModeGetPixelHeight(mode);
  self.requestedFrameRate = frameRate;

  CFRelease(mode);

  self.display = [SCKitVideo screenCaptureKitDisplayForID:displayID];
  if (self.display == nil) {
    [self release];
    return nil;
  }

  BOOST_LOG(info) << "Using ScreenCaptureKit display capture for display " << displayID
                  << " (" << self.frameWidth << "x" << self.frameHeight << " pixels)"
                  << " requested_fps=" << frameRate;

  return self;
}

- (void)dealloc {
  [self.stream stopCaptureWithCompletionHandler:nil];
  self.stream = nil;
  self.display = nil;
  self.captureCallback = nil;
  self.captureError = nil;
  [super dealloc];
}

- (void)setFrameWidth:(int)frameWidth frameHeight:(int)frameHeight {
  self.frameWidth = frameWidth;
  self.frameHeight = frameHeight;
}

- (dispatch_semaphore_t)capture:(SCKitFrameCallbackBlock)frameCallback {
  @synchronized(self) {
    self.captureCallback = frameCallback;
    self.captureSignal = dispatch_semaphore_create(0);
    self.captureError = nil;

    SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:self.display excludingWindows:@[]];
    SCStreamConfiguration *configuration = [[SCStreamConfiguration alloc] init];
    configuration.width = self.frameWidth;
    configuration.height = self.frameHeight;
    configuration.pixelFormat = self.pixelFormat;
    configuration.queueDepth = 8;
    configuration.showsCursor = YES;

    // ScreenCaptureKit's default is 1/60. Use a slightly shorter interval than the
    // client target to avoid SCK/WindowServer timing jitter dropping high-FPS updates.
    if (self.requestedFrameRate > 0) {
      CMTime targetInterval = CMTimeMake(1, self.requestedFrameRate);
      configuration.minimumFrameInterval = CMTimeMultiplyByFloat64(targetInterval, 0.9);
    } else {
      configuration.minimumFrameInterval = kCMTimeZero;
    }

    if (@available(macOS 14.0, *)) {
      configuration.captureResolution = SCCaptureResolutionNominal;
    }

    self.deliveredFrames = 0;
    self.completeFrames = 0;
    self.startedFrames = 0;
    self.idleFrames = 0;
    self.blankFrames = 0;
    self.suspendedFrames = 0;
    self.stoppedFrames = 0;
    self.unknownStatusFrames = 0;
    self.noImageFrames = 0;
    self.invalidFrames = 0;
    self.lastFrameReportTime = [[NSDate date] timeIntervalSince1970];

    BOOST_LOG(info) << "Starting ScreenCaptureKit stream for display " << self.displayID
                    << " capture=" << self.frameWidth << "x" << self.frameHeight
                    << " pixel_format=0x" << std::hex << self.pixelFormat << std::dec
                    << " queue_depth=" << configuration.queueDepth
                    << " minimum_frame_interval=" << configuration.minimumFrameInterval.value
                    << "/" << configuration.minimumFrameInterval.timescale;

    self.stream = [[[SCStream alloc] initWithFilter:filter configuration:configuration delegate:nil] autorelease];
    [filter release];
    [configuration release];

    NSError *outputError = nil;
    dispatch_queue_attr_t qos = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, DISPATCH_QUEUE_PRIORITY_HIGH);
    dispatch_queue_t recordingQueue = dispatch_queue_create("screenCaptureKitQueue", qos);
    if (![self.stream addStreamOutput:self type:SCStreamOutputTypeScreen sampleHandlerQueue:recordingQueue error:&outputError]) {
      self.captureError = outputError;
      dispatch_semaphore_signal(self.captureSignal);
      return self.captureSignal;
    }

    [self.stream startCaptureWithCompletionHandler:^(NSError *_Nullable startError) {
      if (startError != nil) {
        @synchronized(self) {
          self.captureError = startError;
          BOOST_LOG(error) << "ScreenCaptureKit failed to start display " << self.displayID
                           << ": " << startError.localizedDescription.UTF8String;
          dispatch_semaphore_signal(self.captureSignal);
        }
      }
    }];

    return self.captureSignal;
  }
}

- (void)reportFrameStatsIfNeeded {
  NSTimeInterval now = [[NSDate date] timeIntervalSince1970];
  NSTimeInterval elapsed = now - self.lastFrameReportTime;
  if (elapsed < 5.0) {
    return;
  }

  double deliveredFPS = elapsed > 0 ? (double) self.deliveredFrames / elapsed : 0.0;
  BOOST_LOG(info) << "ScreenCaptureKit display " << self.displayID
                  << " delivered_fps=" << deliveredFPS
                  << " complete=" << self.completeFrames
                  << " started=" << self.startedFrames
                  << " idle=" << self.idleFrames
                  << " blank=" << self.blankFrames
                  << " suspended=" << self.suspendedFrames
                  << " stopped=" << self.stoppedFrames
                  << " unknown_status=" << self.unknownStatusFrames
                  << " no_image=" << self.noImageFrames
                  << " invalid=" << self.invalidFrames
                  << " over_seconds=" << elapsed;

  self.deliveredFrames = 0;
  self.completeFrames = 0;
  self.startedFrames = 0;
  self.idleFrames = 0;
  self.blankFrames = 0;
  self.suspendedFrames = 0;
  self.stoppedFrames = 0;
  self.unknownStatusFrames = 0;
  self.noImageFrames = 0;
  self.invalidFrames = 0;
  self.lastFrameReportTime = now;
}

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
  if (type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sampleBuffer) || !CMSampleBufferDataIsReady(sampleBuffer)) {
    self.invalidFrames += 1;
    [self reportFrameStatsIfNeeded];
    return;
  }

  SCFrameStatus frameStatus = SCFrameStatusComplete;
  bool knownStatus = true;
  CFArrayRef attachmentsArray = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
  if (attachmentsArray != nil && CFArrayGetCount(attachmentsArray) > 0) {
    NSDictionary *attachments = (NSDictionary *) CFArrayGetValueAtIndex(attachmentsArray, 0);
    NSNumber *statusNumber = attachments[SCStreamFrameInfoStatus];
    if (statusNumber != nil) {
      frameStatus = (SCFrameStatus) statusNumber.integerValue;
    }
  }

  switch (frameStatus) {
    case SCFrameStatusComplete:
      self.completeFrames += 1;
      break;
    case SCFrameStatusStarted:
      self.startedFrames += 1;
      break;
    case SCFrameStatusIdle:
      self.idleFrames += 1;
      break;
    case SCFrameStatusBlank:
      self.blankFrames += 1;
      break;
    case SCFrameStatusSuspended:
      self.suspendedFrames += 1;
      break;
    case SCFrameStatusStopped:
      self.stoppedFrames += 1;
      break;
    default:
      self.unknownStatusFrames += 1;
      knownStatus = false;
      break;
  }

  CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (imageBuffer == nil) {
    self.noImageFrames += 1;
    [self reportFrameStatsIfNeeded];
    return;
  }

  SCKitFrameCallbackBlock callback = self.captureCallback;
  if (callback == nil) {
    return;
  }

  self.deliveredFrames += 1;
  [self reportFrameStatsIfNeeded];

  if (!knownStatus) {
    BOOST_LOG(warning) << "ScreenCaptureKit display " << self.displayID
                       << " delivered frame with unknown status=" << (NSInteger) frameStatus;
  }

  if (!callback(sampleBuffer)) {
    @synchronized(self) {
      self.captureCallback = nil;
      [self.stream stopCaptureWithCompletionHandler:^(NSError *_Nullable error) {
        BOOST_LOG(info) << "Stopped ScreenCaptureKit stream for display " << self.displayID;
        dispatch_semaphore_signal(self.captureSignal);
      }];
    }
  }
}

@end
