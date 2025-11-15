#include "sdlvid.h"

#include "streaming/session.h"
#include "streaming/streamutils.h"

#include <Limelight.h>

#include <SDL_syswm.h>

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
}

SdlRenderer::SdlRenderer()
    : IFFmpegRenderer(RendererType::SDL),
      m_VideoFormat(0),
      m_Renderer(nullptr),
      m_Texture(nullptr),
      m_NeedsYuvToRgbConversion(false),
      m_SwsContext(nullptr),
      m_RgbFrame(av_frame_alloc()),
      m_SwFrameMapper(this)
{
    SDL_zero(m_OverlayTextures);

#ifdef HAVE_CUDA
    m_CudaGLHelper = nullptr;
#endif
}

SdlRenderer::~SdlRenderer()
{
#ifdef HAVE_CUDA
    if (m_CudaGLHelper != nullptr) {
        delete m_CudaGLHelper;
    }
#endif

    for (int i = 0; i < Overlay::OverlayMax; i++) {
        if (m_OverlayTextures[i] != nullptr) {
            SDL_DestroyTexture(m_OverlayTextures[i]);
        }
    }

    av_frame_free(&m_RgbFrame);
    sws_freeContext(m_SwsContext);

    if (m_Texture != nullptr) {
        SDL_DestroyTexture(m_Texture);
    }

    if (m_Renderer != nullptr) {
        SDL_DestroyRenderer(m_Renderer);
    }
}

bool SdlRenderer::prepareDecoderContext(AVCodecContext*, AVDictionary**)
{
    /* Nothing to do */

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Using SDL renderer");

    return true;
}

void SdlRenderer::prepareToRender()
{
    // Draw a black frame until the video stream starts rendering
    SDL_SetRenderDrawColor(m_Renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(m_Renderer);
    SDL_RenderPresent(m_Renderer);
}

bool SdlRenderer::isRenderThreadSupported()
{
    SDL_RendererInfo info;
    SDL_GetRendererInfo(m_Renderer, &info);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SDL renderer backend: %s",
                info.name);

    if (info.name != QString("direct3d") && info.name != QString("metal")) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL renderer backend requires main thread rendering");
        return false;
    }

    return true;
}

bool SdlRenderer::isPixelFormatSupported(int videoFormat, AVPixelFormat pixelFormat)
{
    if (videoFormat & (VIDEO_FORMAT_MASK_10BIT | VIDEO_FORMAT_MASK_YUV444)) {
        // SDL2 can't natively handle textures with these formats, but we can perform
        // conversion on the CPU using swscale then upload them as an RGB texture.
        const AVPixFmtDescriptor* formatDesc = av_pix_fmt_desc_get(pixelFormat);
        if (!formatDesc) {
            SDL_assert(formatDesc);
            return false;
        }

        const int expectedPixelDepth = (videoFormat & VIDEO_FORMAT_MASK_10BIT) ? 10 : 8;
        const int expectedLog2ChromaW = (videoFormat & VIDEO_FORMAT_MASK_YUV444) ? 0 : 1;
        const int expectedLog2ChromaH = (videoFormat & VIDEO_FORMAT_MASK_YUV444) ? 0 : 1;

        return formatDesc->comp[0].depth == expectedPixelDepth &&
               formatDesc->log2_chroma_w == expectedLog2ChromaW &&
               formatDesc->log2_chroma_h == expectedLog2ChromaH;
    }
    else {
        // The formats listed below are natively supported by SDL, so it can handle
        // YUV to RGB conversion on the GPU using pixel shaders.
        //
        // Remember to keep this in sync with SdlRenderer::renderFrame()!
        switch (pixelFormat) {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P:
        case AV_PIX_FMT_NV12:
        case AV_PIX_FMT_NV21:
            return true;

        default:
            return false;
        }
    }
}

