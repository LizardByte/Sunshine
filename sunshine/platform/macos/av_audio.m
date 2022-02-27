#import "av_audio.h"

@implementation AVAudio

+ (NSArray<AVCaptureDevice *> *)microphones {
  AVCaptureDeviceDiscoverySession *discoverySession = [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInMicrophone,
    AVCaptureDeviceTypeExternalUnknown]
                                                                                                             mediaType:AVMediaTypeAudio
                                                                                                              position:AVCaptureDevicePositionUnspecified];
  return discoverySession.devices;
}

+ (NSArray<NSString *> *)microphoneNames {
  NSMutableArray *result = [[NSMutableArray alloc] init];

  for(AVCaptureDevice *device in [AVAudio microphones]) {
    [result addObject:[device localizedName]];
  }

  return result;
}

+ (AVCaptureDevice *)findMicrophone:(NSString *)name {
  for(AVCaptureDevice *device in [AVAudio microphones]) {
    if([[device localizedName] isEqualToString:name]) {
      return device;
    }
  }

  return nil;
}

- (void)dealloc {
  // make sure we don't process any further samples
  self.audioConnection = nil;
  // make sure nothing gets stuck on this signal
  [self.samplesArrivedSignal signal];
  [self.samplesArrivedSignal release];
  TPCircularBufferCleanup(&audioSampleBuffer);
  [super dealloc];
}

- (int)setupMicrophone:(AVCaptureDevice *)device sampleRate:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels {
  self.audioCaptureSession = [[AVCaptureSession alloc] init];

  NSError *error;
  AVCaptureDeviceInput *audioInput = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
  if(audioInput == nil) {
    return -1;
  }

  if([self.audioCaptureSession canAddInput:audioInput]) {
    [self.audioCaptureSession addInput:audioInput];
  }
  else {
    [audioInput dealloc];
    return -1;
  }

  AVCaptureAudioDataOutput *audioOutput = [[AVCaptureAudioDataOutput alloc] init];

  [audioOutput setAudioSettings:@{
    (NSString *)AVFormatIDKey: [NSNumber numberWithUnsignedInt:kAudioFormatLinearPCM],
    (NSString *)AVSampleRateKey: [NSNumber numberWithUnsignedInt:sampleRate],
    (NSString *)AVNumberOfChannelsKey: [NSNumber numberWithUnsignedInt:channels],
    (NSString *)AVLinearPCMBitDepthKey: [NSNumber numberWithUnsignedInt:16],
    (NSString *)AVLinearPCMIsFloatKey: @NO,
    (NSString *)AVLinearPCMIsNonInterleaved: @NO
  }];

  dispatch_queue_attr_t qos       = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_CONCURRENT,
          QOS_CLASS_USER_INITIATED,
          DISPATCH_QUEUE_PRIORITY_HIGH);
  dispatch_queue_t recordingQueue = dispatch_queue_create("audioSamplingQueue", qos);

  [audioOutput setSampleBufferDelegate:self queue:recordingQueue];

  if([self.audioCaptureSession canAddOutput:audioOutput]) {
    [self.audioCaptureSession addOutput:audioOutput];
  }
  else {
    [audioInput release];
    [audioOutput release];
    return -1;
  }

  self.audioConnection = [audioOutput connectionWithMediaType:AVMediaTypeAudio];

  [self.audioCaptureSession startRunning];

  [audioInput release];
  [audioOutput release];

  self.samplesArrivedSignal = [[NSCondition alloc] init];
  TPCircularBufferInit(&self->audioSampleBuffer, kBufferLength * channels);

  return 0;
}

- (void)captureOutput:(AVCaptureOutput *)output
  didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
         fromConnection:(AVCaptureConnection *)connection {
  if(connection == self.audioConnection) {
    AudioBufferList audioBufferList;
    CMBlockBufferRef blockBuffer;

    CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sampleBuffer, NULL, &audioBufferList, sizeof(audioBufferList), NULL, NULL, 0, &blockBuffer);

    //NSAssert(audioBufferList.mNumberBuffers == 1, @"Expected interlveaved PCM format but buffer contained %u streams", audioBufferList.mNumberBuffers);

    // this is safe, because an interleaved PCM stream has exactly one buffer
    // and we don't want to do sanity checks in a performance critical exec path
    AudioBuffer audioBuffer = audioBufferList.mBuffers[0];

    TPCircularBufferProduceBytes(&self->audioSampleBuffer, audioBuffer.mData, audioBuffer.mDataByteSize);
    [self.samplesArrivedSignal signal];
  }
}

@end
