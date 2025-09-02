/**
 * @file src/platform/macos/av_audio.mm
 * @brief Implementation of macOS audio capture with dual input paths.
 *
 * This file implements the AVAudio class which provides two distinct audio capture methods:
 * 1. **Microphone capture** - Uses AVFoundation framework to capture from specific microphone devices
 * 2. **System-wide audio tap** - Uses Core Audio taps to capture all system audio output (macOS 14.2+)
 *
 * The implementation handles format conversion, real-time audio processing, and provides
 * a unified interface for both capture methods through a shared circular buffer.
 */
#import "av_audio.h"

#include "src/logging.h"
#include "src/utility.h"

#import <AudioToolbox/AudioConverter.h>
#import <CoreAudio/CATapDescription.h>

namespace platf {
  using namespace std::literals;

  /**
   * @brief Real-time AudioConverter input callback for format conversion.
   * Provides audio data to AudioConverter during format conversion process using pure C++ for optimal performance.
   * This function must avoid all Objective-C runtime calls to meet real-time audio constraints.
   * @param inAudioConverter The audio converter requesting input data
   * @param ioNumberDataPackets Number of data packets to provide  
   * @param ioData Buffer list to fill with audio data
   * @param outDataPacketDescription Packet description for output data
   * @param inUserData User data containing AudioConverterInputData structure
   * @return OSStatus indicating success (noErr) or error code
   */
  OSStatus audioConverterComplexInputProc(AudioConverterRef inAudioConverter, UInt32 *ioNumberDataPackets, AudioBufferList *ioData, AudioStreamPacketDescription **outDataPacketDescription, void *inUserData) {
    auto *inputInfo = static_cast<AudioConverterInputData *>(inUserData);
    
    // Check if we've already provided all available frames
    if (inputInfo->framesProvided >= inputInfo->inputFrames) {
      *ioNumberDataPackets = 0;
      return noErr;
    }

    // Calculate how many frames we can provide (don't exceed remaining frames)
    UInt32 framesToProvide = std::min(*ioNumberDataPackets, inputInfo->inputFrames - inputInfo->framesProvided);

    // Set up the output buffer with the audio data
    ioData->mNumberBuffers = 1;
    ioData->mBuffers[0].mNumberChannels = inputInfo->deviceChannels;
    ioData->mBuffers[0].mDataByteSize = framesToProvide * inputInfo->deviceChannels * sizeof(float);
    ioData->mBuffers[0].mData = inputInfo->inputData + (inputInfo->framesProvided * inputInfo->deviceChannels);

    // Update the tracking of how many frames we've provided
    inputInfo->framesProvided += framesToProvide;
    *ioNumberDataPackets = framesToProvide;

    return noErr;
  }

