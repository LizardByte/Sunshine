#include "slvid.h"
#include "streaming/session.h"

SLVideoDecoder::SLVideoDecoder(bool)
    : m_VideoContext(nullptr),
      m_VideoStream(nullptr),
      m_Overlay(nullptr),
      m_ViewportWidth(0),
      m_ViewportHeight(0)
{
    SLVideo_SetLogFunction(SLVideoDecoder::slLogCallback, nullptr);
}

SLVideoDecoder::~SLVideoDecoder()
{
    Session* session = Session::get();
    if (session != nullptr) {
        session->getOverlayManager().setOverlayRenderer(nullptr);
    }

    if (m_VideoStream != nullptr) {
        SLVideo_FreeStream(m_VideoStream);
    }

    if (m_Overlay != nullptr) {
        SLVideo_HideOverlay(m_Overlay);
        SLVideo_FreeOverlay(m_Overlay);
    }

    if (m_VideoContext != nullptr) {
        if (session != nullptr && m_ViewportWidth != 0 && m_ViewportHeight != 0) {
            // HACK: Fix the overlay that Qt uses to render otherwise the GUI will
            // be squished into an overlay the size of what Moonlight used.
            CSLVideoOverlay* hackOverlay = SLVideo_CreateOverlay(m_VideoContext, m_ViewportWidth, m_ViewportHeight);

            // Quickly show and hide the overlay to flush the overlay changes to the display hardware
            SLVideo_SetOverlayDisplayFullscreen(hackOverlay);
            SLVideo_ShowOverlay(hackOverlay);
            SLVideo_HideOverlay(hackOverlay);
            SLVideo_FreeOverlay(hackOverlay);
        }

        SLVideo_FreeContext(m_VideoContext);
    }
}

bool
SLVideoDecoder::isHardwareAccelerated()
{
    // SLVideo is always hardware accelerated
    return true;
}

bool SLVideoDecoder::isAlwaysFullScreen()
{
    return true;
}

int
SLVideoDecoder::getDecoderCapabilities()
{
    return 0;
}

int
SLVideoDecoder::getDecoderColorspace()
{
    return COLORSPACE_REC_709;
}

int
SLVideoDecoder::getDecoderColorRange()
{
    return COLOR_RANGE_LIMITED;
}

QSize SLVideoDecoder::getDecoderMaxResolution()
{
    return QSize(1920, 1080);
}

bool
SLVideoDecoder::initialize(PDECODER_PARAMETERS params)
{
    // SLVideo only supports hardware decoding
    if (params->vds == StreamingPreferences::VDS_FORCE_SOFTWARE) {
        return false;
    }

    // SLVideo only supports H.264
    if (params->videoFormat != VIDEO_FORMAT_H264) {
        return false;
    }

    m_VideoContext = SLVideo_CreateContext();
    if (!m_VideoContext) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SLVideo_CreateContext() failed");
        return false;
    }

    // Create a low latency H.264 stream
    m_VideoStream = SLVideo_CreateStream(m_VideoContext, k_ESLVideoFormatH264, 1);
    if (!m_VideoStream) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SLVideo_CreateStream() failed");
        return false;
    }

    SLVideo_SetStreamVideoTransferMatrix(m_VideoStream, k_ESLVideoTransferMatrix_BT709);
    SLVideo_SetStreamTargetFramerate(m_VideoStream, params->frameRate, 1);

    SDL_GetWindowSize(params->window, &m_ViewportWidth, &m_ViewportHeight);

    // Register ourselves for overlay callbacks
    Session* session = Session::get();
    if (session != nullptr) {
        session->getOverlayManager().setOverlayRenderer(this);
    }

    return true;
}

int
SLVideoDecoder::submitDecodeUnit(PDECODE_UNIT du)
{
    int err;

    err = SLVideo_BeginFrame(m_VideoStream, du->fullLength);
    if (err < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SLVideo_BeginFrame() failed: %d (frame %d)",
                    err,
                    du->frameNumber);

        // Need an IDR frame to resync
        return DR_NEED_IDR;
    }

    PLENTRY entry = du->bufferList;
    while (entry != nullptr) {
        err = SLVideo_WriteFrameData(m_VideoStream,
                                     entry->data,
                                     entry->length);
        if (err < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "SLVideo_WriteFrameData() failed: %d (frame %d)",
                        err,
                        du->frameNumber);

            // Need an IDR frame to resync
            return DR_NEED_IDR;
        }

        entry = entry->next;
    }

    err = SLVideo_SubmitFrame(m_VideoStream);
    if (err < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SLVideo_SubmitFrame() failed: %d (frame %d)",
                    err,
                    du->frameNumber);

        // Need an IDR frame to resync
        return DR_NEED_IDR;
    }

    return DR_OK;
}

void SLVideoDecoder::notifyOverlayUpdated(Overlay::OverlayType type)
{
    // SLVideo supports only one visible overlay at a time. Since we don't have
    // stats like the FFmpeg-based decoders, we'll just support the status update
    // overlay and nothing else.
    if (type != Overlay::OverlayStatusUpdate) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unsupported overlay type: %d", type);
        return;
    }

    SDL_Surface* newSurface = Session::get()->getOverlayManager().getUpdatedOverlaySurface(type);
    bool overlayEnabled = Session::get()->getOverlayManager().isOverlayEnabled(type);
    if (newSurface == nullptr && overlayEnabled) {
        // There's no updated surface and the overlay is enabled, so just leave the old surface alone.
        return;
    }

    // Hide and free any existing overlay
    if (m_Overlay != nullptr) {
        SLVideo_HideOverlay(m_Overlay);
        SLVideo_FreeOverlay(m_Overlay);
        m_Overlay = nullptr;
    }

    if (!overlayEnabled) {
        SDL_FreeSurface(newSurface);
        return;
    }

    m_Overlay = SLVideo_CreateOverlay(m_VideoContext, newSurface->w, newSurface->h);
    if (m_Overlay == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SLVideo_CreateOverlay() failed");
        SDL_FreeSurface(newSurface);
        return;
    }

    uint32_t* pixels;
    int pitch;
    SLVideo_GetOverlayPixels(m_Overlay, &pixels, &pitch);

    // Copy surface pixels into the new overlay
    SDL_ConvertPixels(newSurface->w, newSurface->h, newSurface->format->format, newSurface->pixels, newSurface->pitch,
                      SDL_PIXELFORMAT_ARGB8888, pixels, pitch);

    // Position the status overlay at the bottom left corner
    float flWidth = (float)newSurface->w / m_ViewportWidth;
    float flHeight = (float)newSurface->h / m_ViewportHeight;
    SLVideo_SetOverlayDisplayArea(m_Overlay, 0.0f, 1.0f - flHeight, flWidth, flHeight);

    // We're done with the surface now
    SDL_FreeSurface(newSurface);

    // Show the overlay
    SLVideo_ShowOverlay(m_Overlay);
}

void SLVideoDecoder::slLogCallback(void*, ESLVideoLog logLevel, const char *message)
{
    SDL_LogPriority priority;

    switch (logLevel)
    {
    case k_ESLVideoLogError:
        priority = SDL_LOG_PRIORITY_ERROR;
        break;
    case k_ESLVideoLogWarning:
        priority = SDL_LOG_PRIORITY_WARN;
        break;
    case k_ESLVideoLogInfo:
        priority = SDL_LOG_PRIORITY_INFO;
        break;
    default:
    case k_ESLVideoLogDebug:
        priority = SDL_LOG_PRIORITY_DEBUG;
        break;
    }

    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION,
                   priority,
                   "SLVideo: %s",
                   message);
}