bool SdlRenderer::initialize(PDECODER_PARAMETERS params)
{
    Uint32 rendererFlags = SDL_RENDERER_ACCELERATED;

    m_VideoFormat = params->videoFormat;
    m_SwFrameMapper.setVideoFormat(m_VideoFormat);

    if (params->videoFormat & VIDEO_FORMAT_MASK_10BIT) {
        // SDL doesn't support rendering HDR yet
        return false;
    }

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(params->window, &info)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_GetWindowWMInfo() failed: %s",
                     SDL_GetError());
        return false;
    }

    // Only set SDL_RENDERER_PRESENTVSYNC if we know we'll get tearing otherwise.
    // Since we don't use V-Sync to pace our frame rate, we want non-blocking
    // presents to reduce video latency.
    switch (info.subsystem) {
    case SDL_SYSWM_WINDOWS:
        // DWM is always tear-free except in full-screen exclusive mode
        if ((SDL_GetWindowFlags(params->window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN) {
            if (params->enableVsync) {
                rendererFlags |= SDL_RENDERER_PRESENTVSYNC;
            }
        }
        break;
    case SDL_SYSWM_WAYLAND:
        // Wayland is always tear-free in all modes
        break;
    default:
        // For other subsystems, just set SDL_RENDERER_PRESENTVSYNC if asked
        if (params->enableVsync) {
            rendererFlags |= SDL_RENDERER_PRESENTVSYNC;
        }
        break;
    }

#ifdef Q_OS_WIN32
    // We render on a different thread than the main thread which is handling window
    // messages. Without D3DCREATE_MULTITHREADED, this can cause a deadlock by blocking
    // on a window message being processed while the main thread is blocked waiting for
    // the render thread to finish.
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DIRECT3D_THREADSAFE, "1", SDL_HINT_OVERRIDE);
#endif

    m_Renderer = SDL_CreateRenderer(params->window, -1, rendererFlags);
    if (!m_Renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_CreateRenderer() failed: %s",
                     SDL_GetError());
    }

    // SDL_CreateRenderer() can end up having to recreate our window (SDL_RecreateWindow())
    // to ensure it's compatible with the renderer's OpenGL context. If that happens, we
    // can get spurious SDL_WINDOWEVENT events that will cause us to (again) recreate our
    // renderer. This can lead to an infinite to renderer recreation, so discard all
    // SDL_WINDOWEVENT events after SDL_CreateRenderer().
    Session* session = Session::get();
    if (session != nullptr) {
        // If we get here during a session, we need to synchronize with the event loop
        // to ensure we don't drop any important events.
        session->flushWindowEvents();
    }
    else {
        // If we get here prior to the start of a session, just pump and flush ourselves.
        SDL_PumpEvents();
        SDL_FlushEvent(SDL_WINDOWEVENT);
    }

    if (!m_Renderer) {
        m_InitFailureReason = InitFailureReason::NoSoftwareSupport;
        return false;
    }

#ifdef Q_OS_WIN32
    // For some reason, using Direct3D9Ex breaks this with multi-monitor setups.
    // When focus is lost, the window is minimized then immediately restored without
    // input focus. This glitches out the renderer and a bunch of other stuff.
    // Direct3D9Ex itself seems to have this minimize on focus loss behavior on its
    // own, so just disable SDL's handling of the focus loss event.
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0", SDL_HINT_OVERRIDE);
#endif

    return true;
}

void SdlRenderer::renderOverlay(Overlay::OverlayType type)
{
    if (Session::get()->getOverlayManager().isOverlayEnabled(type)) {
        // If a new surface has been created for updated overlay data, convert it into a texture.
        // NB: We have to do this conversion at render-time because we can only interact
        // with the renderer on a single thread.
        SDL_Surface* newSurface = Session::get()->getOverlayManager().getUpdatedOverlaySurface(type);
        if (newSurface != nullptr) {
            if (m_OverlayTextures[type] != nullptr) {
                SDL_DestroyTexture(m_OverlayTextures[type]);
            }

            if (type == Overlay::OverlayStatusUpdate) {
                // Bottom Left
                SDL_Rect viewportRect;
                SDL_RenderGetViewport(m_Renderer, &viewportRect);
                m_OverlayRects[type].x = 0;
                m_OverlayRects[type].y = viewportRect.h - newSurface->h;
            }
            else if (type == Overlay::OverlayDebug) {
                // Top left
                m_OverlayRects[type].x = 0;
                m_OverlayRects[type].y = 0;
            }

            m_OverlayRects[type].w = newSurface->w;
            m_OverlayRects[type].h = newSurface->h;

            m_OverlayTextures[type] = SDL_CreateTextureFromSurface(m_Renderer, newSurface);
            SDL_FreeSurface(newSurface);
        }

        // If we have an overlay texture, render it too
        if (m_OverlayTextures[type] != nullptr) {
            SDL_RenderCopy(m_Renderer, m_OverlayTextures[type], nullptr, &m_OverlayRects[type]);
        }
    }
}