  /**
   * @brief Real-time audio processing function for Core Audio IOProc callbacks.
   * Handles system-wide audio capture with format conversion and buffering using pure C++ for optimal performance.
   * This function must avoid all Objective-C runtime calls to meet real-time audio constraints.
   * @param inDevice The audio device identifier
   * @param inNow Current audio time stamp  
   * @param inInputData Input audio buffer list from the device
   * @param inInputTime Time stamp for input data
   * @param outOutputData Output audio buffer list (not used in our implementation)
   * @param inOutputTime Time stamp for output data
   * @param inClientData Client data containing AVAudioIOProcData structure
   * @return OSStatus indicating success (noErr) or error code
   */
  OSStatus systemAudioIOProc(AudioObjectID inDevice, const AudioTimeStamp *inNow, const AudioBufferList *inInputData, const AudioTimeStamp *inInputTime, AudioBufferList *outOutputData, const AudioTimeStamp *inOutputTime, void *inClientData) {
    auto *procData = static_cast<AVAudioIOProcData *>(inClientData);
    
    // Get required parameters from procData
    UInt32 clientChannels = procData->clientRequestedChannels;
    UInt32 clientFrameSize = procData->clientRequestedFrameSize;
    AVAudio *avAudio = procData->avAudio;
    
    // Always ensure we write to buffer and signal, even if input is empty/invalid
    bool didWriteData = false;

    if (inInputData && inInputData->mNumberBuffers > 0) {
      AudioBuffer inputBuffer = inInputData->mBuffers[0];

      if (inputBuffer.mData && inputBuffer.mDataByteSize > 0) {
        auto *inputSamples = static_cast<float *>(inputBuffer.mData);
        UInt32 deviceChannels = procData->aggregateDeviceChannels;
        UInt32 inputFrames = inputBuffer.mDataByteSize / (deviceChannels * sizeof(float));

        // Use AudioConverter if we need any conversion, otherwise pass through
        if (procData->audioConverter) {
          // Use pre-allocated buffer instead of malloc for real-time safety!
          UInt32 maxOutputFrames = procData->conversionBufferSize / (clientChannels * sizeof(float));
          UInt32 requestedOutputFrames = maxOutputFrames;
          
          AudioConverterInputData inputData = {0};
          inputData.inputData = inputSamples;
          inputData.inputFrames = inputFrames;
          inputData.framesProvided = 0;  // Critical: must start at 0!
          inputData.deviceChannels = deviceChannels;
          inputData.avAudio = avAudio;

          AudioBufferList outputBufferList = {0};
          outputBufferList.mNumberBuffers = 1;
          outputBufferList.mBuffers[0].mNumberChannels = clientChannels;
          outputBufferList.mBuffers[0].mDataByteSize = procData->conversionBufferSize;
          outputBufferList.mBuffers[0].mData = procData->conversionBuffer;
          
          UInt32 outputFrameCount = requestedOutputFrames;
          OSStatus converterStatus = AudioConverterFillComplexBuffer(
            procData->audioConverter,
            audioConverterComplexInputProc,
            &inputData,
            &outputFrameCount,
            &outputBufferList,
            nullptr
          );

          if (converterStatus == noErr && outputFrameCount > 0) {
            // AudioConverter did all the work: sample rate + channels + optimal frame count
            UInt32 actualOutputBytes = outputFrameCount * clientChannels * sizeof(float);
            TPCircularBufferProduceBytes(&avAudio->audioSampleBuffer, procData->conversionBuffer, actualOutputBytes);
            didWriteData = true;
          } else {
            // Fallback: write original data
            TPCircularBufferProduceBytes(&avAudio->audioSampleBuffer, inputBuffer.mData, inputBuffer.mDataByteSize);
            didWriteData = true;
          }
        } else {
          // No conversion needed - direct passthrough
          TPCircularBufferProduceBytes(&avAudio->audioSampleBuffer, inputBuffer.mData, inputBuffer.mDataByteSize);
          didWriteData = true;
        }
      }
    }

    // Always signal, even if we didn't write data (ensures consumer doesn't block)
    if (!didWriteData) {
      // Write silence if no valid input data - use pre-allocated buffer or small stack buffer
      UInt32 silenceFrames = clientFrameSize > 0 ? std::min(clientFrameSize, 2048U) : 512U;
      
      if (procData->conversionBuffer && procData->conversionBufferSize > 0) {
        // Use pre-allocated conversion buffer for silence
        UInt32 maxSilenceFrames = procData->conversionBufferSize / (clientChannels * sizeof(float));
        silenceFrames = std::min(silenceFrames, maxSilenceFrames);
        UInt32 silenceBytes = silenceFrames * clientChannels * sizeof(float);
        
        // Creating actual silence 
        memset(procData->conversionBuffer, 0, silenceBytes);
        TPCircularBufferProduceBytes(&avAudio->audioSampleBuffer, procData->conversionBuffer, silenceBytes);
      } else {
        // Fallback to small stack-allocated buffer for cases without conversion buffer
        float silenceBuffer[512 * 8] = {0}; // Max 512 frames, 8 channels on stack
        UInt32 maxStackFrames = sizeof(silenceBuffer) / (clientChannels * sizeof(float));
        silenceFrames = std::min(silenceFrames, maxStackFrames);
        UInt32 silenceBytes = silenceFrames * clientChannels * sizeof(float);
        
        TPCircularBufferProduceBytes(&avAudio->audioSampleBuffer, silenceBuffer, silenceBytes);
      }
    }

    // Signal new data arrival - using real-time safe C-based semaphore
    // instead of Objective-C NSCondition to meet real-time audio constraints
    dispatch_semaphore_signal(avAudio->audioSemaphore);

    return noErr;
  }
} // namespace platf

@implementation AVAudio

+ (NSArray<AVCaptureDevice *> *)microphones {
  using namespace std::literals;
  BOOST_LOG(debug) << "Discovering microphones"sv;

  if ([[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:((NSOperatingSystemVersion) {10, 15, 0})]) {
    BOOST_LOG(debug) << "Using modern AVCaptureDeviceDiscoverySession API"sv;
    // This will generate a warning about AVCaptureDeviceDiscoverySession being
    // unavailable before macOS 10.15, but we have a guard to prevent it from
    // being called on those earlier systems.
    // Unfortunately the supported way to silence this warning, using @available,
    // produces linker errors for __isPlatformVersionAtLeast, so we have to use
    // a different method.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"
    AVCaptureDeviceDiscoverySession *discoverySession = [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInMicrophone, AVCaptureDeviceTypeExternalUnknown]
                                                                                                               mediaType:AVMediaTypeAudio
                                                                                                                position:AVCaptureDevicePositionUnspecified];
    NSArray *devices = discoverySession.devices;
    BOOST_LOG(debug) << "Found "sv << [devices count] << " devices using discovery session"sv;
    return devices;
#pragma clang diagnostic pop
  } else {
    BOOST_LOG(debug) << "Using legacy AVCaptureDevice API"sv;
    // We're intentionally using a deprecated API here specifically for versions
    // of macOS where it's not deprecated, so we can ignore any deprecation
    // warnings:
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    NSArray *devices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeAudio];
    BOOST_LOG(debug) << "Found "sv << [devices count] << " devices using legacy API"sv;
    return devices;
#pragma clang diagnostic pop
  }
}

