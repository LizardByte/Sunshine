/**
 * @file src/platform/macos/sc_video.m
 * @brief Definitions for ScreenCaptureKit video capture on macOS.
 */
// platform includes
#import <CoreVideo/CVMetalTextureCache.h>
#import <Metal/Metal.h>

// local includes
#import "sc_video.h"

/**
 * While the screen is static ScreenCaptureKit sends no frames, but Sunshine's capture loop
 * only notices a stop request when it handles a frame (AVCaptureScreenInput streamed at a
 * constant rate). Re-deliver the last frame at this interval while idle.
 */
static const uint64_t kIdleRepeatNs = 100 * NSEC_PER_MSEC;

const float sc_hdr_sdr_white_nits = 203.0f;

/**
 * @brief Metal kernel that scales PQ (BT.2020) 10-bit 4:2:0 video by a linear-light gain.
 *
 * One thread handles a 2x2 block: it converts each pixel to R'G'B', applies the gain in
 * linear light and re-encodes to PQ, then writes the four lumas and the averaged chroma.
 */
static NSString *const kPqGainKernel =
  @"#include <metal_stdlib>\n"
   "using namespace metal;\n"
   "constant float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 32.0, c1 = 3424.0 / 4096.0, c2 = 2413.0 / 128.0, c3 = 2392.0 / 128.0;\n"
   "constant float s = 65535.0 / 64.0;\n"
   "float pq_eotf(float e) { float p = pow(clamp(e, 0.0, 1.0), 1.0 / m2); return pow(max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1); }\n"
   "float pq_oetf(float l) { float p = pow(clamp(l, 0.0, 1.0), m1); return pow((c1 + c2 * p) / (1.0 + c3 * p), m2); }\n"
   "kernel void pq_gain(texture2d<float, access::read> srcY [[texture(0)]], texture2d<float, access::read> srcC [[texture(1)]],\n"
   "                    texture2d<float, access::write> dstY [[texture(2)]], texture2d<float, access::write> dstC [[texture(3)]],\n"
   "                    constant float &gain [[buffer(0)]], uint2 gid [[thread_position_in_grid]]) {\n"
   "  if (gid.x >= srcC.get_width() || gid.y >= srcC.get_height()) return;\n"
   "  float2 c = srcC.read(gid).rg * s;\n"
   "  float cb = (c.x - 512.0) / 896.0, cr = (c.y - 512.0) / 896.0;\n"
   "  float2 acc = 0.0;\n"
   "  for (uint i = 0; i < 4; i++) {\n"
   "    uint2 p = gid * 2 + uint2(i & 1, i >> 1);\n"
   "    float y = (srcY.read(p).r * s - 64.0) / 876.0;\n"
   "    float3 rgb = float3(y + 1.4746 * cr, y - 0.16455 * cb - 0.57135 * cr, y + 1.8814 * cb);\n"
   "    rgb = float3(pq_oetf(pq_eotf(rgb.r) * gain), pq_oetf(pq_eotf(rgb.g) * gain), pq_oetf(pq_eotf(rgb.b) * gain));\n"
   "    float yo = dot(rgb, float3(0.2627, 0.6780, 0.0593));\n"
   "    acc += float2((rgb.b - yo) / 1.8814, (rgb.r - yo) / 1.4746);\n"
   "    dstY.write(float4(round(yo * 876.0 + 64.0) / s), p);\n"
   "  }\n"
   "  acc *= 0.25;\n"
   "  dstC.write(float4(round(acc.x * 896.0 + 512.0) / s, round(acc.y * 896.0 + 512.0) / s, 0.0, 0.0), gid);\n"
   "}\n";

/**
 * @brief Make sure `pool` produces Metal-compatible IOSurface buffers matching `src`.
 *
 * @param src Pixel buffer whose format and size the pool must match.
 * @param pool Pool to check, (re)created when it doesn't match.
 * @return `YES` when the pool is ready.
 */
static BOOL ensure_pool(CVPixelBufferRef src, CVPixelBufferPoolRef *pool) {
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
      (id) kCVPixelBufferMetalCompatibilityKey: @YES,
    };
    if (CVPixelBufferPoolCreate(kCFAllocatorDefault, NULL, (CFDictionaryRef) attrs, pool) != kCVReturnSuccess) {
      return NO;
    }
  }
  return YES;
}

/**
 * @brief Copy every plane of `src` into `dst` on the CPU.
 *
 * @param src Source pixel buffer.
 * @param dst Destination pixel buffer with the same format and size.
 */