void SdlRenderer::ffNoopFree(void*, uint8_t*)
{
    // Nothing
}

void SdlRenderer::renderFrame(AVFrame* frame)
{
    int err;
    AVFrame* swFrame = nullptr;

    if (frame->hw_frames_ctx != nullptr && frame->format != AV_PIX_FMT_CUDA) {
#ifdef HAVE_CUDA
ReadbackRetry:
#endif
        // If we are acting as the frontend for a hardware
        // accelerated decoder, we'll need to read the frame
        // back to render it.

        // Map or copy this hwframe to a swframe that we can work with
        frame = swFrame = m_SwFrameMapper.getSwFrameFromHwFrame(frame);
        if (swFrame == nullptr) {
            return;
        }
    }

    // Recreate the texture if the frame format or size changes
    if (hasFrameFormatChanged(frame)) {
#ifdef HAVE_CUDA
        if (m_CudaGLHelper != nullptr) {
            delete m_CudaGLHelper;
            m_CudaGLHelper = nullptr;
        }
#endif

        if (m_Texture != nullptr) {
            SDL_DestroyTexture(m_Texture);
            m_Texture = nullptr;
        }
    }

    if (m_Texture == nullptr) {
        Uint32 sdlFormat;

        // Remember to keep this in sync with SdlRenderer::isPixelFormatSupported()!
        m_NeedsYuvToRgbConversion = false;
        switch (frame->format)
        {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P:
            sdlFormat = SDL_PIXELFORMAT_YV12;
            break;
        case AV_PIX_FMT_CUDA:
        case AV_PIX_FMT_NV12:
            sdlFormat = SDL_PIXELFORMAT_NV12;
            break;
        case AV_PIX_FMT_NV21:
            sdlFormat = SDL_PIXELFORMAT_NV21;
            break;
        default:
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Performing color conversion on CPU due to lack of SDL support for format: %s",
                        av_get_pix_fmt_name((AVPixelFormat)frame->format));
            sdlFormat = SDL_PIXELFORMAT_XRGB8888;
            m_NeedsYuvToRgbConversion = true;
            break;
        }

        if (m_NeedsYuvToRgbConversion) {
            m_RgbFrame->width = frame->width;
            m_RgbFrame->height = frame->height;
            m_RgbFrame->format = AV_PIX_FMT_BGR0;

            sws_freeContext(m_SwsContext);

#if LIBSWSCALE_VERSION_INT >= AV_VERSION_INT(6, 1, 100)
            m_SwsContext = sws_alloc_context();
            if (!m_SwsContext) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "sws_alloc_context() failed");
                goto Exit;
            }

            AVDictionary *options { nullptr };
            av_dict_set_int(&options, "srcw", frame->width, 0);
            av_dict_set_int(&options, "srch", frame->height, 0);
            av_dict_set_int(&options, "src_format", frame->format, 0);
            av_dict_set_int(&options, "dstw", m_RgbFrame->width, 0);
            av_dict_set_int(&options, "dsth", m_RgbFrame->height, 0);
            av_dict_set_int(&options, "dst_format", m_RgbFrame->format, 0);
            av_dict_set_int(&options, "threads", std::min(SDL_GetCPUCount(), 4), 0); // Up to 4 threads

            err = av_opt_set_dict(m_SwsContext, &options);
            av_dict_free(&options);
            if (err < 0) {
                char string[AV_ERROR_MAX_STRING_SIZE];
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "av_opt_set_dict() failed: %s",
                             av_make_error_string(string, sizeof(string), err));
                goto Exit;
            }

            err = sws_init_context(m_SwsContext, nullptr, nullptr);
            if (err < 0) {
                char string[AV_ERROR_MAX_STRING_SIZE];
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "sws_init_context() failed: %s",
                             av_make_error_string(string, sizeof(string), err));
                goto Exit;
            }
#else
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "CPU color conversion is slow on FFmpeg 4.x. Update FFmpeg for better performance.");

            m_SwsContext = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                                          m_RgbFrame->width, m_RgbFrame->height, (AVPixelFormat)m_RgbFrame->format,
                                          0, nullptr, nullptr, nullptr);
            if (!m_SwsContext) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "sws_getContext() failed");
                goto Exit;
            }