+ (NSArray<NSString *> *)microphoneNames {
  using namespace std::literals;
  BOOST_LOG(debug) << "Retrieving microphone names"sv;
  NSMutableArray *result = [[NSMutableArray alloc] init];

  for (AVCaptureDevice *device in [AVAudio microphones]) {
    [result addObject:[device localizedName]];
  }

  BOOST_LOG(info) << "Found "sv << [result count] << " microphones"sv;
  return result;
}

+ (AVCaptureDevice *)findMicrophone:(NSString *)name {
  using namespace std::literals;

  if (name == nil) {
    BOOST_LOG(warning) << "Microphone not found: (nil)"sv;
    return nil;
  }

  BOOST_LOG(debug) << "Searching for microphone: "sv << [name UTF8String];

  for (AVCaptureDevice *device in [AVAudio microphones]) {
    if ([[device localizedName] isEqualToString:name]) {
      BOOST_LOG(info) << "Found microphone: "sv << [name UTF8String];
      return device;
    }
  }

  BOOST_LOG(warning) << "Microphone not found: "sv << [name UTF8String];
  return nil;
}

- (int)setupMicrophone:(AVCaptureDevice *)device sampleRate:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels {
  using namespace std::literals;

  if (device == nil) {
    BOOST_LOG(error) << "Cannot setup microphone: device is nil"sv;
    return -1;
  }

  BOOST_LOG(info) << "Setting up microphone: "sv << [[device localizedName] UTF8String] << " with "sv << sampleRate << "Hz, "sv << frameSize << " frames, "sv << (int) channels << " channels"sv;

  self.audioCaptureSession = [[AVCaptureSession alloc] init];

  NSError *nsError;
  AVCaptureDeviceInput *audioInput = [AVCaptureDeviceInput deviceInputWithDevice:device error:&nsError];
  if (audioInput == nil) {
    BOOST_LOG(error) << "Failed to create audio input from device: "sv << (nsError ? [[nsError localizedDescription] UTF8String] : "unknown error"sv);
    return -1;
  }

  if ([self.audioCaptureSession canAddInput:audioInput]) {
    [self.audioCaptureSession addInput:audioInput];
    BOOST_LOG(debug) << "Successfully added audio input to capture session"sv;
  } else {
    BOOST_LOG(error) << "Cannot add audio input to capture session"sv;
    [audioInput dealloc];
    return -1;
  }

  AVCaptureAudioDataOutput *audioOutput = [[AVCaptureAudioDataOutput alloc] init];

  [audioOutput setAudioSettings:@{
    (NSString *) AVFormatIDKey: [NSNumber numberWithUnsignedInt:kAudioFormatLinearPCM],
    (NSString *) AVSampleRateKey: [NSNumber numberWithUnsignedInt:sampleRate],
    (NSString *) AVNumberOfChannelsKey: [NSNumber numberWithUnsignedInt:channels],
    (NSString *) AVLinearPCMBitDepthKey: [NSNumber numberWithUnsignedInt:32],
    (NSString *) AVLinearPCMIsFloatKey: @YES,
    (NSString *) AVLinearPCMIsNonInterleaved: @NO
  }];
  BOOST_LOG(debug) << "Configured audio output with settings: "sv << sampleRate << "Hz, "sv << (int) channels << " channels, 32-bit float"sv;

  dispatch_queue_attr_t qos = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_CONCURRENT, QOS_CLASS_USER_INITIATED, DISPATCH_QUEUE_PRIORITY_HIGH);
  dispatch_queue_t recordingQueue = dispatch_queue_create("audioSamplingQueue", qos);

  [audioOutput setSampleBufferDelegate:self queue:recordingQueue];

  if ([self.audioCaptureSession canAddOutput:audioOutput]) {
    [self.audioCaptureSession addOutput:audioOutput];
    BOOST_LOG(debug) << "Successfully added audio output to capture session"sv;
  } else {
    BOOST_LOG(error) << "Cannot add audio output to capture session"sv;
    [audioInput release];
    [audioOutput release];
    return -1;
  }

  self.audioConnection = [audioOutput connectionWithMediaType:AVMediaTypeAudio];

  [self.audioCaptureSession startRunning];
  BOOST_LOG(info) << "Audio capture session started successfully"sv;

  [audioInput release];
  [audioOutput release];

  // Initialize buffer and signal
  [self initializeAudioBuffer:channels];
  BOOST_LOG(debug) << "Audio buffer initialized for microphone capture"sv;

  return 0;
}

