/**
 * @file src/platform/macos/av_audio.h
 * @brief Declarations for audio capture on macOS.
 */
#pragma once

// platform includes
#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/AudioHardwareTapping.h>

// lib includes
#include "third-party/TPCircularBuffer/TPCircularBuffer.h"

#define kBufferLength 4096

NS_ASSUME_NONNULL_BEGIN

// Forward declarations
@class AVAudio;
@class CATapDescription;

// AudioConverter input callback data
struct AudioConverterInputData {
  float *inputData;
  UInt32 inputFrames;
  UInt32 framesProvided;
  UInt32 deviceChannels;
  AVAudio *avAudio;  // Reference to the AVAudio instance
};

// IOProc client data structure
typedef struct {
  AVAudio *avAudio;
  UInt32 clientRequestedChannels;
  UInt32 clientRequestedSampleRate;
  UInt32 clientRequestedFrameSize;
  UInt32 aggregateDeviceSampleRate;
  UInt32 aggregateDeviceChannels;
  AudioConverterRef _Nullable audioConverter;
} AVAudioIOProcData;

@interface AVAudio: NSObject <AVCaptureAudioDataOutputSampleBufferDelegate> {
@public
  TPCircularBuffer audioSampleBuffer;
@private
  AudioObjectID tapObjectID;
  AudioObjectID aggregateDeviceID;
  AudioDeviceIOProcID ioProcID;
  AVAudioIOProcData * _Nullable ioProcData;
}

@property (nonatomic, assign, nullable) AVCaptureSession *audioCaptureSession;
@property (nonatomic, assign, nullable) AVCaptureConnection *audioConnection;
@property (nonatomic, assign, nullable) NSCondition *samplesArrivedSignal;

+ (NSArray<AVCaptureDevice *> *)microphones;
+ (NSArray<NSString *> *)microphoneNames;
+ (nullable AVCaptureDevice *)findMicrophone:(NSString *)name;

- (int)setupMicrophone:(AVCaptureDevice *)device sampleRate:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels;
- (int)setupSystemTap:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels;

// Buffer management methods for testing and internal use
- (void)initializeAudioBuffer:(UInt8)channels;
- (void)cleanupAudioBuffer;

- (int)initSystemTapContext:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels;
- (nullable CATapDescription *)createSystemTapDescriptionForChannels:(UInt8)channels;
- (OSStatus)createAggregateDeviceWithTapDescription:(CATapDescription *)tapDescription sampleRate:(UInt32)sampleRate frameSize:(UInt32)frameSize;
- (OSStatus)audioConverterComplexInputProc:(AudioConverterRef)inAudioConverter
                        ioNumberDataPackets:(UInt32 *)ioNumberDataPackets
                                     ioData:(AudioBufferList *)ioData
                     outDataPacketDescription:(AudioStreamPacketDescription * _Nullable * _Nullable)outDataPacketDescription
                                   inputInfo:(struct AudioConverterInputData *)inputInfo;
- (OSStatus)systemAudioIOProc:(AudioObjectID)inDevice
                               inNow:(const AudioTimeStamp *)inNow
                        inInputData:(const AudioBufferList *)inInputData
                        inInputTime:(const AudioTimeStamp *)inInputTime
                       outOutputData:(nullable AudioBufferList *)outOutputData
                        inOutputTime:(const AudioTimeStamp *)inOutputTime
                      clientChannels:(UInt32)clientChannels
                     clientFrameSize:(UInt32)clientFrameSize
                    clientSampleRate:(UInt32)clientSampleRate;

@end

NS_ASSUME_NONNULL_END
