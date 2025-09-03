/**
 * @file tests/unit/platform/test_macos_av_audio.mm
 * @brief Test src/platform/macos/av_audio.*.
 */

// Only compile these tests on macOS
#ifdef __APPLE__

  #include "../../tests_common.h"

  #import <AVFoundation/AVFoundation.h>
  #import <CoreAudio/CATapDescription.h>
  #import <CoreAudio/CoreAudio.h>
  #import <Foundation/Foundation.h>

  // Include the header for the class we're testing
  #import <src/platform/macos/av_audio.h>

/**
 * @brief Test parameters for processSystemAudioIOProc tests.
 * Contains various audio configuration parameters to test different scenarios.
 */
struct ProcessSystemAudioIOProcTestParams {
  UInt32 frameCount;  ///< Number of audio frames to process
  UInt32 channels;  ///< Number of audio channels (1=mono, 2=stereo)
  UInt32 sampleRate;  ///< Sample rate in Hz
  bool useNilInput;  ///< Whether to test with nil input data
  const char *testName;  ///< Descriptive name for the test case
};

/**
 * @brief Test suite for AVAudio class functionality.
 * Parameterized test class for testing Core Audio system tap functionality.
 */
class AVAudioTest: public PlatformTestSuite, public ::testing::WithParamInterface<ProcessSystemAudioIOProcTestParams> {};

/**
 * @brief Test that microphoneNames returns a valid NSArray.
 * Verifies the static method returns a non-nil array object.
 */
TEST_F(AVAudioTest, MicrophoneNamesReturnsArray) {
  NSArray<NSString *> *names = [AVAudio microphoneNames];

  EXPECT_NE(names, nil);  // Should always return an array, even if empty
  EXPECT_TRUE([names isKindOfClass:[NSArray class]]);  // Should be an NSArray
}

/**
 * @brief Test that findMicrophone handles nil input gracefully.
 * Verifies the method returns nil when passed a nil microphone name.
 */
TEST_F(AVAudioTest, FindMicrophoneWithNilNameReturnsNil) {
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wnonnull"
  AVCaptureDevice *device = [AVAudio findMicrophone:nil];
  #pragma clang diagnostic pop
  EXPECT_EQ(device, nil);
}

/**
 * @brief Test that findMicrophone handles empty string input gracefully.
 * Verifies the method returns nil when passed an empty microphone name.
 */
TEST_F(AVAudioTest, FindMicrophoneWithEmptyNameReturnsNil) {
  AVCaptureDevice *device = [AVAudio findMicrophone:@""];
  EXPECT_EQ(device, nil);  // Should return nil for empty string
}

/**
 * @brief Test that findMicrophone handles non-existent microphone names.
 * Verifies the method returns nil when passed an invalid microphone name.
 */
TEST_F(AVAudioTest, FindMicrophoneWithInvalidNameReturnsNil) {
  NSString *invalidName = @"NonExistentMicrophone123456789ABCDEF";
  AVCaptureDevice *device = [AVAudio findMicrophone:invalidName];
  EXPECT_EQ(device, nil);  // Should return nil for non-existent device
}

/**
 * @brief Test that setupMicrophone handles nil device input properly.
 * Verifies the method returns an error code when passed a nil device.
 */
TEST_F(AVAudioTest, SetupMicrophoneWithNilDeviceReturnsError) {
  AVAudio *avAudio = [[AVAudio alloc] init];
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wnonnull"
  int result = [avAudio setupMicrophone:nil sampleRate:48000 frameSize:512 channels:2];
  #pragma clang diagnostic pop
  [avAudio release];
  EXPECT_EQ(result, -1);  // Should fail with nil device
}

/**
 * @brief Test that setupSystemTap validates channel count parameter.
 * Verifies the method returns an error when passed zero channels.
 */
TEST_F(AVAudioTest, SetupSystemTapWithZeroChannelsReturnsError) {
  AVAudio *avAudio = [[AVAudio alloc] init];
  int result = [avAudio setupSystemTap:48000 frameSize:512 channels:0];
  [avAudio release];
  EXPECT_EQ(result, -1);  // Should fail with zero channels
}

/**
 * @brief Test basic AVAudio object lifecycle.
 * Verifies that AVAudio objects can be created and destroyed without issues.
 */