/**
 * @brief AVFoundation delegate method for processing microphone audio samples.
 * Called automatically when new audio samples are available from the microphone capture session.
 * Writes audio data directly to the shared circular buffer.
 * @param output The capture output that produced the sample buffer
 * @param sampleBuffer CMSampleBuffer containing the audio data
 * @param connection The capture connection that provided the sample buffer
 */
- (void)captureOutput:(AVCaptureOutput *)output
  didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
         fromConnection:(AVCaptureConnection *)connection {
  if (connection == self.audioConnection) {
    AudioBufferList audioBufferList;
    CMBlockBufferRef blockBuffer;

    CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sampleBuffer, NULL, &audioBufferList, sizeof(audioBufferList), NULL, NULL, 0, &blockBuffer);

    // NSAssert(audioBufferList.mNumberBuffers == 1, @"Expected interleaved PCM format but buffer contained %u streams", audioBufferList.mNumberBuffers);

    // this is safe, because an interleaved PCM stream has exactly one buffer,
    // and we don't want to do sanity checks in a performance critical exec path
    AudioBuffer audioBuffer = audioBufferList.mBuffers[0];

    TPCircularBufferProduceBytes(&self->audioSampleBuffer, audioBuffer.mData, audioBuffer.mDataByteSize);
    dispatch_semaphore_signal(self->audioSemaphore);
  }
}

- (int)setupSystemTap:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels {
  using namespace std::literals;
  BOOST_LOG(debug) << "setupSystemTap called with sampleRate:"sv << sampleRate << " frameSize:"sv << frameSize << " channels:"sv << (int) channels;

  // 1. Initialize system tap components
  if ([self initializeSystemTapContext:sampleRate frameSize:frameSize channels:channels] != 0) {
    return -1;
  }

  // 2. Create tap description and process tap
  CATapDescription *tapDescription = [self createSystemTapDescriptionForChannels:channels];
  if (!tapDescription) {
    [self cleanupSystemTapContext:nil];
    return -1;
  }

  // 3. Create and configure aggregate device
  OSStatus aggregateStatus = [self createAggregateDeviceWithTapDescription:tapDescription sampleRate:sampleRate frameSize:frameSize];
  if (aggregateStatus != noErr) {
    [self cleanupSystemTapContext:tapDescription];
    return -1;
  }

  // 4. Configure device properties and AudioConverter
  OSStatus configureStatus = [self configureDevicePropertiesAndConverter:sampleRate clientChannels:channels];
  if (configureStatus != noErr) {
    [self cleanupSystemTapContext:tapDescription];
    return -1;
  }

  // 5. Create and start IOProc
  OSStatus ioProcStatus = [self createAndStartAggregateDeviceIOProc:tapDescription];
  if (ioProcStatus != noErr) {
    [self cleanupSystemTapContext:tapDescription];
    return -1;
  }

  // 6. Initialize buffer and signal
  [self initializeAudioBuffer:channels];

  [tapDescription release];

  BOOST_LOG(info) << "System tap setup completed successfully!"sv;
  return 0;
}

/**
 * @brief Helper method to query Core Audio device properties.
 * Provides a centralized way to get device properties with error logging.
 * @param deviceID The audio device to query
 * @param selector The property selector to retrieve
 * @param scope The property scope (global, input, output)
 * @param element The property element identifier
 * @param ioDataSize Pointer to size variable (input: max size, output: actual size)
 * @param outData Buffer to store the property data
 * @return OSStatus indicating success (noErr) or error code
 */
- (OSStatus)getDeviceProperty:(AudioObjectID)deviceID
                     selector:(AudioObjectPropertySelector)selector
                        scope:(AudioObjectPropertyScope)scope
                      element:(AudioObjectPropertyElement)element
                         size:(UInt32 *)ioDataSize
                         data:(void *)outData {
  using namespace std::literals;

  AudioObjectPropertyAddress addr = {
    .mSelector = selector,
    .mScope = scope,
    .mElement = element
  };

  OSStatus result = AudioObjectGetPropertyData(deviceID, &addr, 0, NULL, ioDataSize, outData);

  if (result != noErr) {
    BOOST_LOG(warning) << "Failed to get device property (selector: "sv << selector << ", scope: "sv << scope << ", element: "sv << element << ") with status: "sv << result;
  }

  return result;
}

/**
 * @brief Generalized method for cleaning up system tap resources.
 * Safely cleans up Core Audio system tap components in reverse order of creation.
 * @param tapDescription Optional tap description object to release (can be nil)
 */
