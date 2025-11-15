#include "genhwaccel.h"

GenericHwAccelRenderer::GenericHwAccelRenderer(AVHWDeviceType hwDeviceType)
    : IFFmpegRenderer(RendererType::Unknown),
      m_HwDeviceType(hwDeviceType),
      m_HwContext(nullptr)
{

}

GenericHwAccelRenderer::~GenericHwAccelRenderer()
{
    if (m_HwContext != nullptr) {
        av_buffer_unref(&m_HwContext);
    }
}

bool GenericHwAccelRenderer::initialize(PDECODER_PARAMETERS)
{
    int err;

    err = av_hwdevice_ctx_create(&m_HwContext, m_HwDeviceType, nullptr, nullptr, 0);
    if (err != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "av_hwdevice_ctx_create(%u) failed: %d",
                     m_HwDeviceType,
                     err);
        return false;
    }

    return true;
}

bool GenericHwAccelRenderer::prepareDecoderContext(AVCodecContext* context, AVDictionary**)
{
    context->hw_device_ctx = av_buffer_ref(m_HwContext);

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Using generic FFmpeg hwaccel backend (type: %u). Performance may not be optimal!",
                m_HwDeviceType);

    return true;
}

void GenericHwAccelRenderer::renderFrame(AVFrame*)
{
    // We only support indirect rendering
    SDL_assert(false);
}

bool GenericHwAccelRenderer::needsTestFrame()
{
    return true;
}

bool GenericHwAccelRenderer::isDirectRenderingSupported()
{
    // We only support rendering via read-back
    return false;
}

int GenericHwAccelRenderer::getDecoderCapabilities()
{
    bool ok;
    int caps = qEnvironmentVariableIntValue("GENHWACCEL_CAPS", &ok);
    if (ok) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Using GENHWACCEL_CAPS for decoder capabilities: %x",
                    caps);
    }
    else {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Assuming default decoder capabilities. Set GENHWACCEL_CAPS to override.");
        caps = 0;
    }

    return caps;
}