TEST_F(AVAudioTest, AVAudioObjectCreationAndDestruction) {
  AVAudio *avAudio = [[AVAudio alloc] init];
  EXPECT_NE(avAudio, nil);  // Should create successfully
  [avAudio release];  // Should not crash
}

/**
 * @brief Test that multiple AVAudio objects can coexist.
 * Verifies that multiple instances can be created simultaneously.
 */
TEST_F(AVAudioTest, AVAudioMultipleObjectsCanBeCreated) {
  AVAudio *avAudio1 = [[AVAudio alloc] init];
  AVAudio *avAudio2 = [[AVAudio alloc] init];

  EXPECT_NE(avAudio1, nil);
  EXPECT_NE(avAudio2, nil);
  EXPECT_NE(avAudio1, avAudio2);  // Should be different objects

  [avAudio1 release];
  [avAudio2 release];
}

/**
 * @brief Test audio buffer initialization with various channel configurations.
 * Verifies that the audio buffer can be initialized with different channel counts.
 */
TEST_F(AVAudioTest, InitializeAudioBufferSucceeds) {
  AVAudio *avAudio = [[AVAudio alloc] init];

  // Test with various channel counts
  [avAudio initializeAudioBuffer:1];  // Mono
  EXPECT_NE(avAudio->audioSemaphore, nullptr);
  [avAudio cleanupAudioBuffer];

  [avAudio initializeAudioBuffer:2];  // Stereo
  EXPECT_NE(avAudio->audioSemaphore, nullptr);
  [avAudio cleanupAudioBuffer];

  [avAudio initializeAudioBuffer:8];  // 7.1 Surround
  EXPECT_NE(avAudio->audioSemaphore, nullptr);
  [avAudio cleanupAudioBuffer];

  [avAudio release];
}

/**
 * @brief Test audio buffer cleanup functionality.
 * Verifies that cleanup works correctly even with uninitialized buffers.
 */
TEST_F(AVAudioTest, CleanupAudioBufferHandlesNilSignal) {
  AVAudio *avAudio = [[AVAudio alloc] init];

  // Should not crash even if buffer was never initialized
  [avAudio cleanupAudioBuffer];

  // Initialize then cleanup
  [avAudio initializeAudioBuffer:2];
  EXPECT_NE(avAudio->audioSemaphore, nullptr);
  [avAudio cleanupAudioBuffer];
  EXPECT_EQ(avAudio->audioSemaphore, nullptr);

  [avAudio release];
}

/**
 * @brief Test system tap context initialization with valid parameters.
 * Verifies that system tap context can be initialized on supported macOS versions.
 */