- (void)cleanupSystemTapContext:(id)tapDescription {
  using namespace std::literals;
  BOOST_LOG(debug) << "Starting system tap context cleanup"sv;

  // Clean up in reverse order of creation
  if (self->ioProcID && self->aggregateDeviceID != kAudioObjectUnknown) {
    AudioDeviceStop(self->aggregateDeviceID, self->ioProcID);
    AudioDeviceDestroyIOProcID(self->aggregateDeviceID, self->ioProcID);
    self->ioProcID = NULL;
    BOOST_LOG(debug) << "IOProc stopped and destroyed"sv;
  }

  if (self->aggregateDeviceID != kAudioObjectUnknown) {
    AudioHardwareDestroyAggregateDevice(self->aggregateDeviceID);
    self->aggregateDeviceID = kAudioObjectUnknown;
    BOOST_LOG(debug) << "Aggregate device destroyed"sv;
  }

  if (self->tapObjectID != kAudioObjectUnknown) {
    AudioHardwareDestroyProcessTap(self->tapObjectID);
    self->tapObjectID = kAudioObjectUnknown;
    BOOST_LOG(debug) << "Process tap destroyed"sv;
  }

  if (self->ioProcData) {
    if (self->ioProcData->conversionBuffer) {
      free(self->ioProcData->conversionBuffer);
      self->ioProcData->conversionBuffer = NULL;
      BOOST_LOG(debug) << "Conversion buffer freed"sv;
    }
    if (self->ioProcData->audioConverter) {
      AudioConverterDispose(self->ioProcData->audioConverter);
      self->ioProcData->audioConverter = NULL;
      BOOST_LOG(debug) << "AudioConverter disposed"sv;
    }
    free(self->ioProcData);
    self->ioProcData = NULL;
    BOOST_LOG(debug) << "IOProc data freed"sv;
  }

  if (tapDescription) {
    [tapDescription release];
    BOOST_LOG(debug) << "Tap description released"sv;
  }

  BOOST_LOG(debug) << "System tap context cleanup completed"sv;
}

// MARK: - Buffer Management Methods
// Shared buffer management methods used by both audio capture paths

- (void)initializeAudioBuffer:(UInt8)channels {
  using namespace std::literals;
  BOOST_LOG(debug) << "Initializing audio buffer for "sv << (int) channels << " channels"sv;

  // Cleanup any existing circular buffer first
  TPCircularBufferCleanup(&self->audioSampleBuffer);

  // Initialize the circular buffer with proper size for the channel count
  TPCircularBufferInit(&self->audioSampleBuffer, kBufferLength * channels);

  // Initialize real-time safe semaphore for synchronization (cleanup any existing one first)
  if (self->audioSemaphore) {
    dispatch_release(self->audioSemaphore);
  }
  self->audioSemaphore = dispatch_semaphore_create(0);

  BOOST_LOG(info) << "Audio buffer initialized successfully with size: "sv << (kBufferLength * channels) << " bytes"sv;
}

- (void)cleanupAudioBuffer {
  using namespace std::literals;
  BOOST_LOG(debug) << "Cleaning up audio buffer"sv;

  // Signal any waiting threads before cleanup and release semaphore
  if (self->audioSemaphore) {
    dispatch_semaphore_signal(self->audioSemaphore);  // Wake up any waiting threads
    dispatch_release(self->audioSemaphore);
    self->audioSemaphore = NULL;
  }

  // Cleanup the circular buffer
  TPCircularBufferCleanup(&self->audioSampleBuffer);

  BOOST_LOG(info) << "Audio buffer cleanup completed"sv;
}

/**
 * @brief Destructor for AVAudio instances.
 * Performs comprehensive cleanup of both audio capture paths and shared resources.
 */
- (void)dealloc {
  using namespace std::literals;
  BOOST_LOG(debug) << "AVAudio dealloc started"sv;

  // Cleanup system tap resources using the generalized method
  [self cleanupSystemTapContext:nil];

  // Cleanup microphone session (AVFoundation path)
  if (self.audioCaptureSession) {
    [self.audioCaptureSession stopRunning];
    self.audioCaptureSession = nil;
    BOOST_LOG(debug) << "Audio capture session stopped and released"sv;
  }
  self.audioConnection = nil;

  // Use our centralized buffer cleanup method (handles signal and buffer cleanup)
  [self cleanupAudioBuffer];

  BOOST_LOG(debug) << "AVAudio dealloc completed"sv;
  [super dealloc];
}

// MARK: - System Tap Initialization
// Private methods for initializing Core Audio system tap components

