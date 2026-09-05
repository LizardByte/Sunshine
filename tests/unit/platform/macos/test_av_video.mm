/**
 * @file tests/unit/platform/macos/test_av_video.mm
 * @brief Unit tests for serialized macOS video capture teardown.
 */

// Only compile these tests on macOS
#ifdef __APPLE__

  #include "../../../tests_common.h"

  #import <AVFoundation/AVFoundation.h>
  #import <src/platform/macos/av_video.h>

/**
 * @brief Minimal capture session double that records lifecycle calls.
 */
@interface FakeCaptureSession: NSObject

@property (atomic, assign) NSUInteger startRunningCount;  ///< Number of startRunning calls.
@property (atomic, assign) NSUInteger stopRunningCount;  ///< Number of stopRunning calls.
@property (atomic, assign) NSUInteger removeOutputCount;  ///< Number of removeOutput calls.

@end

@implementation FakeCaptureSession

- (void)startRunning {
  self.startRunningCount++;
}

- (void)stopRunning {
  self.stopRunningCount++;
}

- (void)removeOutput:(AVCaptureOutput *)output {
  (void) output;
  self.removeOutputCount++;
}

@end

/**
 * @brief Fixture that installs a synthetic capture without accessing display hardware.
 */
class AVVideoTest: public PlatformTestSuite {
protected:
  AVVideo *video {};  ///< Video capture object under test.
  FakeCaptureSession *session {};  ///< Session double used by the capture object.
  AVCaptureConnection *connection {};  ///< Synthetic map key representing a capture connection.
  AVCaptureVideoDataOutput *video_output {};  ///< Video output that owns the callback queue.
  dispatch_queue_t callback_queue {};  ///< Serial queue used for frame callbacks.
  dispatch_semaphore_t capture_signal {};  ///< Semaphore returned to the capture wait loop.
  CMSampleBufferRef sample_buffer {};  ///< Empty sample buffer passed to the capture callback.

  void SetUp() override {
    video = [[AVVideo alloc] init];
    session = [[FakeCaptureSession alloc] init];
    connection = (AVCaptureConnection *) [[NSObject alloc] init];
    video_output = [[AVCaptureVideoDataOutput alloc] init];
    callback_queue = dispatch_queue_create("testAVVideoCallbackQueue", DISPATCH_QUEUE_SERIAL);
    capture_signal = dispatch_semaphore_create(0);
    ASSERT_EQ(CMSampleBufferCreate(kCFAllocatorDefault, nullptr, true, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, nullptr, &sample_buffer), noErr);
    ASSERT_NE(sample_buffer, nullptr);

    video.session = (AVCaptureSession *) session;
    video.videoOutputs = [[NSMapTable alloc] init];
    video.captureCallbacks = [[NSMapTable alloc] init];
    video.captureSignals = [[NSMapTable alloc] init];

    [video_output setSampleBufferDelegate:video queue:callback_queue];
  }

  void TearDown() override {
    [video_output setSampleBufferDelegate:nil queue:nil];
    [video release];
    [connection release];
    [video_output release];
    [session release];
    dispatch_release(callback_queue);
    dispatch_release(capture_signal);
    if (sample_buffer != nullptr) {
      CFRelease(sample_buffer);
    }
  }

  /**
   * @brief Register a callback and its capture resources in the object under test.
   *
   * @param callback Frame callback to register.
   */
  void register_capture(FrameCallbackBlock callback) {
    [video.videoOutputs setObject:video_output forKey:connection];
    [video.captureCallbacks setObject:callback forKey:connection];
    [video.captureSignals setObject:capture_signal forKey:connection];
  }
};

/**
 * @test Verify forced teardown waits for an in-flight callback and only tears down once.
 */
