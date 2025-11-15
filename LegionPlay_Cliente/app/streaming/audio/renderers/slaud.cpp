#include "slaud.h"

#include "SDL_compat.h"

SLAudioRenderer::SLAudioRenderer()
    : m_AudioContext(nullptr),
      m_AudioStream(nullptr),
      m_AudioBuffer(nullptr)
{
    SLAudio_SetLogFunction(SLAudioRenderer::slLogCallback, nullptr);
}

bool SLAudioRenderer::prepareForPlayback(const OPUS_MULTISTREAM_CONFIGURATION* opusConfig)
{
    m_AudioContext = SLAudio_CreateContext();
    if (m_AudioContext == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SLAudio_CreateContext() failed");
        return false;
    }

    // This number is pretty conservative (especially for surround), but
    // it's hard to avoid since we get crushed by CPU limitations.
    m_MaxQueuedAudioMs = 40 * opusConfig->channelCount / 2;

    m_AudioBufferSize = opusConfig->samplesPerFrame *
                        opusConfig->channelCount *
                        getAudioBufferSampleSize();
    m_AudioStream = SLAudio_CreateStream(m_AudioContext,
                                         opusConfig->sampleRate,
                                         opusConfig->channelCount,
                                         m_AudioBufferSize,
                                         1);
    if (m_AudioStream == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SLAudio_CreateStream() failed");
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Using SLAudio renderer with %d samples per frame",
                opusConfig->samplesPerFrame);

    return true;
}

void SLAudioRenderer::remapChannels(POPUS_MULTISTREAM_CONFIGURATION opusConfig) {
    OPUS_MULTISTREAM_CONFIGURATION originalConfig = *opusConfig;

    // The Moonlight's default channel order is FL,FR,C,LFE,RL,RR,SL,SR
    // SLAudio expects FL,C,FR,RL,RR,(SL,SR),LFE for 5.1/7.1 so we swap the channels around to match

    if (opusConfig->channelCount == 3 || opusConfig->channelCount >= 6) {
        // Swap FR and C
        opusConfig->mapping[1] = originalConfig.mapping[2];
        opusConfig->mapping[2] = originalConfig.mapping[1];
    }

    if (opusConfig->channelCount >= 6) {
        // SLAudio expects the LFE channel at the end
        opusConfig->mapping[opusConfig->channelCount - 1] = originalConfig.mapping[3];

        // Slide the other channels down
        memcpy(&opusConfig->mapping[3],
               &originalConfig.mapping[4],
               opusConfig->channelCount - 4);
    }
}

void* SLAudioRenderer::getAudioBuffer(int* size)
{
    SDL_assert(*size == m_AudioBufferSize);

    if (m_AudioBuffer == nullptr) {
        m_AudioBuffer = SLAudio_BeginFrame(m_AudioStream);
    }

    return m_AudioBuffer;
}

SLAudioRenderer::~SLAudioRenderer()
{
    if (m_AudioBuffer != nullptr) {
        memset(m_AudioBuffer, 0, m_AudioBufferSize);
        SLAudio_SubmitFrame(m_AudioStream);
    }

    if (m_AudioStream != nullptr) {
        SLAudio_FreeStream(m_AudioStream);
    }

    if (m_AudioContext != nullptr) {
        SLAudio_FreeContext(m_AudioContext);
    }
}

bool SLAudioRenderer::submitAudio(int bytesWritten)
{
    if (bytesWritten == 0) {
        // This buffer will be reused next time
        return true;
    }

    if (LiGetPendingAudioDuration() < m_MaxQueuedAudioMs) {
        SLAudio_SubmitFrame(m_AudioStream);
        m_AudioBuffer = nullptr;
    }
    else {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Too many queued audio frames: %d",
                    LiGetPendingAudioFrames());
    }

    return true;
}

IAudioRenderer::AudioFormat SLAudioRenderer::getAudioBufferFormat()
{
    return AudioFormat::Sint16NE;
}

void SLAudioRenderer::slLogCallback(void*, ESLAudioLog logLevel, const char *message)
{
    SDL_LogPriority priority;

    switch (logLevel)
    {
    case k_ESLAudioLogError:
        priority = SDL_LOG_PRIORITY_ERROR;
        break;
    case k_ESLAudioLogWarning:
        priority = SDL_LOG_PRIORITY_WARN;
        break;
    case k_ESLAudioLogInfo:
        priority = SDL_LOG_PRIORITY_INFO;
        break;
    default:
    case k_ESLAudioLogDebug:
        priority = SDL_LOG_PRIORITY_DEBUG;
        break;
    }

    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION,
                   priority,
                   "SLAudio: %s",
                   message);
}