- (int)initializeSystemTapContext:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels {
  using namespace std::literals;

  // Check macOS version requirement
  if (![[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:((NSOperatingSystemVersion) {14, 2, 0})]) {
    BOOST_LOG(error) << "macOS version requirement not met (need 14.2+)"sv;
    return -1;
  }

  NSOperatingSystemVersion version = [[NSProcessInfo processInfo] operatingSystemVersion];
  BOOST_LOG(debug) << "macOS version check passed (running "sv << version.majorVersion << "."sv << version.minorVersion << "."sv << version.patchVersion << ")"sv;

  // Initialize Core Audio objects
  self->tapObjectID = kAudioObjectUnknown;
  self->aggregateDeviceID = kAudioObjectUnknown;
  self->ioProcID = NULL;

  // Create IOProc data structure with client requirements
  self->ioProcData = (AVAudioIOProcData *) malloc(sizeof(AVAudioIOProcData));
  if (!self->ioProcData) {
    BOOST_LOG(error) << "Failed to allocate IOProc data structure"sv;
    return -1;
  }

  self->ioProcData->avAudio = self;
  self->ioProcData->clientRequestedChannels = channels;
  self->ioProcData->clientRequestedFrameSize = frameSize;
  self->ioProcData->clientRequestedSampleRate = sampleRate;
  self->ioProcData->audioConverter = NULL;
  self->ioProcData->conversionBuffer = NULL;
  self->ioProcData->conversionBufferSize = 0;

  BOOST_LOG(debug) << "System tap initialization completed"sv;
  return 0;
}

- (CATapDescription *)createSystemTapDescriptionForChannels:(UInt8)channels {
  using namespace std::literals;

  BOOST_LOG(debug) << "Creating tap description for "sv << (int) channels << " channels"sv;
  CATapDescription *tapDescription;
  NSArray *excludeProcesses = @[];

  if (channels == 1) {
    tapDescription = [[CATapDescription alloc] initMonoGlobalTapButExcludeProcesses:excludeProcesses];
  } else {
    tapDescription = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:excludeProcesses];
  }

  // Set unique name and UUID for this instance
  NSString *uniqueName = [NSString stringWithFormat:@"SunshineAVAudio-Tap-%p", (void *) self];
  NSUUID *uniqueUUID = [[NSUUID alloc] init];

  tapDescription.name = uniqueName;
  tapDescription.UUID = uniqueUUID;
  [tapDescription setPrivate:YES];

  // Create the tap
  BOOST_LOG(debug) << "Creating process tap with name: "sv << [uniqueName UTF8String];

  // Use direct API call like the reference implementation
  OSStatus status = AudioHardwareCreateProcessTap((CATapDescription *) tapDescription, &self->tapObjectID);
  BOOST_LOG(debug) << "AudioHardwareCreateProcessTap returned status: "sv << status;

  [uniqueUUID release];

  if (status != noErr) {
    BOOST_LOG(error) << "AudioHardwareCreateProcessTap failed with status: "sv << status << " (tapDescription: "sv << [[tapDescription description] UTF8String] << ")"sv;
    [tapDescription release];
    return nil;
  }

  BOOST_LOG(debug) << "Process tap created successfully with ID: "sv << self->tapObjectID;
  return tapDescription;
}

- (OSStatus)createAggregateDeviceWithTapDescription:(CATapDescription *)tapDescription sampleRate:(UInt32)sampleRate frameSize:(UInt32)frameSize {
  using namespace std::literals;

  // Get Tap UUID string properly
  NSString *tapUIDString = nil;
  if ([tapDescription respondsToSelector:@selector(UUID)]) {
    tapUIDString = [[tapDescription UUID] UUIDString];
  }
  if (!tapUIDString) {
    BOOST_LOG(error) << "Failed to get tap UUID from description"sv;
    return kAudioHardwareUnspecifiedError;
  }

  // Create aggregate device with better drift compensation and proper keys
  NSDictionary *subTapDictionary = @{
    @kAudioSubTapUIDKey: tapUIDString,
    @kAudioSubTapDriftCompensationKey: @YES,
  };

  NSDictionary *aggregateProperties = @{
    @kAudioAggregateDeviceNameKey: [NSString stringWithFormat:@"SunshineAggregate-%p", (void *) self],
    @kAudioAggregateDeviceUIDKey: [NSString stringWithFormat:@"com.lizardbyte.sunshine.aggregate-%p", (void *) self],
    @kAudioAggregateDeviceTapListKey: @[subTapDictionary],
    @kAudioAggregateDeviceTapAutoStartKey: @NO,
    @kAudioAggregateDeviceIsPrivateKey: @YES,
    // Add clock domain configuration for better timing
    @kAudioAggregateDeviceIsStackedKey: @NO,
  };

  BOOST_LOG(debug) << "Creating aggregate device with tap UID: "sv << [tapUIDString UTF8String];
  OSStatus status = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef) aggregateProperties, &self->aggregateDeviceID);
  BOOST_LOG(debug) << "AudioHardwareCreateAggregateDevice returned status: "sv << status;
  if (status != noErr && status != 'ExtA') {
    BOOST_LOG(error) << "Failed to create aggregate device with status: "sv << status;
    return status;
  }

  BOOST_LOG(info) << "Aggregate device created with ID: "sv << self->aggregateDeviceID;

  // Configure the aggregate device
  if (self->aggregateDeviceID != kAudioObjectUnknown) {
    // Set sample rate on the aggregate device
    AudioObjectPropertyAddress sampleRateAddr = {
      .mSelector = kAudioDevicePropertyNominalSampleRate,
      .mScope = kAudioObjectPropertyScopeGlobal,
      .mElement = kAudioObjectPropertyElementMain
    };
    Float64 deviceSampleRate = (Float64) sampleRate;
    UInt32 sampleRateSize = sizeof(Float64);
    OSStatus sampleRateResult = AudioObjectSetPropertyData(self->aggregateDeviceID, &sampleRateAddr, 0, NULL, sampleRateSize, &deviceSampleRate);
    if (sampleRateResult != noErr) {
      BOOST_LOG(warning) << "Failed to set aggregate device sample rate: "sv << sampleRateResult;
    } else {
      BOOST_LOG(debug) << "Set aggregate device sample rate to "sv << sampleRate << "Hz"sv;
    }

    // Set buffer size on the aggregate device
    AudioObjectPropertyAddress bufferSizeAddr = {
      .mSelector = kAudioDevicePropertyBufferFrameSize,
      .mScope = kAudioObjectPropertyScopeGlobal,
      .mElement = kAudioObjectPropertyElementMain
    };
    UInt32 deviceFrameSize = frameSize;
    UInt32 frameSizeSize = sizeof(UInt32);
    OSStatus bufferSizeResult = AudioObjectSetPropertyData(self->aggregateDeviceID, &bufferSizeAddr, 0, NULL, frameSizeSize, &deviceFrameSize);
    if (bufferSizeResult != noErr) {
      BOOST_LOG(warning) << "Failed to set aggregate device buffer size: "sv << bufferSizeResult;
    } else {
      BOOST_LOG(debug) << "Set aggregate device buffer size to "sv << frameSize << " frames"sv;
    }
  }

  BOOST_LOG(info) << "Aggregate device created and configured successfully"sv;
  return noErr;
}

