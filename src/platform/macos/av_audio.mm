/**
 * @file src/platform/macos/av_audio.mm
 * @brief Simplified audio capture on macOS with system tap.
 */
#import "av_audio.h"
#include "src/logging.h"

#import <AudioToolbox/AudioConverter.h>
#import <CoreAudio/CATapDescription.h>

// C wrapper for AudioConverter input callback
static OSStatus audioConverterComplexInputProcWrapper(AudioConverterRef inAudioConverter, UInt32 *ioNumberDataPackets, AudioBufferList *ioData, AudioStreamPacketDescription **outDataPacketDescription, void *inUserData) {
  struct AudioConverterInputData *inputInfo = (struct AudioConverterInputData *) inUserData;
  AVAudio *avAudio = inputInfo->avAudio;
  
  return [avAudio audioConverterComplexInputProc:inAudioConverter
                             ioNumberDataPackets:ioNumberDataPackets
                                          ioData:ioData
                          outDataPacketDescription:outDataPacketDescription
                                       inputInfo:inputInfo];
}

// C wrapper for IOProc callback
static OSStatus systemAudioIOProcWrapper(AudioObjectID inDevice, const AudioTimeStamp *inNow, const AudioBufferList *inInputData, const AudioTimeStamp *inInputTime, AudioBufferList *outOutputData, const AudioTimeStamp *inOutputTime, void *inClientData) {
  AVAudioIOProcData *procData = (AVAudioIOProcData *) inClientData;
  AVAudio *avAudio = procData->avAudio;
  return [avAudio systemAudioIOProc:inDevice
                                     inNow:inNow
                               inInputData:inInputData
                               inInputTime:inInputTime
                             outOutputData:outOutputData
                              inOutputTime:inOutputTime
                            clientChannels:procData->clientRequestedChannels
                           clientFrameSize:procData->clientRequestedFrameSize
                          clientSampleRate:procData->clientRequestedSampleRate];
}

@implementation AVAudio

+ (NSArray<AVCaptureDevice *> *)microphones {
  if ([[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:((NSOperatingSystemVersion) {10, 15, 0})]) {
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
    return discoverySession.devices;
#pragma clang diagnostic pop
  } else {
    // We're intentionally using a deprecated API here specifically for versions
    // of macOS where it's not deprecated, so we can ignore any deprecation
    // warnings:
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return [AVCaptureDevice devicesWithMediaType:AVMediaTypeAudio];
#pragma clang diagnostic pop
  }
}

+ (NSArray<NSString *> *)microphoneNames {
  NSMutableArray *result = [[NSMutableArray alloc] init];

  for (AVCaptureDevice *device in [AVAudio microphones]) {
    [result addObject:[device localizedName]];
  }

  return result;
}

+ (AVCaptureDevice *)findMicrophone:(NSString *)name {
  for (AVCaptureDevice *device in [AVAudio microphones]) {
    if ([[device localizedName] isEqualToString:name]) {
      return device;
    }
  }

  return nil;
}

- (int)setupMicrophone:(AVCaptureDevice *)device sampleRate:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels {
  self.audioCaptureSession = [[AVCaptureSession alloc] init];

  NSError *error;
  AVCaptureDeviceInput *audioInput = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
  if (audioInput == nil) {
    return -1;
  }

  if ([self.audioCaptureSession canAddInput:audioInput]) {
    [self.audioCaptureSession addInput:audioInput];
  } else {
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

  dispatch_queue_attr_t qos = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_CONCURRENT, QOS_CLASS_USER_INITIATED, DISPATCH_QUEUE_PRIORITY_HIGH);
  dispatch_queue_t recordingQueue = dispatch_queue_create("audioSamplingQueue", qos);

  [audioOutput setSampleBufferDelegate:self queue:recordingQueue];

  if ([self.audioCaptureSession canAddOutput:audioOutput]) {
    [self.audioCaptureSession addOutput:audioOutput];
  } else {
    [audioInput release];
    [audioOutput release];
    return -1;
  }

  self.audioConnection = [audioOutput connectionWithMediaType:AVMediaTypeAudio];

  [self.audioCaptureSession startRunning];

  [audioInput release];
  [audioOutput release];

  // Initialize buffer and signal
  [self initializeAudioBuffer:channels];

  return 0;
}

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
    [self.samplesArrivedSignal signal];
  }
}