TEST_F(AVAudioTest, InitSystemTapContextWithValidParameters) {
  AVAudio *avAudio = [[AVAudio alloc] init];

  int result = [avAudio initializeSystemTapContext:48000 frameSize:512 channels:2];

  // On systems with macOS 14.2+, this should succeed
  NSOperatingSystemVersion minVersion = {14, 2, 0};
  if ([[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:minVersion]) {
    EXPECT_EQ(result, 0);
  } else {
    // On older systems, should fail gracefully
    EXPECT_EQ(result, -1);
  }

  [avAudio release];
}

/**
 * @brief Test system tap context initialization with edge case parameters.
 * Verifies that system tap handles minimum and maximum reasonable audio parameters.
 */
TEST_F(AVAudioTest, InitSystemTapContextWithEdgeCases) {
  AVAudio *avAudio = [[AVAudio alloc] init];

  NSOperatingSystemVersion minVersion = {14, 2, 0};
  if ([[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:minVersion]) {
    // Test with minimum values
    int result1 = [avAudio initializeSystemTapContext:8000 frameSize:64 channels:1];
    EXPECT_EQ(result1, 0);

    // Test with maximum reasonable values
    int result2 = [avAudio initializeSystemTapContext:192000 frameSize:4096 channels:8];
    EXPECT_EQ(result2, 0);
  }

  [avAudio release];
}

/**
 * @brief Test Core Audio tap description creation for different channel configurations.
 * Verifies that system tap descriptions can be created for various channel counts.
 */
TEST_F(AVAudioTest, CreateSystemTapDescriptionForChannels) {
  AVAudio *avAudio = [[AVAudio alloc] init];

  NSOperatingSystemVersion minVersion = {14, 2, 0};
  if ([[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:minVersion]) {
    // Initialize context first
    int initResult = [avAudio initializeSystemTapContext:48000 frameSize:512 channels:2];
    EXPECT_EQ(initResult, 0);

    // Test mono tap description
    CATapDescription *monoTap = [avAudio createSystemTapDescriptionForChannels:1];
    if (monoTap) {
      EXPECT_NE(monoTap, nil);
      // Note: Can't test properties due to forward declaration limitations
      [monoTap release];
    }

    // Test stereo tap description
    CATapDescription *stereoTap = [avAudio createSystemTapDescriptionForChannels:2];
    if (stereoTap) {
      EXPECT_NE(stereoTap, nil);
      // Note: Can't test properties due to forward declaration limitations
      [stereoTap release];
    }
  }

  [avAudio release];
}

/**
 * @brief Test system tap context cleanup functionality.
 * Verifies that system tap context can be cleaned up safely and multiple times.
 */
TEST_F(AVAudioTest, CleanupSystemTapContext) {
  AVAudio *avAudio = [[AVAudio alloc] init];

  NSOperatingSystemVersion minVersion = {14, 2, 0};
  if ([[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:minVersion]) {
    // Test cleanup without initialization (should not crash)
    [avAudio cleanupSystemTapContext:nil];  // Should be safe to call

    // Initialize system tap context
    int initResult = [avAudio initializeSystemTapContext:48000 frameSize:512 channels:2];
    EXPECT_EQ(initResult, 0);

    // Cleanup should work without issues
    [avAudio cleanupSystemTapContext:nil];

    // Multiple cleanup calls should be safe
    [avAudio cleanupSystemTapContext:nil];  // Second call should not crash
    [avAudio cleanupSystemTapContext:nil];  // Third call should not crash

    // Re-initialize after cleanup should work
    int reinitResult = [avAudio initializeSystemTapContext:44100 frameSize:256 channels:1];
    EXPECT_EQ(reinitResult, 0);

    // Final cleanup
    [avAudio cleanupSystemTapContext:nil];
  } else {
    // On older systems, cleanup should still be safe even though init fails
    [avAudio cleanupSystemTapContext:nil];
  }
  [avAudio release];
}

/**
 * @brief Test Core Audio tap mute behavior with hostAudioEnabled property.
 * Verifies that tap descriptions have correct mute behavior based on hostAudioEnabled setting.
 */
TEST_F(AVAudioTest, CoreAudioTapMuteBehavior) {
  AVAudio *avAudio = [[AVAudio alloc] init];

  NSOperatingSystemVersion minVersion = {14, 2, 0};
  if ([[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:minVersion]) {
    // Initialize context first
    int initResult = [avAudio initializeSystemTapContext:48000 frameSize:512 channels:2];
    EXPECT_EQ(initResult, 0);

    // Test with host audio disabled (muted)
    avAudio.hostAudioEnabled = NO;
    CATapDescription *mutedTap = [avAudio createSystemTapDescriptionForChannels:2];
    if (mutedTap) {
      EXPECT_NE(mutedTap, nil);
      // On macOS 14.2+, we should be able to check the mute behavior
      if (@available(macOS 14.2, *)) {
        EXPECT_EQ(mutedTap.muteBehavior, CATapMuted);
      }
      [mutedTap release];
    }

    // Test with host audio enabled (unmuted)
    avAudio.hostAudioEnabled = YES;
    CATapDescription *unmutedTap = [avAudio createSystemTapDescriptionForChannels:2];
    if (unmutedTap) {
      EXPECT_NE(unmutedTap, nil);
      // On macOS 14.2+, we should be able to check the mute behavior
      if (@available(macOS 14.2, *)) {
        EXPECT_EQ(unmutedTap.muteBehavior, CATapUnmuted);
      }
      [unmutedTap release];
    }

    // Cleanup
    [avAudio cleanupSystemTapContext:nil];
  }

  [avAudio release];
}

// Type alias for parameterized cleanup system tap context tests
using CleanupSystemTapContextTest = AVAudioTest;

// Test parameters for cleanup system tap context tests (reusing same configurations)
INSTANTIATE_TEST_SUITE_P(
  AVAudioTest,
  CleanupSystemTapContextTest,
  ::testing::Values(
    // Representative subset focusing on different channel configurations
    ProcessSystemAudioIOProcTestParams {512, 1, 48000, false, "CleanupMono48kHz512Frames"},
    ProcessSystemAudioIOProcTestParams {512, 2, 48000, false, "CleanupStereo48kHz512Frames"},
    ProcessSystemAudioIOProcTestParams {256, 4, 48000, false, "CleanupQuad48kHz256Frames"},
    ProcessSystemAudioIOProcTestParams {512, 6, 44100, false, "Cleanup51Surround44kHz512Frames"},
    ProcessSystemAudioIOProcTestParams {240, 8, 48000, false, "Cleanup71Surround48kHz240Frames"},
    ProcessSystemAudioIOProcTestParams {128, 1, 22050, false, "CleanupMono22kHz128Frames"},
    ProcessSystemAudioIOProcTestParams {1024, 2, 96000, false, "CleanupStereo96kHz1024Frames"},
    ProcessSystemAudioIOProcTestParams {128, 8, 192000, false, "Cleanup71Surround192kHz128Frames"}
  ),
  [](const ::testing::TestParamInfo<ProcessSystemAudioIOProcTestParams> &info) {
    return std::string(info.param.testName);
  }
);

/**
 * @brief Parameterized test for system tap context cleanup with various audio configurations.
 * Tests init/cleanup cycles across different channel counts, sample rates, and frame sizes.
 */
TEST_P(CleanupSystemTapContextTest, CleanupSystemTapContextParameterized) {
  ProcessSystemAudioIOProcTestParams params = GetParam();

  AVAudio *avAudio = [[AVAudio alloc] init];

  NSOperatingSystemVersion minVersion = {14, 2, 0};
  if ([[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:minVersion]) {
    // Test initialization with the parameterized configuration
    int initResult = [avAudio initializeSystemTapContext:params.sampleRate
                                               frameSize:params.frameCount
                                                channels:params.channels];
    EXPECT_EQ(initResult, 0) << "Failed to initialize system tap context for " << params.testName;

    // Test cleanup after successful initialization
    [avAudio cleanupSystemTapContext:nil];

    // Test re-initialization after cleanup (should work)
    int reinitResult = [avAudio initializeSystemTapContext:params.sampleRate
                                                 frameSize:params.frameCount
                                                  channels:params.channels];
    EXPECT_EQ(reinitResult, 0) << "Failed to re-initialize system tap context after cleanup for " << params.testName;

    // Test multiple cleanup calls (should be safe)
    [avAudio cleanupSystemTapContext:nil];
    [avAudio cleanupSystemTapContext:nil];  // Second call should not crash

    // Test cleanup without prior initialization (should be safe)
    [avAudio cleanupSystemTapContext:nil];
  } else {
    // On older systems, cleanup should still be safe even though init fails
    [avAudio cleanupSystemTapContext:nil];
  }

  [avAudio release];
}

/**
 * @brief Test system tap context cleanup with tap description object.
 * Verifies cleanup works properly when a tap description is provided.
 */
TEST_F(AVAudioTest, CleanupSystemTapContextWithTapDescription) {
  AVAudio *avAudio = [[AVAudio alloc] init];

  NSOperatingSystemVersion minVersion = {14, 2, 0};
  if ([[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:minVersion]) {
    // Initialize system tap context
    int initResult = [avAudio initializeSystemTapContext:48000 frameSize:512 channels:2];
    EXPECT_EQ(initResult, 0);

    // Create a tap description
    CATapDescription *tapDescription = [avAudio createSystemTapDescriptionForChannels:2];
    if (tapDescription) {
      EXPECT_NE(tapDescription, nil);

      // Test cleanup with the tap description object
      [avAudio cleanupSystemTapContext:tapDescription];
      // Note: tapDescription should be released by the cleanup method
    } else {
      // If tap description creation failed, just cleanup normally
      [avAudio cleanupSystemTapContext:nil];
    }

    // Additional cleanup should be safe
    [avAudio cleanupSystemTapContext:nil];
  }

  [avAudio release];
}

/**
 * @brief Test audio converter complex input callback with valid data.
 * Verifies that the audio converter callback properly processes valid audio data.
 */
TEST_F(AVAudioTest, AudioConverterComplexInputProcHandlesValidData) {
  AVAudio *avAudio = [[AVAudio alloc] init];

  // Create test input data
  UInt32 frameCount = 256;
  UInt32 channels = 2;
  UInt32 sampleRate = 48000;
  float *testData = (float *) calloc(frameCount * channels, sizeof(float));

  // Fill with test sine wave data (different frequency per channel) - same as parameterized test
  for (UInt32 frame = 0; frame < frameCount; frame++) {
    for (UInt32 channel = 0; channel < channels; channel++) {
      // Generate different frequencies for each channel for testing
      // Channel 0: 440Hz, Channel 1: 880Hz, Channel 2: 1320Hz, etc.
      double frequency = 440.0 * (channel + 1);
      testData[frame * channels + channel] =
        (float) (sin(2.0 * M_PI * frequency * frame / (double) sampleRate) * 0.5);
    }
  }

  AudioConverterInputData inputInfo = {0};
  inputInfo.inputData = testData;
  inputInfo.inputFrames = frameCount;
  inputInfo.framesProvided = 0;
  inputInfo.deviceChannels = channels;
  inputInfo.avAudio = avAudio;

  // Test the method
  UInt32 requestedPackets = 128;
  AudioBufferList bufferList = {0};
  // Use a dummy AudioConverterRef (can be null for our test since our implementation doesn't use it)
  AudioConverterRef dummyConverter = nullptr;
  OSStatus result = platf::audioConverterComplexInputProc(dummyConverter, &requestedPackets, &bufferList, nullptr, &inputInfo);

  EXPECT_EQ(result, noErr);
  EXPECT_EQ(requestedPackets, 128);  // Should provide requested frames
  EXPECT_EQ(inputInfo.framesProvided, 128);  // Should update frames provided

  free(testData);
  [avAudio release];
}

/**
 * @brief Test audio converter callback when no more data is available.
 * Verifies that the callback handles end-of-data scenarios correctly.
 */
TEST_F(AVAudioTest, AudioConverterComplexInputProcHandlesNoMoreData) {
  AVAudio *avAudio = [[AVAudio alloc] init];

  UInt32 frameCount = 256;
  UInt32 channels = 2;
  float *testData = (float *) calloc(frameCount * channels, sizeof(float));

  AudioConverterInputData inputInfo = {0};
  inputInfo.inputData = testData;
  inputInfo.inputFrames = frameCount;
  inputInfo.framesProvided = frameCount;  // Already provided all frames
  inputInfo.deviceChannels = channels;
  inputInfo.avAudio = avAudio;

  UInt32 requestedPackets = 128;
  AudioBufferList bufferList = {0};
  // Use a dummy AudioConverterRef (can be null for our test since our implementation doesn't use it)
  AudioConverterRef dummyConverter = nullptr;
  OSStatus result = platf::audioConverterComplexInputProc(dummyConverter, &requestedPackets, &bufferList, nullptr, &inputInfo);

  EXPECT_EQ(result, noErr);
  EXPECT_EQ(requestedPackets, 0);  // Should return 0 packets when no more data

  free(testData);
  [avAudio release];
}

/**
 * @brief Test that audio buffer cleanup can be called multiple times safely.
 * Verifies that repeated cleanup calls don't cause crashes or issues.
 */
TEST_F(AVAudioTest, CleanupAudioBufferMultipleCalls) {
  AVAudio *avAudio = [[AVAudio alloc] init];

  [avAudio initializeAudioBuffer:2];
  EXPECT_NE(avAudio->audioSemaphore, nullptr);

  // Multiple cleanup calls should not crash
  [avAudio cleanupAudioBuffer];
  EXPECT_EQ(avAudio->audioSemaphore, nullptr);

  [avAudio cleanupAudioBuffer];  // Second call should be safe
  [avAudio cleanupAudioBuffer];  // Third call should be safe

  [avAudio release];
}

/**
 * @brief Test buffer management with edge case channel configurations.
 * Verifies that buffer management works with minimum and maximum channel counts.
 */
TEST_F(AVAudioTest, BufferManagementEdgeCases) {
  AVAudio *avAudio = [[AVAudio alloc] init];

  // Test with minimum reasonable channel count (1 channel)
  [avAudio initializeAudioBuffer:1];
  EXPECT_NE(avAudio->audioSemaphore, nullptr);
  [avAudio cleanupAudioBuffer];

  // Test with very high channel count
  [avAudio initializeAudioBuffer:32];
  EXPECT_NE(avAudio->audioSemaphore, nullptr);
  [avAudio cleanupAudioBuffer];

  [avAudio release];
}

// Type alias for parameterized audio processing tests
using ProcessSystemAudioIOProcTest = AVAudioTest;

// Test parameters - covering various audio configurations
INSTANTIATE_TEST_SUITE_P(
  AVAudioTest,
  ProcessSystemAudioIOProcTest,
  ::testing::Values(
    // Mono channel variants
    ProcessSystemAudioIOProcTestParams {240, 1, 48000, false, "ValidMono48kHz240Frames"},
    ProcessSystemAudioIOProcTestParams {512, 1, 44100, false, "ValidMono44kHz512Frames"},
    ProcessSystemAudioIOProcTestParams {1024, 1, 96000, false, "ValidMono96kHz1024Frames"},
    ProcessSystemAudioIOProcTestParams {128, 1, 22050, false, "ValidMono22kHz128Frames"},

    // Stereo channel variants
    ProcessSystemAudioIOProcTestParams {240, 2, 48000, false, "ValidStereo48kHz240Frames"},
    ProcessSystemAudioIOProcTestParams {480, 2, 48000, false, "ValidStereo48kHz480Frames"},
    ProcessSystemAudioIOProcTestParams {512, 2, 44100, false, "ValidStereo44kHz512Frames"},

    // Quad (4 channel) variants
    ProcessSystemAudioIOProcTestParams {256, 4, 48000, false, "ValidQuad48kHz256Frames"},
    ProcessSystemAudioIOProcTestParams {512, 4, 44100, false, "ValidQuad44kHz512Frames"},
    ProcessSystemAudioIOProcTestParams {1024, 4, 96000, false, "ValidQuad96kHz1024Frames"},
    ProcessSystemAudioIOProcTestParams {128, 4, 22050, false, "ValidQuad22kHz128Frames"},

    // 5.1 Surround (6 channel) variants
    ProcessSystemAudioIOProcTestParams {240, 6, 48000, false, "Valid51Surround48kHz240Frames"},
    ProcessSystemAudioIOProcTestParams {512, 6, 44100, false, "Valid51Surround44kHz512Frames"},
    ProcessSystemAudioIOProcTestParams {1024, 6, 96000, false, "Valid51Surround96kHz1024Frames"},
    ProcessSystemAudioIOProcTestParams {256, 6, 88200, false, "Valid51Surround88kHz256Frames"},

    // 7.1 Surround (8 channel) variants
    ProcessSystemAudioIOProcTestParams {240, 8, 48000, false, "Valid71Surround48kHz240Frames"},
    ProcessSystemAudioIOProcTestParams {512, 8, 44100, false, "Valid71Surround44kHz512Frames"},
    ProcessSystemAudioIOProcTestParams {1024, 8, 96000, false, "Valid71Surround96kHz1024Frames"},
    ProcessSystemAudioIOProcTestParams {128, 8, 192000, false, "Valid71Surround192kHz128Frames"},

    // Edge cases with various configurations
    ProcessSystemAudioIOProcTestParams {240, 2, 48000, true, "NilInputHandlesGracefully"},
    ProcessSystemAudioIOProcTestParams {64, 2, 8000, false, "ValidStereo8kHz64Frames"},
    ProcessSystemAudioIOProcTestParams {2048, 1, 48000, false, "ValidMono48kHz2048Frames"},
    ProcessSystemAudioIOProcTestParams {32, 4, 176400, false, "ValidQuad176kHz32Frames"},
    ProcessSystemAudioIOProcTestParams {128, 6, 44100, false, "Valid51Surround44kHz128Frames"}  // Reduced from 4096 to fit buffer
  ),
  [](const ::testing::TestParamInfo<ProcessSystemAudioIOProcTestParams> &info) {
    return std::string(info.param.testName);
  }
);

TEST_P(ProcessSystemAudioIOProcTest, ProcessSystemAudioIOProc) {
  ProcessSystemAudioIOProcTestParams params = GetParam();

  AVAudio *avAudio = [[AVAudio alloc] init];

  // Use the new buffer initialization method instead of manual setup
  [avAudio initializeAudioBuffer:params.channels];

  // Create timestamps
  AudioTimeStamp timeStamp = {0};
  timeStamp.mFlags = kAudioTimeStampSampleTimeValid;
  timeStamp.mSampleTime = 0;

  AudioBufferList *inputBufferList = nullptr;
  float *testInputData = nullptr;
  UInt32 inputDataSize = 0;

  // Only create input data if not testing nil input
  if (!params.useNilInput) {
    inputDataSize = params.frameCount * params.channels * sizeof(float);
    testInputData = (float *) calloc(params.frameCount * params.channels, sizeof(float));

    // Fill with test sine wave data (different frequency per channel)
    for (UInt32 frame = 0; frame < params.frameCount; frame++) {
      for (UInt32 channel = 0; channel < params.channels; channel++) {
        // Generate different frequencies for each channel for testing
        // Channel 0: 440Hz, Channel 1: 880Hz, Channel 2: 1320Hz, etc.
        double frequency = 440.0 * (channel + 1);
        testInputData[frame * params.channels + channel] =
          (float) (sin(2.0 * M_PI * frequency * frame / (double) params.sampleRate) * 0.5);
      }
    }

    // Create AudioBufferList
    inputBufferList = (AudioBufferList *) malloc(sizeof(AudioBufferList));
    inputBufferList->mNumberBuffers = 1;
    inputBufferList->mBuffers[0].mNumberChannels = params.channels;
    inputBufferList->mBuffers[0].mDataByteSize = inputDataSize;
    inputBufferList->mBuffers[0].mData = testInputData;
  }

  // Get initial buffer state
  uint32_t initialAvailableBytes = 0;
  TPCircularBufferTail(&avAudio->audioSampleBuffer, &initialAvailableBytes);

  // Create IOProc data structure for the C++ function
  AVAudioIOProcData procData = {0};
  procData.avAudio = avAudio;
  procData.clientRequestedChannels = params.channels;
  procData.clientRequestedFrameSize = params.frameCount;
  procData.clientRequestedSampleRate = params.sampleRate;
  procData.aggregateDeviceChannels = params.channels;  // For simplicity in tests
  procData.aggregateDeviceSampleRate = params.sampleRate;
  procData.audioConverter = nullptr;  // No conversion needed for most tests

  // Create a dummy output buffer (not used in our implementation but required by signature)
  AudioBufferList dummyOutputBufferList = {0};

  // Test the systemAudioIOProcWrapper function
  OSStatus result = platf::systemAudioIOProc(0,  // device ID (not used in our logic)
                                             &timeStamp,
                                             inputBufferList,
                                             &timeStamp,
                                             &dummyOutputBufferList,
                                             &timeStamp,
                                             &procData);

  // Verify the method returns success
  EXPECT_EQ(result, noErr);

  if (!params.useNilInput) {
    // Verify data was written to the circular buffer
    uint32_t finalAvailableBytes = 0;
    void *bufferData = TPCircularBufferTail(&avAudio->audioSampleBuffer, &finalAvailableBytes);
    EXPECT_GT(finalAvailableBytes, initialAvailableBytes);  // Should have more data than before
    EXPECT_GT(finalAvailableBytes, 0);  // Should have data in buffer

    // Verify we wrote the expected amount of data (input size for direct passthrough)
    EXPECT_EQ(finalAvailableBytes, inputDataSize);

    // Verify the actual audio data matches what we put in (first few samples)
    // Test up to 16 samples or 4 complete frames, whichever is smaller
    UInt32 samplesToTest = std::min(16U, params.channels * 4);  // Up to 4 frames worth
    if (bufferData && finalAvailableBytes >= sizeof(float) * samplesToTest) {
      float *outputSamples = (float *) bufferData;
      for (UInt32 i = 0; i < samplesToTest; i++) {
        EXPECT_FLOAT_EQ(outputSamples[i], testInputData[i]) << "Sample " << i << " mismatch";
      }
    }
  }

  // Cleanup
  if (testInputData) {
    free(testInputData);
  }
  if (inputBufferList) {
    free(inputBufferList);
  }
  [avAudio cleanupAudioBuffer];
  [avAudio release];
}

#endif  // __APPLE__
