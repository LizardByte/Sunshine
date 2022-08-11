#ifndef SUNSHINE_PLATFORM_AV_AUDIO_H
#define SUNSHINE_PLATFORM_AV_AUDIO_H

#import <AVFoundation/AVFoundation.h>

#include "third-party/TPCircularBuffer/TPCircularBuffer.h"

#define kBufferLength 2048

@interface AVAudio : NSObject <AVCaptureAudioDataOutputSampleBufferDelegate> {
@public
  TPCircularBuffer audioSampleBuffer;
}

@property(nonatomic, assign) AVCaptureSession *audioCaptureSession;
@property(nonatomic, assign) AVCaptureConnection *audioConnection;
@property(nonatomic, assign) NSCondition *samplesArrivedSignal;

+ (NSArray *)microphoneNames;
+ (AVCaptureDevice *)findMicrophone:(NSString *)name;

- (int)setupMicrophone:(AVCaptureDevice *)device sampleRate:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels;

@end

#endif //SUNSHINE_PLATFORM_AV_AUDIO_H