- (int)setupSystemTap:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels {
  using namespace std::literals;
  BOOST_LOG(info) << "setupSystemTap called with sampleRate:"sv << sampleRate << " frameSize:"sv << frameSize << " channels:"sv << (int) channels;

  // Initialize system tap components
  if ([self initSystemTapContext:sampleRate frameSize:frameSize channels:channels] != 0) {
    return -1;
  }

  // 1. Create tap description and process tap
  CATapDescription *tapDescription = [self createSystemTapDescriptionForChannels:channels];
  if (!tapDescription) {
    [self cleanupSystemTapContext:nil];
    return -1;
  }

  // 2. Create and configure aggregate device
  if ([self createAggregateDeviceWithTapDescription:tapDescription sampleRate:sampleRate frameSize:frameSize] != 0) {
    [self cleanupSystemTapContext:tapDescription];
    return -1;
  }

  // 3. Configure device properties and AudioConverter
  OSStatus configureStatus = [self configureDevicePropertiesAndConverter:sampleRate clientChannels:channels];
  if (configureStatus != noErr) {
    [self cleanupSystemTapContext:tapDescription];
    return -1;
  }

  // 4. Create and start IOProc
  OSStatus ioProcStatus = [self createAndStartIOProc:tapDescription];
  if (ioProcStatus != noErr) {
    [self cleanupSystemTapContext:tapDescription];
    return -1;
  }

  // Initialize buffer and signal
  [self initializeAudioBuffer:channels];

  [tapDescription release];

  BOOST_LOG(info) << "System tap setup completed successfully!";
  return 0;
}

- (OSStatus)systemAudioIOProc:(AudioObjectID)inDevice
                               inNow:(const AudioTimeStamp *)inNow
                         inInputData:(const AudioBufferList *)inInputData
                         inInputTime:(const AudioTimeStamp *)inInputTime
                       outOutputData:(AudioBufferList *)outOutputData
                        inOutputTime:(const AudioTimeStamp *)inOutputTime
                      clientChannels:(UInt32)clientChannels
                     clientFrameSize:(UInt32)clientFrameSize
                    clientSampleRate:(UInt32)clientSampleRate {
  // Always ensure we write to buffer and signal, even if input is empty/invalid
  BOOL didWriteData = NO;

  if (inInputData && inInputData->mNumberBuffers > 0) {
    AudioBuffer inputBuffer = inInputData->mBuffers[0];

    if (inputBuffer.mData && inputBuffer.mDataByteSize > 0) {
      float *inputSamples = (float *) inputBuffer.mData;
      UInt32 deviceChannels = self->ioProcData ? self->ioProcData->aggregateDeviceChannels : 2;
      UInt32 inputFrames = inputBuffer.mDataByteSize / (deviceChannels * sizeof(float));

      // Use AudioConverter if we need any conversion, otherwise pass through
      if (self->ioProcData && self->ioProcData->audioConverter) {
        // Let AudioConverter determine optimal output size - it knows best!
        // We'll provide a generous buffer and let it tell us what it actually used
        UInt32 maxOutputFrames = inputFrames * 4;  // Very generous for any upsampling scenario
        UInt32 outputBytes = maxOutputFrames * clientChannels * sizeof(float);
        float *outputBuffer = (float *) malloc(outputBytes);

        if (outputBuffer) {
          struct AudioConverterInputData inputData = {
            .inputData = inputSamples,
            .inputFrames = inputFrames,
            .framesProvided = 0,
            .deviceChannels = deviceChannels,
            .avAudio = self
          };

          AudioBufferList outputBufferList = {0};
          outputBufferList.mNumberBuffers = 1;
          outputBufferList.mBuffers[0].mNumberChannels = clientChannels;
          outputBufferList.mBuffers[0].mDataByteSize = outputBytes;
          outputBufferList.mBuffers[0].mData = outputBuffer;

          UInt32 outputFrameCount = maxOutputFrames;
          OSStatus converterStatus = AudioConverterFillComplexBuffer(
            self->ioProcData->audioConverter,
            audioConverterComplexInputProcWrapper,
            &inputData,
            &outputFrameCount,
            &outputBufferList,
            NULL
          );

          if (converterStatus == noErr && outputFrameCount > 0) {
            // AudioConverter did all the work: sample rate + channels + optimal frame count
            UInt32 actualOutputBytes = outputFrameCount * clientChannels * sizeof(float);
            TPCircularBufferProduceBytes(&self->audioSampleBuffer, outputBuffer, actualOutputBytes);
            didWriteData = YES;
          } else {
            // Fallback: write original data
            TPCircularBufferProduceBytes(&self->audioSampleBuffer, inputBuffer.mData, inputBuffer.mDataByteSize);
            didWriteData = YES;
          }

          free(outputBuffer);
        } else {
          // Memory allocation failed, fallback to original data
          TPCircularBufferProduceBytes(&self->audioSampleBuffer, inputBuffer.mData, inputBuffer.mDataByteSize);
          didWriteData = YES;
        }
      } else {
        // No conversion needed - direct passthrough
        TPCircularBufferProduceBytes(&self->audioSampleBuffer, inputBuffer.mData, inputBuffer.mDataByteSize);
        didWriteData = YES;
      }
    }
  }

  // Always signal, even if we didn't write data (ensures consumer doesn't block)
  if (!didWriteData) {
    // Write silence if no valid input data
    UInt32 silenceFrames = clientFrameSize > 0 ? clientFrameSize : 2048;
    UInt32 silenceBytes = silenceFrames * clientChannels * sizeof(float);

    float *silenceBuffer = (float *) calloc(silenceFrames * clientChannels, sizeof(float));
    if (silenceBuffer) {
      TPCircularBufferProduceBytes(&self->audioSampleBuffer, silenceBuffer, silenceBytes);
      free(silenceBuffer);
    }
  }

  [self.samplesArrivedSignal signal];

  return noErr;
}