#endif
        }
        else {
            // SDL will perform YUV conversion on the GPU
            switch (getFrameColorspace(frame))
            {
            case COLORSPACE_REC_709:
                SDL_assert(!isFrameFullRange(frame));
                SDL_SetYUVConversionMode(SDL_YUV_CONVERSION_BT709);
                break;
            case COLORSPACE_REC_601:
                if (isFrameFullRange(frame)) {
                    // SDL's JPEG mode is Rec 601 Full Range
                    SDL_SetYUVConversionMode(SDL_YUV_CONVERSION_JPEG);
                }
                else {
                    SDL_SetYUVConversionMode(SDL_YUV_CONVERSION_BT601);
                }
                break;
            default:
                break;
            }
        }

        m_Texture = SDL_CreateTexture(m_Renderer,
                                      sdlFormat,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      frame->width,
                                      frame->height);
        if (!m_Texture) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL_CreateTexture() failed: %s",
                         SDL_GetError());
            goto Exit;
        }

#ifdef HAVE_CUDA
        if (frame->format == AV_PIX_FMT_CUDA) {
            SDL_assert(m_CudaGLHelper == nullptr);
            m_CudaGLHelper = new CUDAGLInteropHelper(((AVHWFramesContext*)frame->hw_frames_ctx->data)->device_ctx);

            SDL_GL_BindTexture(m_Texture, nullptr, nullptr);
            if (!m_CudaGLHelper->registerBoundTextures()) {
                // If we can't register textures, fall back to normal read-back rendering
                delete m_CudaGLHelper;
                m_CudaGLHelper = nullptr;
            }
            SDL_GL_UnbindTexture(m_Texture);
        }
#endif
    }

    if (frame->format == AV_PIX_FMT_CUDA) {
#ifdef HAVE_CUDA
        if (m_CudaGLHelper == nullptr || !m_CudaGLHelper->copyCudaFrameToTextures(frame)) {
            goto ReadbackRetry;
        }
#else
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Got CUDA frame, but not built with CUDA support!");
        goto Exit;
#endif
    }
    else if (frame->format == AV_PIX_FMT_YUV420P || frame->format == AV_PIX_FMT_YUVJ420P) {
        SDL_UpdateYUVTexture(m_Texture, nullptr,
                             frame->data[0],
                             frame->linesize[0],
                             frame->data[1],
                             frame->linesize[1],
                             frame->data[2],
                             frame->linesize[2]);
    }
    else if (!m_NeedsYuvToRgbConversion) {
#if SDL_VERSION_ATLEAST(2, 0, 15)
        // SDL_UpdateNVTexture is not supported on all renderer backends,
        // (notably not DX9), so we must have a fallback in case it's not
        // supported and for earlier versions of SDL.
        if (SDL_UpdateNVTexture(m_Texture,
                                nullptr,
                                frame->data[0],
                                frame->linesize[0],
                                frame->data[1],
                                frame->linesize[1]) != 0)
#endif
        {
            char* pixels;
            int texturePitch;

            err = SDL_LockTexture(m_Texture, nullptr, (void**)&pixels, &texturePitch);
            if (err < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "SDL_LockTexture() failed: %s",
                             SDL_GetError());
                goto Exit;
            }

            // If the planar pitches match, we can use a single memcpy() to transfer
            // the data. If not, we'll need to do separate memcpy() calls for each
            // line to ensure the pitch doesn't get screwed up.

            if (frame->linesize[0] == texturePitch) {
                memcpy(pixels,
                       frame->data[0],
                       frame->linesize[0] * frame->height);
            }
            else {
                int pitch = SDL_min(frame->linesize[0], texturePitch);
                for (int i = 0; i < frame->height; i++) {
                    memcpy(pixels + (texturePitch * i),
                           frame->data[0] + (frame->linesize[0] * i),
                           pitch);
                }
            }

            if (frame->linesize[1] == texturePitch) {
                memcpy(pixels + (texturePitch * frame->height),
                       frame->data[1],
                       frame->linesize[1] * frame->height / 2);
            }
            else {
                int pitch = SDL_min(frame->linesize[1], texturePitch);
                for (int i = 0; i < frame->height / 2; i++) {
                    memcpy(pixels + (texturePitch * (frame->height + i)),
                           frame->data[1] + (frame->linesize[1] * i),
                           pitch);
                }
            }

            SDL_UnlockTexture(m_Texture);
        }
    }
    else {
        // We have a pixel format that SDL doesn't natively support, so we must use
        // swscale to convert the YUV frame into an RGB frame to upload to the GPU.
        uint8_t* pixels;
        int texturePitch;

        err = SDL_LockTexture(m_Texture, nullptr, (void**)&pixels, &texturePitch);
        if (err < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL_LockTexture() failed: %s",
                         SDL_GetError());
            goto Exit;
        }

        // Create a buffer to wrap our locked texture buffer
        m_RgbFrame->buf[0] = av_buffer_create(pixels, m_RgbFrame->height * texturePitch, ffNoopFree, nullptr, 0);
        m_RgbFrame->data[0] = pixels;
        m_RgbFrame->linesize[0] = texturePitch;

