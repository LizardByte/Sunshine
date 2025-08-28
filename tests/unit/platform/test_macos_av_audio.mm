/**
 * @file tests/unit/platform/test_macos_av_audio.mm
 * @brief Test src/platform/macos/av_audio.*.
 */

// Only compile these tests on macOS
#ifdef __APPLE__

#include "../../tests_common.h"

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>

// Include the header for the class we're testing
#import <src/platform/macos/av_audio.h>

// C++ Test Cases using GoogleTest

// Test parameters for processSystemAudioIOProc tests
struct ProcessSystemAudioIOProcTestParams {
  UInt32 frameCount;
  UInt32 channels;
  UInt32 sampleRate;
  bool useNilInput;
  const char* testName;
};

// Make AVAudioTest itself parameterized for the processSystemAudioIOProc tests
class AVAudioTest : public PlatformTestSuite, public ::testing::WithParamInterface<ProcessSystemAudioIOProcTestParams> {};

TEST_F(AVAudioTest, MicrophoneNamesReturnsArray) {
  NSArray<NSString*>* names = [AVAudio microphoneNames];

  EXPECT_NE(names, nil); // Should always return an array, even if empty
  EXPECT_TRUE([names isKindOfClass:[NSArray class]]); // Should be an NSArray
}

TEST_F(AVAudioTest, FindMicrophoneWithNilNameReturnsNil) {
  AVCaptureDevice* device = [AVAudio findMicrophone:nil];
  EXPECT_EQ(device, nil);
}

TEST_F(AVAudioTest, FindMicrophoneWithEmptyNameReturnsNil) {
  AVCaptureDevice* device = [AVAudio findMicrophone:@""];
  EXPECT_EQ(device, nil); // Should return nil for empty string
}

TEST_F(AVAudioTest, FindMicrophoneWithInvalidNameReturnsNil) {
  NSString* invalidName = @"NonExistentMicrophone123456789ABCDEF";
  AVCaptureDevice* device = [AVAudio findMicrophone:invalidName];
  EXPECT_EQ(device, nil); // Should return nil for non-existent device
}

TEST_F(AVAudioTest, SetupMicrophoneWithNilDeviceReturnsError) {
  AVAudio* avAudio = [[AVAudio alloc] init];
  int result = [avAudio setupMicrophone:nil sampleRate:48000 frameSize:512 channels:2];
  [avAudio release];
  EXPECT_EQ(result, -1); // Should fail with nil device
}

TEST_F(AVAudioTest, SetupSystemTapWithZeroChannelsReturnsError) {
  AVAudio* avAudio = [[AVAudio alloc] init];
  int result = [avAudio setupSystemTap:48000 frameSize:512 channels:0];
  [avAudio release];
  EXPECT_EQ(result, -1); // Should fail with zero channels
}

TEST_F(AVAudioTest, AVAudioObjectCreationAndDestruction) {
  AVAudio* avAudio = [[AVAudio alloc] init];
  EXPECT_NE(avAudio, nil); // Should create successfully
  [avAudio release]; // Should not crash
}

TEST_F(AVAudioTest, AVAudioMultipleObjectsCanBeCreated) {
  AVAudio* avAudio1 = [[AVAudio alloc] init];
  AVAudio* avAudio2 = [[AVAudio alloc] init];
  
  EXPECT_NE(avAudio1, nil);
  EXPECT_NE(avAudio2, nil);
  EXPECT_NE(avAudio1, avAudio2); // Should be different objects
  
  [avAudio1 release];
  [avAudio2 release];
}


// Type alias for parameterized audio processing tests
using ProcessSystemAudioIOProcTest = AVAudioTest;

// Test parameters - covering various audio configurations
INSTANTIATE_TEST_SUITE_P(
  AVAudioTest,
  ProcessSystemAudioIOProcTest,
  ::testing::Values(
    // Original test cases
    ProcessSystemAudioIOProcTestParams{240, 2, 48000, false, "ValidStereo48kHz240Frames"},
    ProcessSystemAudioIOProcTestParams{240, 2, 48000, true, "NilInputHandlesGracefully"},
    ProcessSystemAudioIOProcTestParams{480, 2, 48000, false, "ValidStereo48kHz480Frames"},
    ProcessSystemAudioIOProcTestParams{512, 2, 44100, false, "ValidStereo44kHz512Frames"},
    
    // Mono channel variants
    ProcessSystemAudioIOProcTestParams{240, 1, 48000, false, "ValidMono48kHz240Frames"},
    ProcessSystemAudioIOProcTestParams{512, 1, 44100, false, "ValidMono44kHz512Frames"},
    ProcessSystemAudioIOProcTestParams{1024, 1, 96000, false, "ValidMono96kHz1024Frames"},
    ProcessSystemAudioIOProcTestParams{128, 1, 22050, false, "ValidMono22kHz128Frames"},
    
    // Quad (4 channel) variants
    ProcessSystemAudioIOProcTestParams{256, 4, 48000, false, "ValidQuad48kHz256Frames"},
    ProcessSystemAudioIOProcTestParams{512, 4, 44100, false, "ValidQuad44kHz512Frames"},
    ProcessSystemAudioIOProcTestParams{1024, 4, 96000, false, "ValidQuad96kHz1024Frames"},
    ProcessSystemAudioIOProcTestParams{128, 4, 22050, false, "ValidQuad22kHz128Frames"},
    
    // 5.1 Surround (6 channel) variants
    ProcessSystemAudioIOProcTestParams{240, 6, 48000, false, "Valid51Surround48kHz240Frames"},
    ProcessSystemAudioIOProcTestParams{512, 6, 44100, false, "Valid51Surround44kHz512Frames"},
    ProcessSystemAudioIOProcTestParams{1024, 6, 96000, false, "Valid51Surround96kHz1024Frames"},
    ProcessSystemAudioIOProcTestParams{256, 6, 88200, false, "Valid51Surround88kHz256Frames"},
    
    // 7.1 Surround (8 channel) variants
    ProcessSystemAudioIOProcTestParams{240, 8, 48000, false, "Valid71Surround48kHz240Frames"},
    ProcessSystemAudioIOProcTestParams{512, 8, 44100, false, "Valid71Surround44kHz512Frames"},
    ProcessSystemAudioIOProcTestParams{1024, 8, 96000, false, "Valid71Surround96kHz1024Frames"},
    ProcessSystemAudioIOProcTestParams{128, 8, 192000, false, "Valid71Surround192kHz128Frames"},
    
    // Edge cases with various configurations
    ProcessSystemAudioIOProcTestParams{64, 2, 8000, false, "ValidStereo8kHz64Frames"},
    ProcessSystemAudioIOProcTestParams{2048, 1, 48000, false, "ValidMono48kHz2048Frames"},
    ProcessSystemAudioIOProcTestParams{32, 4, 176400, false, "ValidQuad176kHz32Frames"},
    ProcessSystemAudioIOProcTestParams{128, 6, 44100, false, "Valid51Surround44kHz128Frames"}  // Reduced from 4096 to fit buffer
  ),
  [](const ::testing::TestParamInfo<ProcessSystemAudioIOProcTestParams>& info) {
    return std::string(info.param.testName);
  }
);

