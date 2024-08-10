/**
 * @file src/platform/macos/av_audio.h
 * @brief Declarations for audio capture on macOS.
 */
#pragma once

#import <AVFoundation/AVFoundation.h>

#include "third-party/TPCircularBuffer/TPCircularBuffer.h"

#define kBufferLength 4096

@interface AVAudio: NSObject <AVCaptureAudioDataOutputSampleBufferDelegate> {
@public
  TPCircularBuffer audioSampleBuffer;
}

@property (nonatomic, assign) AVCaptureSession *audioCaptureSession;
@property (nonatomic, assign) AVCaptureConnection *audioConnection;
@property (nonatomic, assign) NSCondition *samplesArrivedSignal;

+ (NSArray *)microphoneNames;
+ (AVCaptureDevice *)findMicrophone:(NSString *)name;

- (int)setupMicrophone:(AVCaptureDevice *)device sampleRate:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels;

@end
