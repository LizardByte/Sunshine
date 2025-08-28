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

// Forward declaration
@class AVAudio;

// IOProc client data structure
  typedef struct {
    AVAudio *avAudio;
    UInt32 clientRequestedChannels;
    UInt32 clientRequestedSampleRate;
    UInt32 clientRequestedFrameSize;
    AudioConverterRef sampleRateConverter;
  } AVAudioIOProcData;

@interface AVAudio: NSObject <AVCaptureAudioDataOutputSampleBufferDelegate> {
@public
  TPCircularBuffer audioSampleBuffer;
@private
  AudioObjectID tapObjectID;
  AudioObjectID aggregateDeviceID;
  AudioDeviceIOProcID ioProcID;
  AVAudioIOProcData *ioProcData;
}

@property (nonatomic, assign) AVCaptureSession *audioCaptureSession;
@property (nonatomic, assign) AVCaptureConnection *audioConnection;
@property (nonatomic, assign) NSCondition *samplesArrivedSignal;

+ (NSArray *)microphoneNames;
+ (AVCaptureDevice *)findMicrophone:(NSString *)name;

- (int)setupMicrophone:(AVCaptureDevice *)device sampleRate:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels;
- (int)setupSystemTap:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels;

// Buffer management methods for testing and internal use
- (void)initializeAudioBuffer:(UInt8)channels;
- (void)cleanupAudioBuffer;

- (OSStatus)processSystemAudioIOProc:(AudioObjectID)inDevice
                               inNow:(const AudioTimeStamp *)inNow
                        inInputData:(const AudioBufferList *)inInputData
                        inInputTime:(const AudioTimeStamp *)inInputTime
                       outOutputData:(AudioBufferList *)outOutputData
                        inOutputTime:(const AudioTimeStamp *)inOutputTime
                      clientChannels:(UInt32)clientChannels
                     clientFrameSize:(UInt32)clientFrameSize
                    clientSampleRate:(UInt32)clientSampleRate;

@end
