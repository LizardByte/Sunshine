#import "av_video.h"

@implementation AVVideo

// XXX: Currently, this function only returns the screen IDs as names,
// which is not very helpful to the user. The API to retrieve names
// was deprecated with 10.9+.
// However, there is a solution with little external code that can be used:
// https://stackoverflow.com/questions/20025868/cgdisplayioserviceport-is-deprecated-in-os-x-10-9-how-to-replace
+ (NSArray<NSDictionary *> *)displayNames {
  CGDirectDisplayID displays[kMaxDisplays];
  uint32_t count;
  if(CGGetActiveDisplayList(kMaxDisplays, displays, &count) != kCGErrorSuccess) {
    return [NSArray array];
  }

  NSMutableArray *result = [NSMutableArray array];

  for(uint32_t i = 0; i < count; i++) {
    [result addObject:@{
      @"id": [NSNumber numberWithUnsignedInt:displays[i]],
      @"name": [NSString stringWithFormat:@"%d", displays[i]]
    }];
  }

  return [NSArray arrayWithArray:result];
}

- (id)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate {
  self = [super init];

  CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayID);

  self.displayID        = displayID;
  self.pixelFormat      = kCVPixelFormatType_32BGRA;
  self.frameWidth       = CGDisplayModeGetPixelWidth(mode);
  self.frameHeight      = CGDisplayModeGetPixelHeight(mode);
  self.scaling          = CGDisplayPixelsWide(displayID) / CGDisplayModeGetPixelWidth(mode);
  self.paddingLeft      = 0;
  self.paddingRight     = 0;
  self.paddingTop       = 0;
  self.paddingBottom    = 0;
  self.minFrameDuration = CMTimeMake(1, frameRate);
  self.session          = [[AVCaptureSession alloc] init];
  self.videoOutputs     = [[NSMapTable alloc] init];
  self.captureCallbacks = [[NSMapTable alloc] init];
  self.captureSignals   = [[NSMapTable alloc] init];

  CFRelease(mode);

  AVCaptureScreenInput *screenInput = [[AVCaptureScreenInput alloc] initWithDisplayID:self.displayID];
  [screenInput setMinFrameDuration:self.minFrameDuration];

  if([self.session canAddInput:screenInput]) {
    [self.session addInput:screenInput];
  }
  else {
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
  CGImageRef screenshot = CGDisplayCreateImage(self.displayID);

  self.frameWidth  = frameWidth;
  self.frameHeight = frameHeight;

  double screenRatio = (double)CGImageGetWidth(screenshot) / (double)CGImageGetHeight(screenshot);
  double streamRatio = (double)frameWidth / (double)frameHeight;

  if(screenRatio < streamRatio) {
    int padding        = frameWidth - (frameHeight * screenRatio);
    self.paddingLeft   = padding / 2;
    self.paddingRight  = padding - self.paddingLeft;
    self.paddingTop    = 0;
    self.paddingBottom = 0;
  }
  else {
    int padding        = frameHeight - (frameWidth / screenRatio);
    self.paddingLeft   = 0;
    self.paddingRight  = 0;
    self.paddingTop    = padding / 2;
    self.paddingBottom = padding - self.paddingTop;
  }

  // XXX: if the streamed image is larger than the native resolution, we add a black box around
  // the frame. Instead the frame should be resized entirely.
  int delta_width = frameWidth - (CGImageGetWidth(screenshot) + self.paddingLeft + self.paddingRight);
  if(delta_width > 0) {
    int adjust_left  = delta_width / 2;
    int adjust_right = delta_width - adjust_left;
    self.paddingLeft += adjust_left;
    self.paddingRight += adjust_right;
  }

  int delta_height = frameHeight - (CGImageGetHeight(screenshot) + self.paddingTop + self.paddingBottom);
  if(delta_height > 0) {
    int adjust_top    = delta_height / 2;
    int adjust_bottom = delta_height - adjust_top;
    self.paddingTop += adjust_top;
    self.paddingBottom += adjust_bottom;
  }

  CFRelease(screenshot);
}

- (dispatch_semaphore_t)capture:(FrameCallbackBlock)frameCallback {
  @synchronized(self) {
    AVCaptureVideoDataOutput *videoOutput = [[AVCaptureVideoDataOutput alloc] init];

    [videoOutput setVideoSettings:@{
      (NSString *)kCVPixelBufferPixelFormatTypeKey: [NSNumber numberWithUnsignedInt:self.pixelFormat],
      (NSString *)kCVPixelBufferWidthKey: [NSNumber numberWithInt:self.frameWidth],
      (NSString *)kCVPixelBufferExtendedPixelsRightKey: [NSNumber numberWithInt:self.paddingRight],
      (NSString *)kCVPixelBufferExtendedPixelsLeftKey: [NSNumber numberWithInt:self.paddingLeft],
      (NSString *)kCVPixelBufferExtendedPixelsTopKey: [NSNumber numberWithInt:self.paddingTop],
      (NSString *)kCVPixelBufferExtendedPixelsBottomKey: [NSNumber numberWithInt:self.paddingBottom],
      (NSString *)kCVPixelBufferHeightKey: [NSNumber numberWithInt:self.frameHeight]
    }];

    dispatch_queue_attr_t qos       = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL,
            QOS_CLASS_USER_INITIATED,
            DISPATCH_QUEUE_PRIORITY_HIGH);
    dispatch_queue_t recordingQueue = dispatch_queue_create("videoCaptureQueue", qos);
    [videoOutput setSampleBufferDelegate:self queue:recordingQueue];

    [self.session stopRunning];

    if([self.session canAddOutput:videoOutput]) {
      [self.session addOutput:videoOutput];
    }
    else {
      [videoOutput release];
      return nil;
    }

    AVCaptureConnection *videoConnection = [videoOutput connectionWithMediaType:AVMediaTypeVideo];
    dispatch_semaphore_t signal          = dispatch_semaphore_create(0);

    [self.videoOutputs setObject:videoOutput forKey:videoConnection];
    [self.captureCallbacks setObject:frameCallback forKey:videoConnection];
    [self.captureSignals setObject:signal forKey:videoConnection];

    [self.session startRunning];

    return signal;
  }
}

- (void)captureOutput:(AVCaptureOutput *)captureOutput
  didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
         fromConnection:(AVCaptureConnection *)connection {

  FrameCallbackBlock callback = [self.captureCallbacks objectForKey:connection];

  if(callback != nil) {
    if(!callback(sampleBuffer)) {
      @synchronized(self) {
        [self.session stopRunning];
        [self.captureCallbacks removeObjectForKey:connection];
        [self.session removeOutput:[self.videoOutputs objectForKey:connection]];
        [self.videoOutputs removeObjectForKey:connection];
        dispatch_semaphore_signal([self.captureSignals objectForKey:connection]);
        [self.captureSignals removeObjectForKey:connection];
        [self.session startRunning];
      }
    }
  }
}

@end