#if LIBSWSCALE_VERSION_INT >= AV_VERSION_INT(6, 1, 100)
        // Perform multi-threaded color conversion into the locked texture buffer
        err = sws_scale_frame(m_SwsContext, m_RgbFrame, frame);
#else
        // Perform a single-threaded color conversion using the legacy swscale API
        err = sws_scale(m_SwsContext, frame->data, frame->linesize, 0, frame->height,
                        m_RgbFrame->data, m_RgbFrame->linesize);
#endif

        av_buffer_unref(&m_RgbFrame->buf[0]);
        SDL_UnlockTexture(m_Texture);

        if (err < 0) {
            char string[AV_ERROR_MAX_STRING_SIZE];
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "sws_scale_frame() failed: %s",
                         av_make_error_string(string, AV_ERROR_MAX_STRING_SIZE, err));
            goto Exit;
        }
    }

    SDL_RenderClear(m_Renderer);

    // Calculate the video region size, scaling to fill the output size while
    // preserving the aspect ratio of the video stream.
    SDL_Rect src, dst;
    src.x = src.y = 0;
    src.w = frame->width;
    src.h = frame->height;
    dst.x = dst.y = 0;
    SDL_GetRendererOutputSize(m_Renderer, &dst.w, &dst.h);
    StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

    // Ensure the viewport is set to the desired video region
    SDL_RenderSetViewport(m_Renderer, &dst);

    // Draw the video content itself
    SDL_RenderCopy(m_Renderer, m_Texture, nullptr, nullptr);

    // Reset the viewport to the full window for overlay rendering
    SDL_RenderSetViewport(m_Renderer, nullptr);

    // Draw the overlays
    for (int i = 0; i < Overlay::OverlayMax; i++) {
        renderOverlay((Overlay::OverlayType)i);
    }

    SDL_RenderPresent(m_Renderer);

Exit:
    if (swFrame != nullptr) {
        av_frame_free(&swFrame);
    }
}

bool SdlRenderer::testRenderFrame(AVFrame* frame)
{
    // If we are acting as the frontend for a hardware
    // accelerated decoder, we'll need to read the frame
    // back to render it. Test that this can be done
    // for the given frame successfully.
    if (frame->hw_frames_ctx != nullptr) {
#ifdef HAVE_MMAL
        // FFmpeg for Raspberry Pi has NEON-optimized routines that allow
        // us to use av_hwframe_transfer_data() to convert from SAND frames
        // to standard fully-planar YUV. Unfortunately, the combination of
        // doing this conversion on the CPU and the slow GL texture upload
        // performance on the Pi make the performance of this approach
        // unacceptable. Don't use this copyback path on the Pi by default
        // to ensure we fall back to H.264 (with the MMAL renderer) in X11
        // rather than using HEVC+copyback and getting terrible performance.
        if (qgetenv("RPI_ALLOW_COPYBACK_RENDER") != "1") {
            return false;
        }
#endif

        AVFrame* swFrame = m_SwFrameMapper.getSwFrameFromHwFrame(frame);
        if (swFrame == nullptr) {
            return false;
        }

        av_frame_free(&swFrame);
    }
    else if (!isPixelFormatSupported(m_VideoFormat, (AVPixelFormat)frame->format)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Swframe pixel format unsupported: %d",
                    frame->format);
        return false;
    }

    return true;
}

bool SdlRenderer::notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO info)
{
    // We can transparently handle size and display changes, except Windows where
    // changing size appears to break the renderer (maybe due to the render thread?)
#ifdef Q_OS_WIN32
    return !(info->stateChangeFlags & ~(WINDOW_STATE_CHANGE_DISPLAY));
#else
    return !(info->stateChangeFlags & ~(WINDOW_STATE_CHANGE_SIZE | WINDOW_STATE_CHANGE_DISPLAY));
#endif
}