static void cpu_copy(CVPixelBufferRef src, CVPixelBufferRef dst) {
  CVPixelBufferLockBaseAddress(src, kCVPixelBufferLock_ReadOnly);
  CVPixelBufferLockBaseAddress(dst, 0);
  const size_t planes = CVPixelBufferGetPlaneCount(src);
  for (size_t p = 0; p < (planes ? planes : 1); p++) {
    const uint8_t *s = planes ? CVPixelBufferGetBaseAddressOfPlane(src, p) : CVPixelBufferGetBaseAddress(src);
    uint8_t *d = planes ? CVPixelBufferGetBaseAddressOfPlane(dst, p) : CVPixelBufferGetBaseAddress(dst);
    const size_t s_stride = planes ? CVPixelBufferGetBytesPerRowOfPlane(src, p) : CVPixelBufferGetBytesPerRow(src);
    const size_t d_stride = planes ? CVPixelBufferGetBytesPerRowOfPlane(dst, p) : CVPixelBufferGetBytesPerRow(dst);
    const size_t rows = planes ? CVPixelBufferGetHeightOfPlane(src, p) : CVPixelBufferGetHeight(src);
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
}

/**
 * @brief Wrap one plane of an IOSurface-backed pixel buffer as a Metal texture.
 *
 * @param cache Metal texture cache.
 * @param buffer Pixel buffer.
 * @param plane Plane index.
 * @param format Metal pixel format of the plane.
 * @return Retained CoreVideo Metal texture, or NULL on failure.
 */
static CVMetalTextureRef plane_texture(CVMetalTextureCacheRef cache, CVPixelBufferRef buffer, size_t plane, MTLPixelFormat format) {
  CVMetalTextureRef texture = NULL;
  CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, cache, buffer, NULL, format, CVPixelBufferGetWidthOfPlane(buffer, plane), CVPixelBufferGetHeightOfPlane(buffer, plane), plane, &texture);
  return texture;
}

/**
 * @brief Scale a PQ 10-bit 4:2:0 frame by `gain` in linear light on the GPU.
 *
 * @param src Source pixel buffer (x420, BT.2020 PQ).
 * @param dst Destination pixel buffer with the same format and size.
 * @param gain Linear-light gain.
 * @return `YES` on success; `NO` if Metal is unavailable or the pass failed.
 */
static BOOL gpu_pq_gain(CVPixelBufferRef src, CVPixelBufferRef dst, float gain) {
  static id<MTLDevice> device;
  static id<MTLCommandQueue> queue;
  static id<MTLComputePipelineState> pipeline;
  static CVMetalTextureCacheRef cache;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    device = MTLCreateSystemDefaultDevice();
    if (!device) {
      return;
    }
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:kPqGainKernel options:nil error:&error];
    id<MTLFunction> function = [library newFunctionWithName:@"pq_gain"];
    pipeline = function ? [device newComputePipelineStateWithFunction:function error:&error] : nil;
    if (!pipeline) {
      NSLog(@"PQ gain kernel unavailable: %@", error);
    }
    [function release];
    [library release];
    queue = [device newCommandQueue];
    CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL, device, NULL, &cache);
  });
  if (!pipeline || !cache) {
    return NO;
  }

  CVMetalTextureRef textures[4] = {
    plane_texture(cache, src, 0, MTLPixelFormatR16Unorm),
    plane_texture(cache, src, 1, MTLPixelFormatRG16Unorm),
    plane_texture(cache, dst, 0, MTLPixelFormatR16Unorm),
    plane_texture(cache, dst, 1, MTLPixelFormatRG16Unorm),
  };
  BOOL ok = textures[0] && textures[1] && textures[2] && textures[3];
  if (ok) {
    id<MTLCommandBuffer> commands = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
    [encoder setComputePipelineState:pipeline];
    for (NSUInteger i = 0; i < 4; i++) {
      [encoder setTexture:CVMetalTextureGetTexture(textures[i]) atIndex:i];
    }
    [encoder setBytes:&gain length:sizeof(gain) atIndex:0];
    const MTLSize grid = MTLSizeMake(CVPixelBufferGetWidthOfPlane(src, 1), CVPixelBufferGetHeightOfPlane(src, 1), 1);
    [encoder dispatchThreads:grid threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    [encoder endEncoding];
    [commands commit];
    [commands waitUntilCompleted];
    ok = commands.status == MTLCommandBufferStatusCompleted;
  }
  for (int i = 0; i < 4; i++) {
    if (textures[i]) {
      CFRelease(textures[i]);
    }
  }
  return ok;
}

// Documented in sc_video.h. ScreenCaptureKit owns at most 8 surfaces and stalls once they are all
// held, while Sunshine keeps up to 12 captured images alive, so each frame goes into our own pool
// and the capture surface returns to ScreenCaptureKit right away.
CMSampleBufferRef sc_copy_sample_buffer(CMSampleBufferRef source, CVPixelBufferPoolRef *pool, float pq_gain) {
  CVPixelBufferRef src = CMSampleBufferGetImageBuffer(source);
  CVPixelBufferRef dst = NULL;
  if (!ensure_pool(src, pool) || CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, *pool, &dst) != kCVReturnSuccess) {
    return NULL;
  }

  CFTypeRef transfer = CVBufferCopyAttachment(src, kCVImageBufferTransferFunctionKey, NULL);
  const BOOL is_pq = transfer && CFEqual(transfer, kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ);
  if (transfer) {
    CFRelease(transfer);
  }
  const BOOL scale = is_pq && pq_gain != 1.0f && CVPixelBufferGetPixelFormatType(src) == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
  if (!scale || !gpu_pq_gain(src, dst, pq_gain)) {
    cpu_copy(src, dst);
  }
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

  CMSampleBufferRef copy = sc_copy_sample_buffer(sampleBuffer, &_pool, sc_hdr_sdr_white_nits / 100.0f);
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
