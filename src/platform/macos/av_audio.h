/**
 * @file src/platform/macos/av_audio.h
 * @brief Declarations for macOS audio capture with dual input paths.
 *
 * This header defines the AVAudio class which provides distinct audio capture methods:
 * 1. **Microphone capture** - Uses AVFoundation framework to capture from specific microphone devices
 * 2. **System-wide audio tap** - Uses Core Audio taps to capture all system audio output (macOS 14.2+)
 *
 * The system-wide audio tap allows capturing audio from all applications and system sounds,
 * while microphone capture focuses on input from physical or virtual microphone devices.
 */
#pragma once

// platform includes
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CoreAudio.h>

// lib includes
#include "third-party/TPCircularBuffer/TPCircularBuffer.h"

// Buffer length for audio processing
#define kBufferLength 4096

NS_ASSUME_NONNULL_BEGIN

// Forward declarations
@class AVAudio;
@class CATapDescription;

namespace platf {
  OSStatus audioConverterComplexInputProc(AudioConverterRef _Nullable inAudioConverter, UInt32 * _Nonnull ioNumberDataPackets, AudioBufferList * _Nonnull ioData, AudioStreamPacketDescription * _Nullable * _Nullable outDataPacketDescription, void * _Nonnull inUserData);
  OSStatus systemAudioIOProc(AudioObjectID inDevice, const AudioTimeStamp * _Nullable inNow, const AudioBufferList * _Nullable inInputData, const AudioTimeStamp * _Nullable inInputTime, AudioBufferList * _Nullable outOutputData, const AudioTimeStamp * _Nullable inOutputTime, void * _Nullable inClientData);
}

/**
 * @brief Data structure for AudioConverter input callback.
 * Contains audio data and metadata needed for format conversion during audio processing.
 */
struct AudioConverterInputData {
  float *inputData;  ///< Pointer to input audio data
  UInt32 inputFrames;  ///< Total number of input frames available
  UInt32 framesProvided;  ///< Number of frames already provided to converter
  UInt32 deviceChannels;  ///< Number of channels in the device audio
  AVAudio *avAudio;  ///< Reference to the AVAudio instance
};

/**
 * @brief IOProc client data structure for Core Audio system taps.
 * Contains configuration and conversion data for real-time audio processing.
 */
typedef struct {
  AVAudio *avAudio;  ///< Reference to AVAudio instance
  UInt32 clientRequestedChannels;  ///< Number of channels requested by client
  UInt32 clientRequestedSampleRate;  ///< Sample rate requested by client
  UInt32 clientRequestedFrameSize;  ///< Frame size requested by client
  UInt32 aggregateDeviceSampleRate;  ///< Sample rate of the aggregate device
  UInt32 aggregateDeviceChannels;  ///< Number of channels in aggregate device
  AudioConverterRef _Nullable audioConverter;  ///< Audio converter for format conversion
  float * _Nullable conversionBuffer;  ///< Pre-allocated buffer for audio conversion
  UInt32 conversionBufferSize;  ///< Size of the conversion buffer in bytes
} AVAudioIOProcData;

/**
 * @brief Core Audio capture class for macOS audio input and system-wide audio tapping.
 * Provides functionality for both microphone capture via AVFoundation and system-wide
 * audio capture via Core Audio taps (requires macOS 14.2+).
 */
@interface AVAudio: NSObject <AVCaptureAudioDataOutputSampleBufferDelegate> {
@public
  TPCircularBuffer audioSampleBuffer;  ///< Shared circular buffer for both audio capture paths
  dispatch_semaphore_t audioSemaphore;  ///< Real-time safe semaphore for signaling audio sample availability
@private
  // System-wide audio tap components (Core Audio)
  AudioObjectID tapObjectID;  ///< Core Audio tap object identifier for system audio capture
  AudioObjectID aggregateDeviceID;  ///< Aggregate device ID for system tap audio routing
  AudioDeviceIOProcID ioProcID;  ///< IOProc identifier for real-time audio processing
  AVAudioIOProcData *_Nullable ioProcData;  ///< Context data for IOProc callbacks and format conversion
}

// AVFoundation microphone capture properties
@property (nonatomic, assign, nullable) AVCaptureSession *audioCaptureSession;  ///< AVFoundation capture session for microphone input
@property (nonatomic, assign, nullable) AVCaptureConnection *audioConnection;  ///< Audio connection within the capture session

/**
 * @brief Get all available microphone devices on the system.
 * @return Array of AVCaptureDevice objects representing available microphones
 */
+ (NSArray<AVCaptureDevice *> *)microphones;

/**
 * @brief Get names of all available microphone devices.
 * @return Array of NSString objects with microphone device names
 */
+ (NSArray<NSString *> *)microphoneNames;

/**
 * @brief Find a specific microphone device by name.
 * @param name The name of the microphone to find (nullable - will return nil if name is nil)
 * @return AVCaptureDevice object if found, nil otherwise
 */
+ (nullable AVCaptureDevice *)findMicrophone:(nullable NSString *)name;

/**
 * @brief Sets up microphone capture using AVFoundation framework.
 * @param device The AVCaptureDevice to use for audio input (nullable - will return error if nil)
 * @param sampleRate Target sample rate in Hz
 * @param frameSize Number of frames per buffer
 * @param channels Number of audio channels (1=mono, 2=stereo)
 * @return 0 on success, -1 on failure
 */
- (int)setupMicrophone:(nullable AVCaptureDevice *)device sampleRate:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels;

/**
 * @brief Sets up system-wide audio tap for capturing all system audio.
 * Requires macOS 14.2+ and appropriate permissions.
 * @param sampleRate Target sample rate in Hz
 * @param frameSize Number of frames per buffer
 * @param channels Number of audio channels
 * @return 0 on success, -1 on failure
 */
- (int)setupSystemTap:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels;

// Buffer management methods for testing and internal use
/**
 * @brief Initializes the circular audio buffer for the specified number of channels.
 * @param channels Number of audio channels to configure the buffer for
 */
- (void)initializeAudioBuffer:(UInt8)channels;

/**
 * @brief Cleans up and deallocates the audio buffer resources.
 */
- (void)cleanupAudioBuffer;

/**
 * @brief Cleans up system tap resources in a safe, ordered manner.
 * @param tapDescription Optional tap description object to release (can be nil)
 */
- (void)cleanupSystemTapContext:(nullable id)tapDescription;

/**
 * @brief Initializes the system tap context with specified audio parameters.
 * @param sampleRate Target sample rate in Hz
 * @param frameSize Number of frames per buffer
 * @param channels Number of audio channels
 * @return 0 on success, -1 on failure
 */
- (int)initializeSystemTapContext:(UInt32)sampleRate frameSize:(UInt32)frameSize channels:(UInt8)channels;

/**
 * @brief Creates a Core Audio tap description for system audio capture.
 * @param channels Number of audio channels to configure the tap for
 * @return CATapDescription object on success, nil on failure
 */
- (nullable CATapDescription *)createSystemTapDescriptionForChannels:(UInt8)channels;

/**
 * @brief Creates an aggregate device with the specified tap description and audio parameters.
 * @param tapDescription Core Audio tap description for system audio capture
 * @param sampleRate Target sample rate in Hz
 * @param frameSize Number of frames per buffer
 * @return OSStatus indicating success (noErr) or error code
 */
- (OSStatus)createAggregateDeviceWithTapDescription:(CATapDescription *)tapDescription sampleRate:(UInt32)sampleRate frameSize:(UInt32)frameSize;

@end

NS_ASSUME_NONNULL_END
