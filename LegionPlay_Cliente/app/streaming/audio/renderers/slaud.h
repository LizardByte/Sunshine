#pragma once

#include "renderer.h"
#include <SLAudio.h>

class SLAudioRenderer : public IAudioRenderer
{
public:
    SLAudioRenderer();

    virtual ~SLAudioRenderer();

    virtual bool prepareForPlayback(const OPUS_MULTISTREAM_CONFIGURATION* opusConfig);

    virtual void* getAudioBuffer(int* size);

    virtual bool submitAudio(int bytesWritten);

    virtual AudioFormat getAudioBufferFormat();

    virtual void remapChannels(POPUS_MULTISTREAM_CONFIGURATION opusConfig);

private:
    static void slLogCallback(void* context, ESLAudioLog logLevel, const char* message);

    CSLAudioContext* m_AudioContext;
    CSLAudioStream* m_AudioStream;

    void* m_AudioBuffer;
    int m_AudioBufferSize;
    int m_MaxQueuedAudioMs;
};