TEST_F(AVVideoTest, StopCaptureWaitsForInFlightCallback) {
  dispatch_semaphore_t callback_entered = dispatch_semaphore_create(0);
  dispatch_semaphore_t release_callback = dispatch_semaphore_create(0);
  dispatch_semaphore_t stop_started = dispatch_semaphore_create(0);
  dispatch_semaphore_t stop_finished = dispatch_semaphore_create(0);

  register_capture(^bool(CMSampleBufferRef sample_buffer) {
    (void) sample_buffer;
    dispatch_semaphore_signal(callback_entered);
    dispatch_semaphore_wait(release_callback, DISPATCH_TIME_FOREVER);
    return false;
  });

  dispatch_async(callback_queue, ^{
    [video captureOutput:video_output didOutputSampleBuffer:sample_buffer fromConnection:connection];
  });
  ASSERT_EQ(dispatch_semaphore_wait(callback_entered, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)), 0);

  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    dispatch_semaphore_signal(stop_started);
    [video stopCapture:capture_signal];
    dispatch_semaphore_signal(stop_finished);
  });
  ASSERT_EQ(dispatch_semaphore_wait(stop_started, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)), 0);
  EXPECT_NE(dispatch_semaphore_wait(stop_finished, dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC)), 0);

  dispatch_semaphore_signal(release_callback);
  ASSERT_EQ(dispatch_semaphore_wait(stop_finished, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)), 0);

  EXPECT_EQ(dispatch_semaphore_wait(capture_signal, DISPATCH_TIME_NOW), 0);
  EXPECT_EQ(video.videoOutputs.count, 0);
  EXPECT_EQ(video.captureCallbacks.count, 0);
  EXPECT_EQ(video.captureSignals.count, 0);
  EXPECT_EQ(session.stopRunningCount, 1);
  EXPECT_EQ(session.removeOutputCount, 1);
  EXPECT_EQ(session.startRunningCount, 1);

  dispatch_release(callback_entered);
  dispatch_release(release_callback);
  dispatch_release(stop_started);
  dispatch_release(stop_finished);
}

/**
 * @test Verify forced teardown is idempotent and does not report normal callback completion.
 */
TEST_F(AVVideoTest, StopCaptureWithoutCallbackIsIdempotent) {
  __block bool callback_invoked = false;
  register_capture(^bool(CMSampleBufferRef sample_buffer) {
    (void) sample_buffer;
    callback_invoked = true;
    return false;
  });

  [video_output setSampleBufferDelegate:nil queue:nil];
  [video stopCapture:capture_signal];
  [video stopCapture:capture_signal];
  [video captureOutput:video_output didOutputSampleBuffer:sample_buffer fromConnection:connection];

  EXPECT_FALSE(callback_invoked);
  EXPECT_NE(dispatch_semaphore_wait(capture_signal, DISPATCH_TIME_NOW), 0);
  EXPECT_EQ(video.videoOutputs.count, 0);
  EXPECT_EQ(video.captureCallbacks.count, 0);
  EXPECT_EQ(video.captureSignals.count, 0);
  EXPECT_EQ(session.stopRunningCount, 1);
  EXPECT_EQ(session.removeOutputCount, 1);
  EXPECT_EQ(session.startRunningCount, 1);
}

/**
 * @test Verify a callback that accepts a frame leaves the capture active.
 */
TEST_F(AVVideoTest, CaptureOutputKeepsCaptureActive) {
  __block bool callback_invoked = false;
  register_capture(^bool(CMSampleBufferRef sample_buffer) {
    (void) sample_buffer;
    callback_invoked = true;
    return true;
  });

  dispatch_sync(callback_queue, ^{
    [video captureOutput:video_output didOutputSampleBuffer:sample_buffer fromConnection:connection];
  });

  EXPECT_TRUE(callback_invoked);
  EXPECT_NE(dispatch_semaphore_wait(capture_signal, DISPATCH_TIME_NOW), 0);
  EXPECT_EQ(video.videoOutputs.count, 1);
  EXPECT_EQ(video.captureCallbacks.count, 1);
  EXPECT_EQ(video.captureSignals.count, 1);
  EXPECT_EQ(session.stopRunningCount, 0);
  EXPECT_EQ(session.removeOutputCount, 0);
  EXPECT_EQ(session.startRunningCount, 0);

  [video stopCapture:capture_signal];
}

#endif  // __APPLE__