// AudioConverter input callback as Objective-C method
- (OSStatus)audioConverterComplexInputProc:(AudioConverterRef)inAudioConverter
                        ioNumberDataPackets:(UInt32 *)ioNumberDataPackets
                                     ioData:(AudioBufferList *)ioData
                     outDataPacketDescription:(AudioStreamPacketDescription **)outDataPacketDescription
                                   inputInfo:(struct AudioConverterInputData *)inputInfo {
  if (inputInfo->framesProvided >= inputInfo->inputFrames) {
    *ioNumberDataPackets = 0;
    return noErr;
  }

  UInt32 framesToProvide = MIN(*ioNumberDataPackets, inputInfo->inputFrames - inputInfo->framesProvided);

  ioData->mNumberBuffers = 1;
  ioData->mBuffers[0].mNumberChannels = inputInfo->deviceChannels;
  ioData->mBuffers[0].mDataByteSize = framesToProvide * inputInfo->deviceChannels * sizeof(float);
  ioData->mBuffers[0].mData = inputInfo->inputData + (inputInfo->framesProvided * inputInfo->deviceChannels);

  inputInfo->framesProvided += framesToProvide;
  *ioNumberDataPackets = framesToProvide;

  return noErr;
}

// Helper method to get device properties
- (OSStatus)getDeviceProperty:(AudioObjectID)deviceID 
                     selector:(AudioObjectPropertySelector)selector 
                        scope:(AudioObjectPropertyScope)scope 
                      element:(AudioObjectPropertyElement)element 
                         size:(UInt32 *)ioDataSize 
                         data:(void *)outData {
  AudioObjectPropertyAddress addr = {
    .mSelector = selector,
    .mScope = scope,
    .mElement = element
  };
  return AudioObjectGetPropertyData(deviceID, &addr, 0, NULL, ioDataSize, outData);
}