TEST_P(ProcessSystemAudioIOProcTest, ProcessSystemAudioIOProc) {
  ProcessSystemAudioIOProcTestParams params = GetParam();
  
  AVAudio* avAudio = [[AVAudio alloc] init];
  
  // Use the new buffer initialization method instead of manual setup
  [avAudio initializeAudioBuffer:params.channels];
  
  // Create timestamps
  AudioTimeStamp timeStamp = {0};
  timeStamp.mFlags = kAudioTimeStampSampleTimeValid;
  timeStamp.mSampleTime = 0;
  
  AudioBufferList* inputBufferList = nullptr;
  float* testInputData = nullptr;
  UInt32 inputDataSize = 0;
  
  // Only create input data if not testing nil input
  if (!params.useNilInput) {
    inputDataSize = params.frameCount * params.channels * sizeof(float);
    testInputData = (float*)calloc(params.frameCount * params.channels, sizeof(float));
    
    // Fill with test sine wave data (different frequency per channel)
    for (UInt32 frame = 0; frame < params.frameCount; frame++) {
      for (UInt32 channel = 0; channel < params.channels; channel++) {
        // Generate different frequencies for each channel for testing
        // Channel 0: 440Hz, Channel 1: 880Hz, Channel 2: 1320Hz, etc.
        double frequency = 440.0 * (channel + 1);
        testInputData[frame * params.channels + channel] = 
          (float)(sin(2.0 * M_PI * frequency * frame / (double)params.sampleRate) * 0.5);
      }
    }
    
    // Create AudioBufferList
    inputBufferList = (AudioBufferList*)malloc(sizeof(AudioBufferList));
    inputBufferList->mNumberBuffers = 1;
    inputBufferList->mBuffers[0].mNumberChannels = params.channels;
    inputBufferList->mBuffers[0].mDataByteSize = inputDataSize;
    inputBufferList->mBuffers[0].mData = testInputData;
  }
  
  // Get initial buffer state
  uint32_t initialAvailableBytes = 0;
  TPCircularBufferTail(&avAudio->audioSampleBuffer, &initialAvailableBytes);
  
  // Test the processSystemAudioIOProc method
  OSStatus result = [avAudio processSystemAudioIOProc:0 // device ID (not used in our logic)
                                                inNow:&timeStamp
                                          inInputData:inputBufferList
                                          inInputTime:&timeStamp
                                        outOutputData:nil // not used in our implementation
                                         inOutputTime:&timeStamp
                                       clientChannels:params.channels
                                      clientFrameSize:params.frameCount
                                     clientSampleRate:params.sampleRate];
  
  // Verify the method returns success
  EXPECT_EQ(result, noErr);
  
  if (!params.useNilInput) {
    // Verify data was written to the circular buffer
    uint32_t finalAvailableBytes = 0;
    void* bufferData = TPCircularBufferTail(&avAudio->audioSampleBuffer, &finalAvailableBytes);
    EXPECT_GT(finalAvailableBytes, initialAvailableBytes); // Should have more data than before
    EXPECT_GT(finalAvailableBytes, 0); // Should have data in buffer
    
    // Verify we wrote the expected amount of data (input size for direct passthrough)
    EXPECT_EQ(finalAvailableBytes, inputDataSize);
    
    // Verify the actual audio data matches what we put in (first few samples)
    if (bufferData && finalAvailableBytes >= sizeof(float) * std::min(4U, params.channels * 2)) {
      float* outputSamples = (float*)bufferData;
      for (UInt32 i = 0; i < std::min(4U, params.channels * 2); i++) {
        EXPECT_FLOAT_EQ(outputSamples[i], testInputData[i]) << "Sample " << i << " mismatch";
      }
    }
  }
  
  // Cleanup
  if (testInputData) free(testInputData);
  if (inputBufferList) free(inputBufferList);
  [avAudio cleanupAudioBuffer];
  [avAudio release];
}



#endif // __APPLE__