- (OSStatus)configureDevicePropertiesAndConverter:(UInt32)clientSampleRate
                                   clientChannels:(UInt8)clientChannels {
  using namespace std::literals;

  // Query actual device properties to determine if conversion is needed
  Float64 aggregateDeviceSampleRate = 48000.0;  // Default fallback
  UInt32 aggregateDeviceChannels = 2;  // Default fallback

  // Get actual sample rate from the aggregate device
  UInt32 sampleRateQuerySize = sizeof(Float64);
  OSStatus sampleRateStatus = [self getDeviceProperty:self->aggregateDeviceID
                                             selector:kAudioDevicePropertyNominalSampleRate
                                                scope:kAudioObjectPropertyScopeGlobal
                                              element:kAudioObjectPropertyElementMain
                                                 size:&sampleRateQuerySize
                                                 data:&aggregateDeviceSampleRate];

  if (sampleRateStatus != noErr) {
    BOOST_LOG(warning) << "Failed to get device sample rate, using default 48kHz: "sv << sampleRateStatus;
    aggregateDeviceSampleRate = 48000.0;
  }

  // Get actual channel count from the device's input stream configuration
  AudioObjectPropertyAddress streamConfigAddr = {
    .mSelector = kAudioDevicePropertyStreamConfiguration,
    .mScope = kAudioDevicePropertyScopeInput,
    .mElement = kAudioObjectPropertyElementMain
  };

  UInt32 streamConfigSize = 0;
  OSStatus streamConfigSizeStatus = AudioObjectGetPropertyDataSize(self->aggregateDeviceID, &streamConfigAddr, 0, NULL, &streamConfigSize);

  if (streamConfigSizeStatus == noErr && streamConfigSize > 0) {
    AudioBufferList *streamConfig = (AudioBufferList *) malloc(streamConfigSize);
    if (streamConfig) {
      OSStatus streamConfigStatus = AudioObjectGetPropertyData(self->aggregateDeviceID, &streamConfigAddr, 0, NULL, &streamConfigSize, streamConfig);
      if (streamConfigStatus == noErr && streamConfig->mNumberBuffers > 0) {
        aggregateDeviceChannels = streamConfig->mBuffers[0].mNumberChannels;
        BOOST_LOG(debug) << "Device reports "sv << aggregateDeviceChannels << " input channels"sv;
      } else {
        BOOST_LOG(warning) << "Failed to get stream configuration, using default 2 channels: "sv << streamConfigStatus;
      }
      free(streamConfig);
    }
  } else {
    BOOST_LOG(warning) << "Failed to get stream configuration size, using default 2 channels: "sv << streamConfigSizeStatus;
  }

  BOOST_LOG(debug) << "Device properties - Sample Rate: "sv << aggregateDeviceSampleRate << "Hz, Channels: "sv << aggregateDeviceChannels;

  // Create AudioConverter based on actual device properties vs client requirements
  BOOL needsConversion = ((UInt32) aggregateDeviceSampleRate != clientSampleRate) || (aggregateDeviceChannels != clientChannels);
  BOOST_LOG(debug) << "needsConversion: "sv << (needsConversion ? "YES" : "NO")
                   << " (device: "sv << aggregateDeviceSampleRate << "Hz/" << aggregateDeviceChannels << "ch"
                   << " -> client: "sv << clientSampleRate << "Hz/" << (int) clientChannels << "ch)"sv;

  if (needsConversion) {
    AudioStreamBasicDescription sourceFormat = {0};
    sourceFormat.mSampleRate = aggregateDeviceSampleRate;
    sourceFormat.mFormatID = kAudioFormatLinearPCM;
    sourceFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    sourceFormat.mBytesPerPacket = sizeof(float) * aggregateDeviceChannels;
    sourceFormat.mFramesPerPacket = 1;
    sourceFormat.mBytesPerFrame = sizeof(float) * aggregateDeviceChannels;
    sourceFormat.mChannelsPerFrame = aggregateDeviceChannels;
    sourceFormat.mBitsPerChannel = 32;

    AudioStreamBasicDescription targetFormat = {0};
    targetFormat.mSampleRate = clientSampleRate;
    targetFormat.mFormatID = kAudioFormatLinearPCM;
    targetFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    targetFormat.mBytesPerPacket = sizeof(float) * clientChannels;
    targetFormat.mFramesPerPacket = 1;
    targetFormat.mBytesPerFrame = sizeof(float) * clientChannels;
    targetFormat.mChannelsPerFrame = clientChannels;
    targetFormat.mBitsPerChannel = 32;

    OSStatus converterStatus = AudioConverterNew(&sourceFormat, &targetFormat, &self->ioProcData->audioConverter);
    if (converterStatus != noErr) {
      BOOST_LOG(error) << "Failed to create audio converter: "sv << converterStatus;
      return converterStatus;
    }
    BOOST_LOG(info) << "AudioConverter created successfully for "sv << aggregateDeviceSampleRate << "Hz/" << aggregateDeviceChannels << "ch -> " << clientSampleRate << "Hz/" << (int) clientChannels << "ch"sv;
  } else {
    BOOST_LOG(info) << "No conversion needed - formats match (device: "sv << aggregateDeviceSampleRate << "Hz/" << aggregateDeviceChannels << "ch)"sv;
  }

  // Pre-allocate conversion buffer for real-time use (eliminates malloc in audio callback)
  UInt32 maxFrames = self->ioProcData->clientRequestedFrameSize * 8; // Generous buffer for upsampling scenarios
  self->ioProcData->conversionBufferSize = maxFrames * clientChannels * sizeof(float);
  self->ioProcData->conversionBuffer = (float *)malloc(self->ioProcData->conversionBufferSize);
  
  if (!self->ioProcData->conversionBuffer) {
    BOOST_LOG(error) << "Failed to allocate conversion buffer"sv;
    if (self->ioProcData->audioConverter) {
      AudioConverterDispose(self->ioProcData->audioConverter);
      self->ioProcData->audioConverter = NULL;
    }
    return kAudioHardwareUnspecifiedError;
  }
  
  BOOST_LOG(debug) << "Pre-allocated conversion buffer: "sv << self->ioProcData->conversionBufferSize << " bytes ("sv << maxFrames << " frames)"sv;

  // Store the actual device format for use in the IOProc
  self->ioProcData->aggregateDeviceSampleRate = (UInt32) aggregateDeviceSampleRate;
  self->ioProcData->aggregateDeviceChannels = aggregateDeviceChannels;

  BOOST_LOG(info) << "Device properties and converter configuration completed"sv;
  return noErr;
}