// Generalized method for cleaning up system tap resources
- (void)cleanupSystemTapContext:(id)tapDescription {
  // Clean up in reverse order of creation
  if (self->ioProcID && self->aggregateDeviceID != kAudioObjectUnknown) {
    AudioDeviceStop(self->aggregateDeviceID, self->ioProcID);
    AudioDeviceDestroyIOProcID(self->aggregateDeviceID, self->ioProcID);
    self->ioProcID = NULL;
  }

  if (self->aggregateDeviceID != kAudioObjectUnknown) {
    AudioHardwareDestroyAggregateDevice(self->aggregateDeviceID);
    self->aggregateDeviceID = kAudioObjectUnknown;
  }

  if (self->tapObjectID != kAudioObjectUnknown) {
    AudioHardwareDestroyProcessTap(self->tapObjectID);
    self->tapObjectID = kAudioObjectUnknown;
  }

  if (self->ioProcData) {
    if (self->ioProcData->audioConverter) {
      AudioConverterDispose(self->ioProcData->audioConverter);
      self->ioProcData->audioConverter = NULL;
    }
    free(self->ioProcData);
    self->ioProcData = NULL;
  }

  if (tapDescription) {
    [tapDescription release];
  }
}

#pragma mark - Buffer Management Methods

- (void)initializeAudioBuffer:(UInt8)channels {
  // Initialize the circular buffer with proper size for the channel count
  TPCircularBufferInit(&self->audioSampleBuffer, kBufferLength * channels);
  
  // Initialize the condition signal for synchronization
  if (!self.samplesArrivedSignal) {
    self.samplesArrivedSignal = [[NSCondition alloc] init];
  }
}

- (void)cleanupAudioBuffer {
  // Signal any waiting threads before cleanup
  if (self.samplesArrivedSignal) {
    [self.samplesArrivedSignal signal];
    [self.samplesArrivedSignal release];
    self.samplesArrivedSignal = nil;
  }
  
  // Cleanup the circular buffer
  TPCircularBufferCleanup(&self->audioSampleBuffer);
}

- (void)dealloc {
  // Cleanup system tap resources using the generalized method
  [self cleanupSystemTapContext:nil];

  // Cleanup microphone session (AVFoundation path)
  if (self.audioCaptureSession) {
    [self.audioCaptureSession stopRunning];
    self.audioCaptureSession = nil;
  }
  self.audioConnection = nil;

  // Use our centralized buffer cleanup method (handles signal and buffer cleanup)
  [self cleanupAudioBuffer];

  [super dealloc];
}

#pragma mark - System Tap Initialization

- (int)initSystemTapContext:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels {
  using namespace std::literals;
  
  // Check macOS version requirement
  if (![[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:((NSOperatingSystemVersion) {14, 2, 0})]) {
    BOOST_LOG(error) << "macOS version requirement not met (need 14.2+)"sv;
    return -1;
  }

  NSOperatingSystemVersion version = [[NSProcessInfo processInfo] operatingSystemVersion];
  BOOST_LOG(info) << "macOS version check passed (running "sv << version.majorVersion << "."sv << version.minorVersion << "."sv << version.patchVersion << ")"sv;

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
  
  BOOST_LOG(info) << "System tap initialization completed"sv;
  return 0;
}

- (CATapDescription *)createSystemTapDescriptionForChannels:(UInt8)channels {
  using namespace std::literals;
  
  BOOST_LOG(info) << "Creating tap description for "sv << (int) channels << " channels"sv;
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
  BOOST_LOG(info) << "Creating process tap with name: "sv << [uniqueName UTF8String];

  // Use direct API call like the reference implementation
  OSStatus status = AudioHardwareCreateProcessTap((CATapDescription *) tapDescription, &self->tapObjectID);
  BOOST_LOG(info) << "AudioHardwareCreateProcessTap returned status: "sv << status;

  [uniqueUUID release];

  if (status != noErr) {
    BOOST_LOG(error) << "AudioHardwareCreateProcessTap failed with status: "sv << status << " (tapDescription: "sv << [tapDescription description] << ")"sv;
    [tapDescription release];
    return nil;
  }

  return tapDescription;
}

- (int)createAggregateDeviceWithTapDescription:(CATapDescription *)tapDescription sampleRate:(UInt32)sampleRate frameSize:(UInt32)frameSize {
  using namespace std::literals;
  
  // Get Tap UUID string properly
  NSString *tapUIDString = nil;
  if ([tapDescription respondsToSelector:@selector(UUID)]) {
    tapUIDString = [[tapDescription UUID] UUIDString];
  }
  if (!tapUIDString) {
    BOOST_LOG(error) << "Failed to get tap UUID from description"sv;
    return -1;
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

  BOOST_LOG(info) << "Creating aggregate device with tap UID: "sv << [tapUIDString UTF8String];
  OSStatus status = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef) aggregateProperties, &self->aggregateDeviceID);
  BOOST_LOG(info) << "AudioHardwareCreateAggregateDevice returned status: "sv << status;
  if (status != noErr && status != 'ExtA') {
    BOOST_LOG(error) << "Failed to create aggregate device with status: "sv << status;
    return -1;
  }

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
    }
  }
  
  BOOST_LOG(info) << "Aggregate device created and configured successfully"sv;
  return 0;
}

