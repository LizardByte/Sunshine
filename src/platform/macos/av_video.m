/**
 * @file src/platform/macos/av_video.m
 * @brief Definitions for video capture on macOS.
 */
// local includes
#import "av_video.h"

/**
 * @brief Private capture lifecycle helpers for AVVideo.
 */
@interface AVVideo ()

/**
 * @brief Tear down one capture after its callback queue has been serialized.
 *
 * @param connection Capture connection to tear down.
 * @param signalCompletion Whether to wake the thread waiting for normal capture completion.
 */
- (void)finishCapture:(AVCaptureConnection *)connection signalCompletion:(BOOL)signalCompletion;

@end

@implementation AVVideo

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
  self.session = [[AVCaptureSession alloc] init];
  self.videoOutputs = [[NSMapTable alloc] init];
  self.captureCallbacks = [[NSMapTable alloc] init];
  self.captureSignals = [[NSMapTable alloc] init];

  CFRelease(mode);

  AVCaptureScreenInput *screenInput = [[AVCaptureScreenInput alloc] initWithDisplayID:self.displayID];
  [screenInput setMinFrameDuration:self.minFrameDuration];

  if ([self.session canAddInput:screenInput]) {
    [self.session addInput:screenInput];
  } else {
    [screenInput release];
    return nil;
  }

  [self.session startRunning];

  return self;
}

- (void)dealloc {
  [self.videoOutputs release];
  [self.captureCallbacks release];
  [self.captureSignals release];
  [self.session stopRunning];
  [super dealloc];
}

- (void)setFrameWidth:(int)frameWidth frameHeight:(int)frameHeight {
  self.frameWidth = frameWidth;
  self.frameHeight = frameHeight;
}

- (dispatch_semaphore_t)capture:(FrameCallbackBlock)frameCallback {
  @synchronized(self) {
    AVCaptureVideoDataOutput *videoOutput = [[AVCaptureVideoDataOutput alloc] init];

    [videoOutput setVideoSettings:@{
      (NSString *) kCVPixelBufferPixelFormatTypeKey: [NSNumber numberWithUnsignedInt:self.pixelFormat],
      (NSString *) kCVPixelBufferWidthKey: [NSNumber numberWithInt:self.frameWidth],
      (NSString *) kCVPixelBufferHeightKey: [NSNumber numberWithInt:self.frameHeight],
      (NSString *) AVVideoScalingModeKey: AVVideoScalingModeResizeAspect,
    }];

    dispatch_queue_attr_t qos = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, DISPATCH_QUEUE_PRIORITY_HIGH);
    dispatch_queue_t recordingQueue = dispatch_queue_create("videoCaptureQueue", qos);
    [videoOutput setSampleBufferDelegate:self queue:recordingQueue];

    [self.session stopRunning];

    if ([self.session canAddOutput:videoOutput]) {
      [self.session addOutput:videoOutput];
    } else {
      [videoOutput release];
      return nil;
    }

    AVCaptureConnection *videoConnection = [videoOutput connectionWithMediaType:AVMediaTypeVideo];
    dispatch_semaphore_t signal = dispatch_semaphore_create(0);

    [self.videoOutputs setObject:videoOutput forKey:videoConnection];
    [self.captureCallbacks setObject:frameCallback forKey:videoConnection];
    [self.captureSignals setObject:signal forKey:videoConnection];

    [self.session startRunning];

    return signal;
  }
}

- (void)stopCapture:(dispatch_semaphore_t)signal {
  AVCaptureConnection *target = nil;
  AVCaptureVideoDataOutput *videoOutput = nil;

  @synchronized(self) {
    for (AVCaptureConnection *connection in self.captureSignals) {
      if ([self.captureSignals objectForKey:connection] == signal) {
        target = [connection retain];
        videoOutput = [[self.videoOutputs objectForKey:connection] retain];
        break;
      }
    }
  }

  if (target == nil) {
    return;
  }

  // The callback is invoked on a serial queue. Running teardown on that same queue waits for
  // an in-flight callback and orders this teardown before any callback that has not started.
  dispatch_queue_t callbackQueue = [videoOutput sampleBufferCallbackQueue];
  if (callbackQueue != nil) {
    dispatch_sync(callbackQueue, ^{
      [self finishCapture:target signalCompletion:NO];
    });
  } else {
    [self finishCapture:target signalCompletion:NO];
  }

  [videoOutput release];
  [target release];
}

- (void)finishCapture:(AVCaptureConnection *)connection signalCompletion:(BOOL)signalCompletion {
  @synchronized(self) {
    AVCaptureVideoDataOutput *videoOutput = [self.videoOutputs objectForKey:connection];
    dispatch_semaphore_t signal = [self.captureSignals objectForKey:connection];
    if (videoOutput == nil || signal == nil) {
      return;
    }

    // Claim this capture before stopping the session so queued callbacks become no-ops.
    [self.captureCallbacks removeObjectForKey:connection];
    [self.session stopRunning];
    [self.session removeOutput:videoOutput];
    [self.videoOutputs removeObjectForKey:connection];
    if (signalCompletion) {
      dispatch_semaphore_signal(signal);
    }
    [self.captureSignals removeObjectForKey:connection];
    [self.session startRunning];
  }
}

- (void)captureOutput:(AVCaptureOutput *)captureOutput
  didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
         fromConnection:(AVCaptureConnection *)connection {
  FrameCallbackBlock callback = nil;
  @synchronized(self) {
    callback = [[self.captureCallbacks objectForKey:connection] copy];
  }

  if (callback != nil) {
    const bool shouldStopCapture = !callback(sampleBuffer);
    [callback release];

    if (shouldStopCapture) {
      [self finishCapture:connection signalCompletion:YES];
    }
  }
}

@end