- (OSStatus)createAndStartAggregateDeviceIOProc:(CATapDescription *)tapDescription {
  using namespace std::literals;

  // Create IOProc
  BOOST_LOG(debug) << "Creating IOProc for aggregate device ID: "sv << self->aggregateDeviceID;
  OSStatus status = AudioDeviceCreateIOProcID(self->aggregateDeviceID, platf::systemAudioIOProc, self->ioProcData, &self->ioProcID);
  BOOST_LOG(debug) << "AudioDeviceCreateIOProcID returned status: "sv << status;
  if (status != noErr) {
    BOOST_LOG(error) << "Failed to create IOProc with status: "sv << status;
    return status;
  }

  // Start the IOProc
  BOOST_LOG(debug) << "Starting IOProc for aggregate device";
  status = AudioDeviceStart(self->aggregateDeviceID, self->ioProcID);
  BOOST_LOG(debug) << "AudioDeviceStart returned status: "sv << status;
  if (status != noErr) {
    BOOST_LOG(error) << "Failed to start IOProc with status: "sv << status;
    AudioDeviceDestroyIOProcID(self->aggregateDeviceID, self->ioProcID);
    return status;
  }

  BOOST_LOG(info) << "System tap IO proc created and started successfully"sv;
  return noErr;
}

@end