- (OSStatus)configureDevicePropertiesAndConverter:(UInt32)clientSampleRate 
                                      clientChannels:(UInt8)clientChannels {
  using namespace std::literals;
  
  // Query actual device properties to determine if conversion is needed
  Float64 aggregateDeviceSampleRate = 48000.0; // Default fallback
  UInt32 aggregateDeviceChannels = 2; // Default fallback
  
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
    AudioBufferList *streamConfig = (AudioBufferList *)malloc(streamConfigSize);
    if (streamConfig) {
      OSStatus streamConfigStatus = AudioObjectGetPropertyData(self->aggregateDeviceID, &streamConfigAddr, 0, NULL, &streamConfigSize, streamConfig);
      if (streamConfigStatus == noErr && streamConfig->mNumberBuffers > 0) {
        aggregateDeviceChannels = streamConfig->mBuffers[0].mNumberChannels;
        BOOST_LOG(info) << "Device reports "sv << aggregateDeviceChannels << " input channels"sv;
      } else {
        BOOST_LOG(warning) << "Failed to get stream configuration, using default 2 channels: "sv << streamConfigStatus;
      }
      free(streamConfig);
    }
  } else {
    BOOST_LOG(warning) << "Failed to get stream configuration size, using default 2 channels: "sv << streamConfigSizeStatus;
  }

  BOOST_LOG(info) << "Device properties - Sample Rate: "sv << aggregateDeviceSampleRate << "Hz, Channels: "sv << aggregateDeviceChannels;

  // Create AudioConverter based on actual device properties vs client requirements
  BOOL needsConversion = ((UInt32)aggregateDeviceSampleRate != clientSampleRate) || (aggregateDeviceChannels != clientChannels);
  BOOST_LOG(info) << "needsConversion: "sv << (needsConversion ? "YES" : "NO") 
                  << " (device: "sv << aggregateDeviceSampleRate << "Hz/" << aggregateDeviceChannels << "ch"
                  << " -> client: "sv << clientSampleRate << "Hz/" << (int)clientChannels << "ch)"sv;
  
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
    BOOST_LOG(info) << "AudioConverter created successfully for "sv << aggregateDeviceSampleRate << "Hz/" << aggregateDeviceChannels << "ch -> " << clientSampleRate << "Hz/" << (int)clientChannels << "ch"sv;
  }
  
  // Store the actual device format for use in the IOProc
  self->ioProcData->aggregateDeviceSampleRate = (UInt32)aggregateDeviceSampleRate;
  self->ioProcData->aggregateDeviceChannels = aggregateDeviceChannels;
  
  return noErr;
}

- (OSStatus)createAndStartIOProc:(CATapDescription *)tapDescription {
  using namespace std::literals;
  
  // Create IOProc
  BOOST_LOG(info) << "Creating IOProc for aggregate device ID: "sv << self->aggregateDeviceID;
  OSStatus status = AudioDeviceCreateIOProcID(self->aggregateDeviceID, systemAudioIOProcWrapper, self->ioProcData, &self->ioProcID);
  BOOST_LOG(info) << "AudioDeviceCreateIOProcID returned status: "sv << status;
  if (status != noErr) {
    BOOST_LOG(error) << "Failed to create IOProc with status: "sv << status;
    return status;
  }

  // Start the IOProc
  BOOST_LOG(info) << "Starting IOProc for aggregate device";
  status = AudioDeviceStart(self->aggregateDeviceID, self->ioProcID);
  BOOST_LOG(info) << "AudioDeviceStart returned status: "sv << status;
  if (status != noErr) {
    BOOST_LOG(error) << "Failed to start IOProc with status: "sv << status;
    AudioDeviceDestroyIOProcID(self->aggregateDeviceID, self->ioProcID);
    return status;
  }
  
  return noErr;
}
@end
