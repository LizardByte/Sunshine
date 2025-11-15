#include "swframemapper.h"

SwFrameMapper::SwFrameMapper(IFFmpegRenderer* renderer)
    : m_Renderer(renderer),
      m_VideoFormat(0),
      m_SwPixelFormat(AV_PIX_FMT_NONE),
      m_MapFrame(false)
{
}

void SwFrameMapper::setVideoFormat(int videoFormat)
{
    m_VideoFormat = videoFormat;
}

bool SwFrameMapper::initializeReadBackFormat(AVBufferRef* hwFrameCtxRef, AVFrame* testFrame)
{
    auto hwFrameCtx = (AVHWFramesContext*)hwFrameCtxRef->data;
    int err;
    enum AVPixelFormat *formats;
    AVFrame* outputFrame;

    // This function must only be called once per instance
    SDL_assert(m_SwPixelFormat == AV_PIX_FMT_NONE);
    SDL_assert(!m_MapFrame);
    SDL_assert(m_VideoFormat != 0);

    // Try direct mapping before resorting to copying the frame
    outputFrame = av_frame_alloc();
    if (outputFrame != nullptr) {
        err = av_hwframe_map(outputFrame, testFrame, AV_HWFRAME_MAP_READ);
        if (err == 0) {
            if (m_Renderer->isPixelFormatSupported(m_VideoFormat, (AVPixelFormat)outputFrame->format)) {
                m_SwPixelFormat = (AVPixelFormat)outputFrame->format;
                m_MapFrame = true;
                goto Exit;
            }
            else {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Skipping unsupported hwframe mapping format: %d",
                            outputFrame->format);
            }
        }
        else {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "av_hwframe_map() is unsupported (error: %d)",
                        err);
            SDL_assert(err == AVERROR(ENOSYS));
        }
    }

    // Direct mapping didn't work, so let's see what transfer formats we have
    err = av_hwframe_transfer_get_formats(hwFrameCtxRef, AV_HWFRAME_TRANSFER_DIRECTION_FROM, &formats, 0);
    if (err < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "av_hwframe_transfer_get_formats() failed: %d",
                     err);
        goto Exit;
    }

    // NB: In this algorithm, we prefer to get a preferred hardware readback format
    // and non-preferred rendering format rather than the other way around. This is
    // why we loop through the readback format list in order, rather than searching
    // for the format from getPreferredPixelFormat() in the list.
    for (int i = 0; formats[i] != AV_PIX_FMT_NONE; i++) {
        if (!m_Renderer->isPixelFormatSupported(m_VideoFormat, formats[i])) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Skipping unsupported hwframe transfer format %d",
                        formats[i]);
            continue;
        }

        m_SwPixelFormat = formats[i];
        break;
    }

    av_freep(&formats);

Exit:
    av_frame_free(&outputFrame);

    // If we didn't find any supported formats, try hwFrameCtx->sw_format.
    if (m_SwPixelFormat == AV_PIX_FMT_NONE) {
        if (m_Renderer->isPixelFormatSupported(m_VideoFormat, hwFrameCtx->sw_format)) {
            m_SwPixelFormat = hwFrameCtx->sw_format;
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Unable to find compatible hwframe transfer format (sw_format = %d)",
                         hwFrameCtx->sw_format);
            return false;
        }
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Selected hwframe->swframe format: %d (mapping: %s)",
                m_SwPixelFormat,
                m_MapFrame ? "yes" : "no");
    return true;
}

AVFrame* SwFrameMapper::getSwFrameFromHwFrame(AVFrame* hwFrame)
{
    int err;

    // setVideoFormat() must have been called before our first frame
    SDL_assert(m_VideoFormat != 0);

    if (m_SwPixelFormat == AV_PIX_FMT_NONE) {
        SDL_assert(hwFrame->hw_frames_ctx != nullptr);
        if (!initializeReadBackFormat(hwFrame->hw_frames_ctx, hwFrame)) {
            return nullptr;
        }
    }

    AVFrame* swFrame = av_frame_alloc();
    if (swFrame == nullptr) {
        return nullptr;
    }

    swFrame->format = m_SwPixelFormat;

    if (m_MapFrame) {
        // We don't use AV_HWFRAME_MAP_DIRECT here because it can cause huge
        // performance penalties on Intel hardware with VAAPI due to mappings
        // being uncached memory.
        err = av_hwframe_map(swFrame, hwFrame, AV_HWFRAME_MAP_READ);
        if (err < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "av_hwframe_map() failed: %d",
                         err);
            av_frame_free(&swFrame);
            return nullptr;
        }
    }
    else {
        err = av_hwframe_transfer_data(swFrame, hwFrame, 0);
        if (err < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "av_hwframe_transfer_data() failed: %d",
                         err);
            av_frame_free(&swFrame);
            return nullptr;
        }

        // av_hwframe_transfer_data() doesn't transfer metadata
        // (and can even nuke existing metadata in dst), so we
        // will propagate metadata manually afterwards.
        av_frame_copy_props(swFrame, hwFrame);
    }

    return swFrame;
}
