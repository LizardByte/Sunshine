#include <Limelight.h>
#include "ffmpeg.h"
#include "streaming/session.h"

#include <h264_stream.h>

extern "C" {
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixdesc.h>
}

#include "ffmpeg-renderers/sdlvid.h"
#include "ffmpeg-renderers/genhwaccel.h"

#ifdef Q_OS_WIN32
#include "ffmpeg-renderers/dxva2.h"
#include "ffmpeg-renderers/d3d11va.h"
#endif

#ifdef Q_OS_DARWIN
#include "ffmpeg-renderers/vt.h"
#endif

#ifdef HAVE_LIBVA
#include "ffmpeg-renderers/vaapi.h"
#endif

#ifdef HAVE_LIBVDPAU
#include "ffmpeg-renderers/vdpau.h"
#endif

#ifdef HAVE_MMAL
#include "ffmpeg-renderers/mmal.h"
#endif

#ifdef HAVE_DRM
#include "ffmpeg-renderers/drm.h"
#endif

#ifdef HAVE_EGL
#include "ffmpeg-renderers/eglvid.h"
#endif

#ifdef HAVE_CUDA
#include "ffmpeg-renderers/cuda.h"
#endif

#ifdef HAVE_LIBPLACEBO_VULKAN
#include "ffmpeg-renderers/plvk.h"
#endif

// This is gross but it allows us to use sizeof()
#include "ffmpeg_videosamples.cpp"

#define MAX_DECODER_PASS 2

#define MAX_SPS_EXTRA_SIZE 16

#define FAILED_DECODES_RESET_THRESHOLD 20

// Note: This is NOT an exhaustive list of all decoders
// that Moonlight could pick. It will pick any working
// decoder that matches the codec ID and outputs one of
// the pixel formats that we have a renderer for.
static const QMap<QString, int> k_NonHwaccelCodecInfo = {
    // H.264
    {"h264_mmal", 0},
    {"h264_rkmpp", 0},
    {"h264_nvv4l2", 0},
    {"h264_nvmpi", 0},
    {"h264_v4l2m2m", 0},
    {"h264_omx", 0},

    // HEVC
    {"hevc_rkmpp", 0},
    {"hevc_nvv4l2", CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC},
    {"hevc_nvmpi", 0},
    {"hevc_v4l2m2m", 0},
    {"hevc_omx", 0},

    // AV1
};

bool FFmpegVideoDecoder::isHardwareAccelerated()
{
    return m_HwDecodeCfg != nullptr ||
            (getAVCodecCapabilities(m_VideoDecoderCtx->codec) & AV_CODEC_CAP_HARDWARE) != 0;
}

bool FFmpegVideoDecoder::isAlwaysFullScreen()
{
    return m_FrontendRenderer->getRendererAttributes() & RENDERER_ATTRIBUTE_FULLSCREEN_ONLY;
}

bool FFmpegVideoDecoder::isHdrSupported()
{
    return m_FrontendRenderer->getRendererAttributes() & RENDERER_ATTRIBUTE_HDR_SUPPORT;
}

void FFmpegVideoDecoder::setHdrMode(bool enabled)
{
    m_FrontendRenderer->setHdrMode(enabled);
}

bool FFmpegVideoDecoder::notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO info)
{
    return m_FrontendRenderer->notifyWindowChanged(info);
}

int FFmpegVideoDecoder::getDecoderCapabilities()
{
    bool ok;

    int capabilities = qEnvironmentVariableIntValue("DECODER_CAPS", &ok);
    if (ok) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Using decoder capability override: 0x%x",
                    capabilities);
    }
    else {
        // Start with the backend renderer's capabilities
        capabilities = m_BackendRenderer->getDecoderCapabilities();

        if (!isHardwareAccelerated()) {
            // Slice up to 4 times for parallel CPU decoding, once slice per core
            int slices = qMin(MAX_SLICES, SDL_GetCPUCount());
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Encoder configured for %d slices per frame",
                        slices);
            capabilities |= CAPABILITY_SLICES_PER_FRAME(slices);

            // Enable HEVC RFI when using the FFmpeg software decoder
            capabilities |= CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC;

            // Enable AV1 RFI when using the libdav1d software decoder
            capabilities |= CAPABILITY_REFERENCE_FRAME_INVALIDATION_AV1;
        }
        else if (m_HwDecodeCfg == nullptr) {
            // We have a non-hwaccel hardware decoder. This will always
            // be using SDLRenderer/DrmRenderer/PlVkRenderer so we will
            // pick decoder capabilities based on the decoder name.
            capabilities = k_NonHwaccelCodecInfo.value(m_VideoDecoderCtx->codec->name, 0);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Using capabilities table for decoder: %s -> %d",
                        m_VideoDecoderCtx->codec->name,
                        capabilities);
        }
    }

    // We use our own decoder thread with the "pull" model. This cannot
    // be overridden using the by the user because it is critical to
    // our operation.
    capabilities |= CAPABILITY_PULL_RENDERER;

    return capabilities;
}

int FFmpegVideoDecoder::getDecoderColorspace()
{
    return m_FrontendRenderer->getDecoderColorspace();
}

int FFmpegVideoDecoder::getDecoderColorRange()
{
    return m_FrontendRenderer->getDecoderColorRange();
}

QSize FFmpegVideoDecoder::getDecoderMaxResolution()
{
    if (m_BackendRenderer->getRendererAttributes() & RENDERER_ATTRIBUTE_1080P_MAX) {
        return QSize(1920, 1080);
    }
    else {
        // No known maximum
        return QSize(0, 0);
    }
}

enum AVPixelFormat FFmpegVideoDecoder::ffGetFormat(AVCodecContext* context,
                                                   const enum AVPixelFormat* pixFmts)
{
    FFmpegVideoDecoder* decoder = (FFmpegVideoDecoder*)context->opaque;
    const AVPixelFormat *p;
    AVPixelFormat desiredFmt;

    if (decoder->m_HwDecodeCfg) {
        desiredFmt = decoder->m_HwDecodeCfg->pix_fmt;
    }
    else if (decoder->m_RequiredPixelFormat != AV_PIX_FMT_NONE) {
        desiredFmt = decoder->m_RequiredPixelFormat;
    }
    else {
        desiredFmt = decoder->m_FrontendRenderer->getPreferredPixelFormat(decoder->m_VideoFormat);
    }

    for (p = pixFmts; *p != AV_PIX_FMT_NONE; p++) {
        // Only match our hardware decoding codec or preferred SW pixel
        // format (if not using hardware decoding). It's crucial
        // to override the default get_format() which will try
        // to gracefully fall back to software decode and break us.
        if (*p == desiredFmt && decoder->m_BackendRenderer->prepareDecoderContextInGetFormat(context, *p)) {
            return *p;
        }
    }

    // Failed to match the preferred pixel formats. Try non-preferred pixel format options
    // for non-hwaccel decoders if we didn't have a required pixel format to use.
    if (decoder->m_HwDecodeCfg == nullptr && decoder->m_RequiredPixelFormat == AV_PIX_FMT_NONE) {
        for (p = pixFmts; *p != AV_PIX_FMT_NONE; p++) {
            if (decoder->m_FrontendRenderer->isPixelFormatSupported(decoder->m_VideoFormat, *p) &&
                    decoder->m_BackendRenderer->prepareDecoderContextInGetFormat(context, *p)) {
                return *p;
            }
        }
    }

    return AV_PIX_FMT_NONE;
}

FFmpegVideoDecoder::FFmpegVideoDecoder(bool testOnly)
    : m_Pkt(av_packet_alloc()),
      m_VideoDecoderCtx(nullptr),
      m_RequiredPixelFormat(AV_PIX_FMT_NONE),
      m_DecodeBuffer(1024 * 1024, 0),
      m_HwDecodeCfg(nullptr),
      m_BackendRenderer(nullptr),
      m_FrontendRenderer(nullptr),
      m_ConsecutiveFailedDecodes(0),
      m_Pacer(nullptr),
      m_BwTracker(10, 250),
      m_FramesIn(0),
      m_FramesOut(0),
      m_LastFrameNumber(0),
      m_StreamFps(0),
      m_VideoFormat(0),
      m_NeedsSpsFixup(false),
      m_TestOnly(testOnly),
      m_DecoderThread(nullptr)
{
    SDL_zero(m_ActiveWndVideoStats);
    SDL_zero(m_LastWndVideoStats);
    SDL_zero(m_GlobalVideoStats);

    SDL_AtomicSet(&m_DecoderThreadShouldQuit, 0);

    // Use linear filtering when renderer scaling is required
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
}

FFmpegVideoDecoder::~FFmpegVideoDecoder()
{
    reset();

    // Set log level back to default.
    // NB: We don't do this in reset() because we want
    // to preserve the log level across reset() during
    // test initialization.
    av_log_set_level(AV_LOG_INFO);

    av_packet_free(&m_Pkt);
}

IFFmpegRenderer* FFmpegVideoDecoder::getBackendRenderer()
{
    return m_BackendRenderer;
}

void FFmpegVideoDecoder::reset()
{
    // Terminate the decoder thread before doing anything else.
    // It might be touching things we're about to free.
    if (m_DecoderThread != nullptr) {
        SDL_AtomicSet(&m_DecoderThreadShouldQuit, 1);
        LiWakeWaitForVideoFrame();
        SDL_WaitThread(m_DecoderThread, NULL);
        SDL_AtomicSet(&m_DecoderThreadShouldQuit, 0);
        m_DecoderThread = nullptr;
    }

    m_FramesIn = m_FramesOut = 0;
    m_FrameInfoQueue.clear();

    delete m_Pacer;
    m_Pacer = nullptr;

    // This must be called after deleting Pacer because it
    // may be holding AVFrames to free in its destructor.
    // However, it must be called before deleting the IFFmpegRenderer
    // since the codec context may be referencing objects that we
    // need to delete in the renderer destructor.
    avcodec_free_context(&m_VideoDecoderCtx);

    if (!m_TestOnly) {
        Session::get()->getOverlayManager().setOverlayRenderer(nullptr);
    }

    // If we have a separate frontend renderer, free that first
    if (m_FrontendRenderer != m_BackendRenderer) {
        delete m_FrontendRenderer;
    }

    delete m_BackendRenderer;

    m_FrontendRenderer = m_BackendRenderer = nullptr;

    if (!m_TestOnly) {
        logVideoStats(m_GlobalVideoStats, "Global video stats");
    }
    else {
        // Test-only decoders can't have any frames submitted
        SDL_assert(m_GlobalVideoStats.totalFrames == 0);
    }
}

bool FFmpegVideoDecoder::initializeRendererInternal(IFFmpegRenderer* renderer, PDECODER_PARAMETERS params)
{
    if (renderer->getRendererType() != IFFmpegRenderer::RendererType::Unknown &&
            m_FailedRenderers.find(renderer->getRendererType()) != m_FailedRenderers.end()) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Skipping '%s' due to prior failure",
                    renderer->getRendererName());
        return false;
    }

    if (!renderer->initialize(params)) {
        if (renderer->getInitFailureReason() == IFFmpegRenderer::InitFailureReason::NoSoftwareSupport) {
            m_FailedRenderers.insert(renderer->getRendererType());

            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "'%s' failed to initialize. It will not be tried again.",
                        renderer->getRendererName());
        }

        return false;
    }

    return true;
}

bool FFmpegVideoDecoder::createFrontendRenderer(PDECODER_PARAMETERS params, bool useAlternateFrontend)
{
    if (useAlternateFrontend) {
        if (params->videoFormat & VIDEO_FORMAT_MASK_10BIT) {
#if defined(HAVE_LIBPLACEBO_VULKAN) && !defined(VULKAN_IS_SLOW)
            // The Vulkan renderer can also handle HDR with a supported compositor. We prefer
            // rendering HDR with Vulkan if possible since it's more fully featured than DRM.
            if (m_BackendRenderer->getRendererType() != IFFmpegRenderer::RendererType::Vulkan) {
                m_FrontendRenderer = new PlVkRenderer(false, m_BackendRenderer);
                if (initializeRendererInternal(m_FrontendRenderer, params) && (m_FrontendRenderer->getRendererAttributes() & RENDERER_ATTRIBUTE_HDR_SUPPORT)) {
                    return true;
                }
                delete m_FrontendRenderer;
                m_FrontendRenderer = nullptr;
            }
#endif

#ifdef HAVE_DRM
            // If we're trying to stream HDR, we need to use the DRM renderer in direct
            // rendering mode so it can set the HDR metadata on the display. EGL does
            // not currently support this (and even if it did, Mesa and Wayland don't
            // currently have protocols to actually get that metadata to the display).
            if (m_BackendRenderer->canExportDrmPrime()) {
                m_FrontendRenderer = new DrmRenderer(AV_HWDEVICE_TYPE_NONE, m_BackendRenderer);
                if (initializeRendererInternal(m_FrontendRenderer, params) && (m_FrontendRenderer->getRendererAttributes() & RENDERER_ATTRIBUTE_HDR_SUPPORT)) {
                    return true;
                }
                delete m_FrontendRenderer;
                m_FrontendRenderer = nullptr;
            }
#endif

#if defined(HAVE_LIBPLACEBO_VULKAN) && defined(VULKAN_IS_SLOW)
            if (m_BackendRenderer->getRendererType() != IFFmpegRenderer::RendererType::Vulkan) {
                m_FrontendRenderer = new PlVkRenderer(false, m_BackendRenderer);
                if (initializeRendererInternal(m_FrontendRenderer, params) && (m_FrontendRenderer->getRendererAttributes() & RENDERER_ATTRIBUTE_HDR_SUPPORT)) {
                    return true;
                }
                delete m_FrontendRenderer;
                m_FrontendRenderer = nullptr;
            }
#endif
        }
        else
        {
#ifdef HAVE_LIBPLACEBO_VULKAN
            if (qgetenv("PREFER_VULKAN") == "1") {
                if (m_BackendRenderer->getRendererType() != IFFmpegRenderer::RendererType::Vulkan) {
                    m_FrontendRenderer = new PlVkRenderer(false, m_BackendRenderer);
                    if (initializeRendererInternal(m_FrontendRenderer, params)) {
                        return true;
                    }
                    delete m_FrontendRenderer;
                    m_FrontendRenderer = nullptr;
                }
            }
#endif
        }

#if defined(HAVE_EGL) && !defined(GL_IS_SLOW)
        if (m_BackendRenderer->canExportEGL()) {
            m_FrontendRenderer = new EGLRenderer(m_BackendRenderer);
            if (initializeRendererInternal(m_FrontendRenderer, params)) {
                return true;
            }
            delete m_FrontendRenderer;
            m_FrontendRenderer = nullptr;
        }
#endif

        // If we made it here, we failed to create the EGLRenderer
        return false;
    }

    if (m_BackendRenderer->isDirectRenderingSupported()) {
        // The backend renderer can render to the display
        m_FrontendRenderer = m_BackendRenderer;
    }
    else {
        // The backend renderer cannot directly render to the display, so
        // we will create an SDL or DRM renderer to draw the frames.

#if (defined(VULKAN_IS_SLOW) || defined(GL_IS_SLOW)) && defined(HAVE_DRM)
        // Try DrmRenderer first if we have a slow GPU
        m_FrontendRenderer = new DrmRenderer(AV_HWDEVICE_TYPE_NONE, m_BackendRenderer);
        if (initializeRendererInternal(m_FrontendRenderer, params)) {
            return true;
        }
        delete m_FrontendRenderer;
        m_FrontendRenderer = nullptr;
#endif


#if defined(GL_IS_SLOW) && defined(HAVE_EGL)
        // We explicitly skipped EGL in the GL_IS_SLOW case above.
        // If DRM didn't work either, try EGL now.
        if (m_BackendRenderer->canExportEGL()) {
            m_FrontendRenderer = new EGLRenderer(m_BackendRenderer);
            if (initializeRendererInternal(m_FrontendRenderer, params)) {
                return true;
            }
            delete m_FrontendRenderer;
            m_FrontendRenderer = nullptr;
        }
#endif

#if defined(HAVE_LIBPLACEBO_VULKAN) && defined(VULKAN_IS_SLOW)
        m_FrontendRenderer = new PlVkRenderer(false, m_BackendRenderer);
        if (initializeRendererInternal(m_FrontendRenderer, params)) {
            return true;
        }
        delete m_FrontendRenderer;
        m_FrontendRenderer = nullptr;
#endif

        m_FrontendRenderer = new SdlRenderer();
        if (!initializeRendererInternal(m_FrontendRenderer, params)) {
            return false;
        }
    }

    return true;
}

bool FFmpegVideoDecoder::completeInitialization(const AVCodec* decoder, enum AVPixelFormat requiredFormat, PDECODER_PARAMETERS params, bool testFrame, bool useAlternateFrontend)
{
    // In test-only mode, we should only see test frames
    SDL_assert(!m_TestOnly || testFrame);

    // Create the frontend renderer based on the capabilities of the backend renderer
    if (!createFrontendRenderer(params, useAlternateFrontend)) {
        return false;
    }

    m_RequiredPixelFormat = requiredFormat;
    m_StreamFps = params->frameRate;
    m_VideoFormat = params->videoFormat;

    // Don't bother initializing Pacer if we're not actually going to render
    if (!testFrame) {
        m_Pacer = new Pacer(m_FrontendRenderer, &m_ActiveWndVideoStats);
        if (!m_Pacer->initialize(params->window, params->frameRate,
                                 params->enableFramePacing || (params->enableVsync && (m_FrontendRenderer->getRendererAttributes() & RENDERER_ATTRIBUTE_FORCE_PACING)))) {
            return false;
        }
    }

    m_VideoDecoderCtx = avcodec_alloc_context3(decoder);
    if (!m_VideoDecoderCtx) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to allocate video decoder context");
        return false;
    }

    // Always request low delay decoding
    m_VideoDecoderCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;

    // Allow display of corrupt frames and frames missing references
    m_VideoDecoderCtx->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
    m_VideoDecoderCtx->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;

    // Report decoding errors to allow us to request a key frame
    //
    // With HEVC streams, FFmpeg can drop a frame (hwaccel->start_frame() fails)
    // without telling us. Since we have an infinite GOP length, this causes artifacts
    // on screen that persist for a long time. It's easy to cause this condition
    // by using NVDEC and delaying 100 ms randomly in the render path so the decoder
    // runs out of output buffers.
    m_VideoDecoderCtx->err_recognition = AV_EF_EXPLODE;

    // Enable slice multi-threading for software decoding
    if (!isHardwareAccelerated()) {
        m_VideoDecoderCtx->thread_type = FF_THREAD_SLICE;
        m_VideoDecoderCtx->thread_count = qMin(MAX_SLICES, SDL_GetCPUCount());
    }
    else {
        // No threading for HW decode
        m_VideoDecoderCtx->thread_count = 1;
    }

    // Setup decoding parameters
    m_VideoDecoderCtx->width = params->width;
    m_VideoDecoderCtx->height = params->height;
    m_VideoDecoderCtx->get_format = ffGetFormat;
    m_VideoDecoderCtx->pkt_timebase.num = 1;
    m_VideoDecoderCtx->pkt_timebase.den = 90000;

    // For non-hwaccel decoders, set the pix_fmt to hint to the decoder which
    // format should be used. This is necessary for certain decoders like the
    // out-of-tree nvv4l2dec decoders for L4T platforms. We do not do this
    // for hwaccel decoders because it causes the AV1 Vulkan video decoder in
    // FFmpeg 7.0-8.0 to incorrectly believe ff_get_format() was called.
    // See #1511.
    if (m_HwDecodeCfg == nullptr) {
        m_VideoDecoderCtx->pix_fmt = (requiredFormat != AV_PIX_FMT_NONE) ?
            requiredFormat : m_FrontendRenderer->getPreferredPixelFormat(params->videoFormat);
    }

    AVDictionary* options = nullptr;

    // Allow the backend renderer to attach data to this decoder
    if (!m_BackendRenderer->prepareDecoderContext(m_VideoDecoderCtx, &options)) {
        return false;
    }

    // Nobody must override our ffGetFormat
    SDL_assert(m_VideoDecoderCtx->get_format == ffGetFormat);

    // Stash a pointer to this object in the context
    SDL_assert(m_VideoDecoderCtx->opaque == nullptr);
    m_VideoDecoderCtx->opaque = this;

    int err = avcodec_open2(m_VideoDecoderCtx, decoder, &options);
    av_dict_free(&options);
    if (err < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to open decoder for format: %x",
                     params->videoFormat);
        return false;
    }

    // FFMpeg doesn't completely initialize the codec until the codec
    // config data comes in. This would be too late for us to change
    // our minds on the selected video codec, so we'll do a trial run
    // now to see if things will actually work when the video stream
    // comes in.
    if (testFrame) {
        switch (params->videoFormat) {
        case VIDEO_FORMAT_H264:
            m_Pkt->data = (uint8_t*)k_H264TestFrame;
            m_Pkt->size = sizeof(k_H264TestFrame);
            break;
        case VIDEO_FORMAT_H265:
            m_Pkt->data = (uint8_t*)k_HEVCMainTestFrame;
            m_Pkt->size = sizeof(k_HEVCMainTestFrame);
            break;
        case VIDEO_FORMAT_H265_MAIN10:
            m_Pkt->data = (uint8_t*)k_HEVCMain10TestFrame;
            m_Pkt->size = sizeof(k_HEVCMain10TestFrame);
            break;
        case VIDEO_FORMAT_AV1_MAIN8:
            m_Pkt->data = (uint8_t*)k_AV1Main8TestFrame;
            m_Pkt->size = sizeof(k_AV1Main8TestFrame);
            break;
        case VIDEO_FORMAT_AV1_MAIN10:
            m_Pkt->data = (uint8_t*)k_AV1Main10TestFrame;
            m_Pkt->size = sizeof(k_AV1Main10TestFrame);
            break;
        case VIDEO_FORMAT_H264_HIGH8_444:
            m_Pkt->data = (uint8_t*)k_h264High_444TestFrame;
            m_Pkt->size = sizeof(k_h264High_444TestFrame);
            break;
        case VIDEO_FORMAT_H265_REXT8_444:
            m_Pkt->data = (uint8_t*)k_HEVCRExt8_444TestFrame;
            m_Pkt->size = sizeof(k_HEVCRExt8_444TestFrame);
            break;
        case VIDEO_FORMAT_H265_REXT10_444:
            m_Pkt->data = (uint8_t*)k_HEVCRExt10_444TestFrame;
            m_Pkt->size = sizeof(k_HEVCRExt10_444TestFrame);
            break;
        case VIDEO_FORMAT_AV1_HIGH8_444:
            m_Pkt->data = (uint8_t*)k_AV1High8_444TestFrame;
            m_Pkt->size = sizeof(k_AV1High8_444TestFrame);
            break;
        case VIDEO_FORMAT_AV1_HIGH10_444:
            m_Pkt->data = (uint8_t*)k_AV1High10_444TestFrame;
            m_Pkt->size = sizeof(k_AV1High10_444TestFrame);
            break;
        default:
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "No test frame for format: %x",
                         params->videoFormat);
            return false;
        }

        AVFrame* frame = av_frame_alloc();
        if (!frame) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to allocate frame");
            return false;
        }

        // Some decoders won't output on the first frame, so we'll submit
        // a few test frames if we get an EAGAIN error.
        for (int retries = 0; retries < 5; retries++) {
            // Most FFmpeg decoders process input using a "push" model.
            // We'll see those fail here if the format is not supported.
            err = avcodec_send_packet(m_VideoDecoderCtx, m_Pkt);
            if (err < 0) {
                av_frame_free(&frame);
                char errorstring[512];
                av_strerror(err, errorstring, sizeof(errorstring));
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Test decode failed (avcodec_send_packet): %s", errorstring);
                return false;
            }

            // A few FFmpeg decoders (h264_mmal) process here using a "pull" model.
            // Those decoders will fail here if the format is not supported.
            err = avcodec_receive_frame(m_VideoDecoderCtx, frame);
            if (err == AVERROR(EAGAIN)) {
                // Wait a little while to let the hardware work
                SDL_Delay(100);
            }
            else {
                // Done!
                break;
            }
        }

        if (err < 0) {
            char errorstring[512];
            av_strerror(err, errorstring, sizeof(errorstring));
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Test decode failed (avcodec_receive_frame): %s", errorstring);
            av_frame_free(&frame);
            return false;
        }

        // Allow the renderer to do any validation it wants on this frame
        if (!m_FrontendRenderer->testRenderFrame(frame)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Test decode failed (testRenderFrame)");
            av_frame_free(&frame);
            return false;
        }

        av_frame_free(&frame);
    }
    else {
        if ((params->videoFormat & VIDEO_FORMAT_MASK_H264) &&
                !(m_BackendRenderer->getDecoderCapabilities() & CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC)) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Using H.264 SPS fixup");
            m_NeedsSpsFixup = true;
        }
        else {
            m_NeedsSpsFixup = false;
        }

        // Tell overlay manager to use this frontend renderer
        Session::get()->getOverlayManager().setOverlayRenderer(m_FrontendRenderer);

        // Allow the renderer to perform final preparations for rendering
        m_FrontendRenderer->prepareToRender();

        // Only create the decoder thread when instantiating the decoder for real. It will use APIs from
        // moonlight-common-c that can only be legally called with an established connection.
        m_DecoderThread = SDL_CreateThread(FFmpegVideoDecoder::decoderThreadProcThunk, "FFDecoder", (void*)this);
        if (m_DecoderThread == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to create decoder thread: %s", SDL_GetError());
            return false;
        }

        if (m_FrontendRenderer->getRendererType() != m_BackendRenderer->getRendererType()) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Renderer '%s' with '%s' backend chosen",
                        m_FrontendRenderer->getRendererName(),
                        m_BackendRenderer->getRendererName());
        }
        else {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Renderer '%s' chosen",
                        m_FrontendRenderer->getRendererName());
        }
    }

    return true;
}

void FFmpegVideoDecoder::addVideoStats(VIDEO_STATS& src, VIDEO_STATS& dst)
{
    dst.receivedFrames += src.receivedFrames;
    dst.decodedFrames += src.decodedFrames;
    dst.renderedFrames += src.renderedFrames;
    dst.totalFrames += src.totalFrames;
    dst.networkDroppedFrames += src.networkDroppedFrames;
    dst.pacerDroppedFrames += src.pacerDroppedFrames;
    dst.totalReassemblyTimeUs += src.totalReassemblyTimeUs;
    dst.totalDecodeTimeUs += src.totalDecodeTimeUs;
    dst.totalPacerTimeUs += src.totalPacerTimeUs;
    dst.totalRenderTimeUs += src.totalRenderTimeUs;

    if (dst.minHostProcessingLatency == 0) {
        dst.minHostProcessingLatency = src.minHostProcessingLatency;
    }
    else if (src.minHostProcessingLatency != 0) {
        dst.minHostProcessingLatency = qMin(dst.minHostProcessingLatency, src.minHostProcessingLatency);
    }
    dst.maxHostProcessingLatency = qMax(dst.maxHostProcessingLatency, src.maxHostProcessingLatency);
    dst.totalHostProcessingLatency += src.totalHostProcessingLatency;
    dst.framesWithHostProcessingLatency += src.framesWithHostProcessingLatency;

    if (!LiGetEstimatedRttInfo(&dst.lastRtt, &dst.lastRttVariance)) {
        dst.lastRtt = 0;
        dst.lastRttVariance = 0;
    }
    else {
        // Our logic to determine if RTT is valid depends on us never
        // getting an RTT of 0. ENet currently ensures RTTs are >= 1.
        SDL_assert(dst.lastRtt > 0);
    }

    // Initialize the measurement start point if this is the first video stat window
    if (!dst.measurementStartUs) {
        dst.measurementStartUs = src.measurementStartUs;
    }

    // The following code assumes the global measure was already started first
    SDL_assert(dst.measurementStartUs <= src.measurementStartUs);

    double timeDiffSecs = (double)(LiGetMicroseconds() - dst.measurementStartUs) / 1000000.0;
    dst.totalFps        = (double)dst.totalFrames / timeDiffSecs;
    dst.receivedFps     = (double)dst.receivedFrames / timeDiffSecs;
    dst.decodedFps      = (double)dst.decodedFrames / timeDiffSecs;
    dst.renderedFps     = (double)dst.renderedFrames / timeDiffSecs;
}

void FFmpegVideoDecoder::stringifyVideoStats(VIDEO_STATS& stats, char* output, int length)
{
    int offset = 0;
    const char* codecString;
    int ret;

    // Start with an empty string
    output[offset] = 0;

    switch (m_VideoFormat)
    {
    case VIDEO_FORMAT_H264:
        codecString = "H.264";
        break;

    case VIDEO_FORMAT_H264_HIGH8_444:
        codecString = "H.264 4:4:4";
        break;

    case VIDEO_FORMAT_H265:
        codecString = "HEVC";
        break;

    case VIDEO_FORMAT_H265_REXT8_444:
        codecString = "HEVC 4:4:4";
        break;

    case VIDEO_FORMAT_H265_MAIN10:
        if (LiGetCurrentHostDisplayHdrMode()) {
            codecString = "HEVC 10-bit HDR";
        }
        else {
            codecString = "HEVC 10-bit SDR";
        }
        break;

    case VIDEO_FORMAT_H265_REXT10_444:
        if (LiGetCurrentHostDisplayHdrMode()) {
            codecString = "HEVC 10-bit HDR 4:4:4";
        }
        else {
            codecString = "HEVC 10-bit SDR 4:4:4";
        }
        break;

    case VIDEO_FORMAT_AV1_MAIN8:
        codecString = "AV1";
        break;

    case VIDEO_FORMAT_AV1_HIGH8_444:
        codecString = "AV1 4:4:4";
        break;

    case VIDEO_FORMAT_AV1_MAIN10:
        if (LiGetCurrentHostDisplayHdrMode()) {
            codecString = "AV1 10-bit HDR";
        }
        else {
            codecString = "AV1 10-bit SDR";
        }
        break;

    case VIDEO_FORMAT_AV1_HIGH10_444:
        if (LiGetCurrentHostDisplayHdrMode()) {
            codecString = "AV1 10-bit HDR 4:4:4";
        }
        else {
            codecString = "AV1 10-bit SDR 4:4:4";
        }
        break;

    default:
        SDL_assert(false);
        codecString = "UNKNOWN";
        break;
    }

    if (stats.receivedFps > 0) {
        if (m_VideoDecoderCtx != nullptr) {
            double avgVideoMbps = m_BwTracker.GetAverageMbps();
            double peakVideoMbps = m_BwTracker.GetPeakMbps();

            ret = snprintf(&output[offset],
                           length - offset,
                           "Video stream: %dx%d %.2f FPS (Codec: %s)\n"
                           "Bitrate: %.1f Mbps, Peak (%us): %.1f\n",
                           m_VideoDecoderCtx->width,
                           m_VideoDecoderCtx->height,
                           stats.totalFps,
                           codecString,
                           avgVideoMbps,
                           m_BwTracker.GetWindowSeconds(),
                           peakVideoMbps);
            if (ret < 0 || ret >= length - offset) {
                SDL_assert(false);
                return;
            }

            offset += ret;
        }

        ret = snprintf(&output[offset],
                       length - offset,
                       "Incoming frame rate from network: %.2f FPS\n"
                       "Decoding frame rate: %.2f FPS\n"
                       "Rendering frame rate: %.2f FPS\n",
                       stats.receivedFps,
                       stats.decodedFps,
                       stats.renderedFps);
        if (ret < 0 || ret >= length - offset) {
            SDL_assert(false);
            return;
        }

        offset += ret;
    }

    if (stats.framesWithHostProcessingLatency > 0) {
        ret = snprintf(&output[offset],
                       length - offset,
                       "Host processing latency min/max/average: %.1f/%.1f/%.1f ms\n",
                       (float)stats.minHostProcessingLatency / 10,
                       (float)stats.maxHostProcessingLatency / 10,
                       (float)stats.totalHostProcessingLatency / 10 / stats.framesWithHostProcessingLatency);
        if (ret < 0 || ret >= length - offset) {
            SDL_assert(false);
            return;
        }

        offset += ret;
    }

    if (stats.renderedFrames != 0) {
        char rttString[32];

        if (stats.lastRtt != 0) {
            snprintf(rttString, sizeof(rttString), "%u ms (variance: %u ms)", stats.lastRtt, stats.lastRttVariance);
        }
        else {
            snprintf(rttString, sizeof(rttString), "N/A");
        }

        ret = snprintf(&output[offset],
                       length - offset,
                       "Frames dropped by your network connection: %.2f%%\n"
                       "Frames dropped due to network jitter: %.2f%%\n"
                       "Average network latency: %s\n"
                       "Average decoding time: %.2f ms\n"
                       "Average frame queue delay: %.2f ms\n"
                       "Average rendering time (including monitor V-sync latency): %.2f ms\n",
                       (float)stats.networkDroppedFrames / stats.totalFrames * 100,
                       (float)stats.pacerDroppedFrames / stats.decodedFrames * 100,
                       rttString,
                       (double)(stats.totalDecodeTimeUs / 1000.0) / stats.decodedFrames,
                       (double)(stats.totalPacerTimeUs / 1000.0) / stats.renderedFrames,
                       (double)(stats.totalRenderTimeUs / 1000.0) / stats.renderedFrames);
        if (ret < 0 || ret >= length - offset) {
            SDL_assert(false);
            return;
        }

        offset += ret;
    }
}

void FFmpegVideoDecoder::logVideoStats(VIDEO_STATS& stats, const char* title)
{
    if (stats.renderedFps > 0 || stats.renderedFrames != 0) {
        char videoStatsStr[512];
        stringifyVideoStats(stats, videoStatsStr, sizeof(videoStatsStr));

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "\n%s\n------------------\n%s",
                    title, videoStatsStr);
    }
}

IFFmpegRenderer* FFmpegVideoDecoder::createHwAccelRenderer(const AVCodecHWConfig* hwDecodeCfg, int pass)
{
    if (!(hwDecodeCfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
        return nullptr;
    }

    // First pass using our top-tier hwaccel implementations
    if (pass == 0) {
        switch (hwDecodeCfg->device_type) {
#ifdef Q_OS_WIN32
        // DXVA2 appears in the hwaccel list before D3D11VA, so we only check for D3D11VA
        // on the first pass to ensure we prefer D3D11VA over DXVA2.
        case AV_HWDEVICE_TYPE_D3D11VA:
            return new D3D11VARenderer(pass);
#endif
#ifdef Q_OS_DARWIN
        case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:
            // Prefer the Metal renderer if hardware is compatible
            return VTMetalRendererFactory::createRenderer(true);
#endif
#ifdef HAVE_LIBVA
        case AV_HWDEVICE_TYPE_VAAPI:
            return new VAAPIRenderer(pass);
#endif
#ifdef HAVE_LIBVDPAU
        case AV_HWDEVICE_TYPE_VDPAU:
            return new VDPAURenderer(pass);
#endif
#ifdef HAVE_DRM
        case AV_HWDEVICE_TYPE_DRM:
            return new DrmRenderer(hwDecodeCfg->device_type);
#endif
#ifdef HAVE_LIBPLACEBO_VULKAN
        case AV_HWDEVICE_TYPE_VULKAN:
            return new PlVkRenderer(true);
#endif
        default:
            switch (hwDecodeCfg->pix_fmt) {
#ifdef HAVE_DRM
            case AV_PIX_FMT_DRM_PRIME:
                // Support out-of-tree non-DRM hwaccels that output DRM_PRIME frames
                // https://patchwork.ffmpeg.org/project/ffmpeg/list/?series=12604
                return new DrmRenderer(hwDecodeCfg->device_type);
#endif
            default:
                return nullptr;
            }
        }
    }
    // Second pass for our second-tier hwaccel implementations
    else if (pass == 1) {
        switch (hwDecodeCfg->device_type) {
#ifdef HAVE_CUDA
        case AV_HWDEVICE_TYPE_CUDA:
            // CUDA should only be used to cover the NVIDIA+Wayland case
            return new CUDARenderer();
#endif
#ifdef Q_OS_WIN32
        // This gives us another shot if D3D11VA failed in the first pass.
        // Since DXVA2 is in the hwaccel list first, we'll first try to fall back
        // to that before giving D3D11VA another try as a last resort.
        case AV_HWDEVICE_TYPE_DXVA2:
            return new DXVA2Renderer(pass);
        case AV_HWDEVICE_TYPE_D3D11VA:
            return new D3D11VARenderer(pass);
#endif
#ifdef Q_OS_DARWIN
        case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:
            // Use the older AVSampleBufferDisplayLayer if Metal cannot be used
            return VTRendererFactory::createRenderer();
#endif
#ifdef HAVE_LIBVA
        case AV_HWDEVICE_TYPE_VAAPI:
            return new VAAPIRenderer(pass);
#endif
#ifdef HAVE_LIBVDPAU
        case AV_HWDEVICE_TYPE_VDPAU:
            return new VDPAURenderer(pass);
#endif
        default:
            return nullptr;
        }
    }
    // Third pass for the generic hwaccel backend if we didn't have a specific renderer for
    // any supported hwaccel device type exposed by this decoder.
    else if (pass == 2) {
        switch (hwDecodeCfg->device_type) {
        case AV_HWDEVICE_TYPE_VDPAU:
        case AV_HWDEVICE_TYPE_CUDA:
        case AV_HWDEVICE_TYPE_VAAPI:
        case AV_HWDEVICE_TYPE_DXVA2:
        case AV_HWDEVICE_TYPE_QSV: // Covered by VAAPI and D3D11VA/DXVA2
        case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:
        case AV_HWDEVICE_TYPE_D3D11VA:
        case AV_HWDEVICE_TYPE_DRM:
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(56, 39, 100)
        case AV_HWDEVICE_TYPE_VULKAN:
#endif
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 36, 100)
        case AV_HWDEVICE_TYPE_D3D12VA: // Covered by D3D11VA
#endif
            // If we have a specific renderer for this hwaccel device type, never allow it to fall back
            // to the GenericHwAccelRenderer.
            //
            // If we reach this path for a known device type that we support above, it means either:
            // a) The build was missing core hwaccel libraries and we should break loudly in that case.
            // b) The renderer rejected the device for some reason and we shouldn't second guess it.
            return nullptr;

        default:
            return new GenericHwAccelRenderer(hwDecodeCfg->device_type);
        }
    }
    else {
        SDL_assert(false);
        return nullptr;
    }
}

bool FFmpegVideoDecoder::tryInitializeRenderer(const AVCodec* decoder,
                                               enum AVPixelFormat requiredFormat,
                                               PDECODER_PARAMETERS params,
                                               const AVCodecHWConfig* hwConfig,
                                               IFFmpegRenderer::InitFailureReason* failureReason, // Out - Optional
                                               std::function<IFFmpegRenderer*()> createRendererFunc)
{
    DECODER_PARAMETERS testFrameDecoderParams = *params;

    // Setup the test decoder parameters using the dimensions for the test frame. These are
    // used to populate the AVCodecContext fields of the same names.
    //
    // While most decoders don't care what dimensions we specify here, V4L2M2M seems to puke
    // if we pass whatever the native stream resolution is then decode a 720p test frame.
    //
    // For qcom-venus, it seems to lead to failures allocating capture buffers (bug #1042).
    // For wave5 (VisionFive), it leads to an invalid pitch error when calling drmModeAddFB2().
    testFrameDecoderParams.width = 1280;
    testFrameDecoderParams.height = 720;

    m_HwDecodeCfg = hwConfig;

    if (failureReason != nullptr) {
        *failureReason = IFFmpegRenderer::InitFailureReason::Unknown;
    }

    // i == 0 - Indirect via EGL or DRM frontend with zero-copy DMA-BUF passing
    // i == 1 - Direct rendering or indirect via SDL read-back
    bool backendInitFailure = false;
#ifdef HAVE_EGL
    for (int i = 0; i < 2 && !backendInitFailure; i++) {
#else
    for (int i = 1; i < 2 && !backendInitFailure; i++) {
#endif
        SDL_assert(m_BackendRenderer == nullptr);

        if ((m_BackendRenderer = createRendererFunc()) == nullptr) {
            // Out of memory
            break;
        }

        // Initialize the backend renderer itself
        if (initializeRendererInternal(m_BackendRenderer, (m_TestOnly || m_BackendRenderer->needsTestFrame()) ? &testFrameDecoderParams : params)) {
            if (completeInitialization(decoder, requiredFormat,
                                       (m_TestOnly || m_BackendRenderer->needsTestFrame()) ? &testFrameDecoderParams : params,
                                       m_TestOnly || m_BackendRenderer->needsTestFrame(),
                                       i == 0 /* EGL/DRM */)) {
                if (m_TestOnly) {
                    // This decoder is only for testing capabilities, so don't bother
                    // creating a usable renderer
                    return true;
                }

                if (m_BackendRenderer->needsTestFrame()) {
                    // The test worked, so now let's initialize it for real
                    reset();

                    if ((m_BackendRenderer = createRendererFunc()) == nullptr) {
                        // Out of memory
                        break;
                    }

                    if (initializeRendererInternal(m_BackendRenderer, params) &&
                        completeInitialization(decoder, requiredFormat, params, false, i == 0 /* EGL/DRM */)) {
                        return true;
                    }
                    else {
                        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                                        "Decoder failed to initialize after successful test");
                    }
                }
                else {
                    // No test required. Good to go now.
                    return true;
                }
            }
        }
        else {
            // If we failed to initialize the backend entirely, there's no sense in trying
            // a different frontend renderer as it won't make a difference.
            backendInitFailure = true;
        }

        auto backendFailureReason = m_BackendRenderer->getInitFailureReason();

        if (failureReason != nullptr) {
            *failureReason = backendFailureReason;
        }

        reset();
    }

    // reset() must be called before we reach this point!
    SDL_assert(m_BackendRenderer == nullptr);
    return false;
}

#define TRY_PREFERRED_PIXEL_FORMAT(RENDERER_TYPE) \
    { \
        RENDERER_TYPE renderer; \
        if (renderer.getPreferredPixelFormat(params->videoFormat) == decoder_pix_fmts[i]) { \
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, \
                        "Trying " #RENDERER_TYPE " for codec %s due to preferred pixel format: 0x%x", \
                        decoder->name, decoder_pix_fmts[i]); \
            if (tryInitializeRenderer(decoder, decoder_pix_fmts[i], params, nullptr, nullptr, \
                                      []() -> IFFmpegRenderer* { return new RENDERER_TYPE(); })) { \
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, \
                            "Chose " #RENDERER_TYPE " for codec %s due to preferred pixel format: 0x%x", \
                            decoder->name, decoder_pix_fmts[i]); \
                return true; \
            } \
        } \
    }

#define TRY_SUPPORTED_NON_PREFERRED_PIXEL_FORMAT(RENDERER_TYPE) \
    { \
        RENDERER_TYPE renderer; \
        if (decoder_pix_fmts[i] != renderer.getPreferredPixelFormat(params->videoFormat) && \
            renderer.isPixelFormatSupported(params->videoFormat, decoder_pix_fmts[i])) { \
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, \
                        "Trying " #RENDERER_TYPE " for codec %s due to compatible pixel format: 0x%x", \
                        decoder->name, decoder_pix_fmts[i]); \
            if (tryInitializeRenderer(decoder, decoder_pix_fmts[i], params, nullptr, nullptr, \
                                      []() -> IFFmpegRenderer* { return new RENDERER_TYPE(); })) { \
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, \
                            "Chose " #RENDERER_TYPE " for codec %s due to compatible pixel format: 0x%x", \
                            decoder->name, decoder_pix_fmts[i]); \
                return true; \
            } \
        } \
    }

bool FFmpegVideoDecoder::tryInitializeRendererForUnknownDecoder(const AVCodec* decoder,
                                                                PDECODER_PARAMETERS params,
                                                                bool tryHwAccel)
{
    if (!decoder) {
        return false;
    }

    const AVPixelFormat* decoder_pix_fmts;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    if (avcodec_get_supported_config(nullptr, decoder, AV_CODEC_CONFIG_PIX_FORMAT, 0,
                                     (const void**)&decoder_pix_fmts, nullptr) < 0) {
        decoder_pix_fmts = nullptr;
    }
#else
    decoder_pix_fmts = decoder->pix_fmts;
#endif

    // This might be a hwaccel decoder, so try any hw configs first
    if (tryHwAccel) {
        for (int pass = 0; pass <= MAX_DECODER_PASS; pass++) {
            for (int i = 0;; i++) {
                const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
                if (!config) {
                    // No remaining hwaccel options
                    break;
                }

                // Initialize the hardware codec and submit a test frame if the renderer needs it
                IFFmpegRenderer::InitFailureReason failureReason;
                if (tryInitializeRenderer(decoder, AV_PIX_FMT_NONE, params, config, &failureReason,
                                          [config, pass]() -> IFFmpegRenderer* { return createHwAccelRenderer(config, pass); })) {
                    return true;
                }
                else if (failureReason == IFFmpegRenderer::InitFailureReason::NoHardwareSupport) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "Skipping remaining hwaccels due lack of hardware support for specified codec");
                    return false;
                }
            }
        }
    }

    if (decoder_pix_fmts == NULL) {
        // Supported output pixel formats are unknown. We'll just try DRM/SDL and hope it can cope.

#if defined(HAVE_DRM) && defined(GL_IS_SLOW)
        if (tryInitializeRenderer(decoder, AV_PIX_FMT_NONE, params, nullptr, nullptr,
                                  []() -> IFFmpegRenderer* { return new DrmRenderer(); })) {
            return true;
        }
#endif

#if defined(HAVE_LIBPLACEBO_VULKAN) && !defined(VULKAN_IS_SLOW)
        if (tryInitializeRenderer(decoder, AV_PIX_FMT_NONE, params, nullptr, nullptr,
                                  []() -> IFFmpegRenderer* { return new PlVkRenderer(); })) {
            return true;
        }
#endif

#ifdef Q_OS_DARWIN
        if (tryInitializeRenderer(decoder, AV_PIX_FMT_NONE, params, nullptr, nullptr,
                                  []() -> IFFmpegRenderer* { return VTMetalRendererFactory::createRenderer(false); })) {
            return true;
        }
#endif

        if (tryInitializeRenderer(decoder, AV_PIX_FMT_NONE, params, nullptr, nullptr,
                                  []() -> IFFmpegRenderer* { return new SdlRenderer(); })) {
            return true;
        }

        return false;
    }

    // HACK: Avoid using YUV420P on h264_mmal. It can cause a deadlock inside the MMAL libraries.
    // Even if it didn't completely deadlock us, the performance would likely be atrocious.
    if (strcmp(decoder->name, "h264_mmal") == 0) {
#ifdef HAVE_MMAL
        for (int i = 0; decoder_pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
            TRY_PREFERRED_PIXEL_FORMAT(MmalRenderer);
        }

        for (int i = 0; decoder_pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
            TRY_SUPPORTED_NON_PREFERRED_PIXEL_FORMAT(MmalRenderer);
        }
#endif

        // Give up if we can't use MmalRenderer for h264_mmal
        return false;
    }

    // Check if any of our decoders prefer any of the pixel formats first
    for (int i = 0; decoder_pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
#ifdef HAVE_DRM
        TRY_PREFERRED_PIXEL_FORMAT(DrmRenderer);
#endif
#if defined(HAVE_LIBPLACEBO_VULKAN) && !defined(VULKAN_IS_SLOW)
        TRY_PREFERRED_PIXEL_FORMAT(PlVkRenderer);
#endif
#ifndef GL_IS_SLOW
        TRY_PREFERRED_PIXEL_FORMAT(SdlRenderer);
#endif
    }

    // Nothing prefers any of them. Let's see if anyone will tolerate one.
    for (int i = 0; decoder_pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
#ifdef HAVE_DRM
        TRY_SUPPORTED_NON_PREFERRED_PIXEL_FORMAT(DrmRenderer);
#endif
#if defined(HAVE_LIBPLACEBO_VULKAN) && !defined(VULKAN_IS_SLOW)
        TRY_SUPPORTED_NON_PREFERRED_PIXEL_FORMAT(PlVkRenderer);
#endif
#ifndef GL_IS_SLOW
        TRY_SUPPORTED_NON_PREFERRED_PIXEL_FORMAT(SdlRenderer);
#endif
    }

#if defined(HAVE_LIBPLACEBO_VULKAN) && defined(VULKAN_IS_SLOW)
    // If we got here with VULKAN_IS_SLOW, DrmRenderer didn't work,
    // so we have to resort to PlVkRenderer.
    for (int i = 0; decoder_pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
        TRY_PREFERRED_PIXEL_FORMAT(PlVkRenderer);
    }
    for (int i = 0; decoder_pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
        TRY_SUPPORTED_NON_PREFERRED_PIXEL_FORMAT(PlVkRenderer);
    }
#endif

#ifdef GL_IS_SLOW
    // If we got here with GL_IS_SLOW, DrmRenderer didn't work, so we have
    // to resort to SdlRenderer.
    for (int i = 0; decoder_pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
        TRY_PREFERRED_PIXEL_FORMAT(SdlRenderer);
    }
    for (int i = 0; decoder_pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
        TRY_SUPPORTED_NON_PREFERRED_PIXEL_FORMAT(SdlRenderer);
    }
#endif

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "No renderer can handle output from decoder: %s",
                decoder->name);

    // If we made it here, we couldn't find anything
    return false;
}

int FFmpegVideoDecoder::getAVCodecCapabilities(const AVCodec *codec)
{
    int caps = codec->capabilities;

    // There are a bunch of out-of-tree OMX decoder implementations
    // from various SBC manufacturers that all seem to forget to set
    // AV_CODEC_CAP_HARDWARE (probably because the upstream OMX code
    // also doesn't set it). Avoid a false decoder warning on startup
    // by setting it ourselves.
    if (QString::fromUtf8(codec->name).endsWith("_omx", Qt::CaseInsensitive)) {
        caps |= AV_CODEC_CAP_HARDWARE;
    }

    return caps;
}

bool FFmpegVideoDecoder::isDecoderMatchForParams(const AVCodec *decoder, PDECODER_PARAMETERS params)
{
    SDL_assert(params->videoFormat & (VIDEO_FORMAT_MASK_H264 | VIDEO_FORMAT_MASK_H265 | VIDEO_FORMAT_MASK_AV1));

#if defined(HAVE_MMAL) && !defined(ALLOW_EGL_WITH_MMAL)
    // Only enable V4L2M2M by default on non-MMAL (RPi) builds. The performance
    // of the V4L2M2M wrapper around MMAL is not enough for 1080p 60 FPS, so we
    // would rather show the missing hardware acceleration warning when the user
    // is in Full KMS mode rather than try to use a poorly performing hwaccel.
    // See discussion on https://github.com/jc-kynesim/rpi-ffmpeg/pull/25
    if (strcmp(decoder->name, "h264_v4l2m2m") == 0) {
        return false;
    }
#endif

    return ((params->videoFormat & VIDEO_FORMAT_MASK_H264) && decoder->id == AV_CODEC_ID_H264) ||
           ((params->videoFormat & VIDEO_FORMAT_MASK_H265) && decoder->id == AV_CODEC_ID_HEVC) ||
           ((params->videoFormat & VIDEO_FORMAT_MASK_AV1)  && decoder->id == AV_CODEC_ID_AV1);
}

bool FFmpegVideoDecoder::tryInitializeHwAccelDecoder(PDECODER_PARAMETERS params, int pass, QSet<const AVCodec*>& terminallyFailedHardwareDecoders)
{
    const AVCodec* decoder;
    void* codecIterator;

    SDL_assert(pass <= MAX_DECODER_PASS);

    // Iterate through hwaccel decoders
    codecIterator = NULL;
    while ((decoder = av_codec_iterate(&codecIterator))) {
        // Skip codecs that aren't decoders
        if (!av_codec_is_decoder(decoder)) {
            continue;
        }

        // Skip decoders that don't match our decoding parameters
        if (!isDecoderMatchForParams(decoder, params)) {
            continue;
        }

        // Skip non-hwaccel hardware decoders
        if (getAVCodecCapabilities(decoder) & AV_CODEC_CAP_HARDWARE) {
            continue;
        }

        // Skip hardware decoders that have returned a terminal failure status
        if (terminallyFailedHardwareDecoders.contains(decoder)) {
            continue;
        }

        // Look for the first matching hwaccel hardware decoder
        for (int i = 0;; i++) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
            if (!config) {
                // No remaining hwaccel options
                break;
            }

            // Initialize the hardware codec and submit a test frame if the renderer needs it
            IFFmpegRenderer::InitFailureReason failureReason;
            if (tryInitializeRenderer(decoder, AV_PIX_FMT_NONE, params, config, &failureReason,
                                      [config, pass]() -> IFFmpegRenderer* { return createHwAccelRenderer(config, pass); })) {
                return true;
            }
            else if (failureReason == IFFmpegRenderer::InitFailureReason::NoHardwareSupport) {
                terminallyFailedHardwareDecoders.insert(decoder);
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Skipping remaining hwaccels due lack of hardware support for specified codec");
                break;
            }
        }
    }

    return false;
}

bool FFmpegVideoDecoder::isZeroCopyFormat(AVPixelFormat format)
{
    const AVPixFmtDescriptor* formatDesc = av_pix_fmt_desc_get(format);
    return (formatDesc && !!(formatDesc->flags & AV_PIX_FMT_FLAG_HWACCEL));
}

bool FFmpegVideoDecoder::tryInitializeNonHwAccelDecoder(PDECODER_PARAMETERS params, bool requireZeroCopyFormat, QSet<const AVCodec*>& terminallyFailedHardwareDecoders)
{
    const AVCodec* decoder;
    void* codecIterator;

    // Iterate through non-hwaccel and non-standard hwaccel hardware decoders that have AV_CODEC_CAP_HARDWARE set
    codecIterator = NULL;
    while ((decoder = av_codec_iterate(&codecIterator))) {
        // Skip codecs that aren't decoders
        if (!av_codec_is_decoder(decoder)) {
            continue;
        }

        // Skip decoders that don't match our decoding parameters
        if (!isDecoderMatchForParams(decoder, params)) {
            continue;
        }

        // Skip software/hybrid decoders and normal hwaccel decoders (which were handled in the loop above)
        if (!(getAVCodecCapabilities(decoder) & AV_CODEC_CAP_HARDWARE)) {
            continue;
        }

        // Skip decoders without zero-copy output formats if requested
        if (requireZeroCopyFormat) {
            const AVPixelFormat* decoder_pix_fmts;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
            if (avcodec_get_supported_config(nullptr, decoder, AV_CODEC_CONFIG_PIX_FORMAT, 0,
                                             (const void**)&decoder_pix_fmts, nullptr) < 0) {
                decoder_pix_fmts = nullptr;
            }
#else
            decoder_pix_fmts = decoder->pix_fmts;
#endif
            bool foundZeroCopyFormat = false;
            for (int i = 0; decoder_pix_fmts && decoder_pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
                if (isZeroCopyFormat(decoder_pix_fmts[i])) {
                    foundZeroCopyFormat = true;
                    break;
                }
            }

            if (!foundZeroCopyFormat) {
                continue;
            }
        }

        // Skip hardware decoders that have returned a terminal failure status
        if (terminallyFailedHardwareDecoders.contains(decoder)) {
            continue;
        }

        // Try to initialize this decoder both as hwaccel and non-hwaccel
        if (tryInitializeRendererForUnknownDecoder(decoder, params, true)) {
            return true;
        }
    }

    return false;
}

bool FFmpegVideoDecoder::initialize(PDECODER_PARAMETERS params)
{
    // Increase log level until the first frame is decoded
    av_log_set_level(AV_LOG_DEBUG);

    // First try decoders that the user has manually specified via environment variables.
    // These must output surfaces in one of the formats that one of our renderers supports,
    // which is currently:
    // - AV_PIX_FMT_DRM_PRIME
    // - AV_PIX_FMT_MMAL
    // - AV_PIX_FMT_YUV420P
    // - AV_PIX_FMT_YUVJ420P
    // - AV_PIX_FMT_NV12
    // - AV_PIX_FMT_NV21
    {
        QString h264DecoderHint = qgetenv("H264_DECODER_HINT");
        if (!h264DecoderHint.isEmpty() && (params->videoFormat & VIDEO_FORMAT_MASK_H264)) {
            QByteArray decoderString = h264DecoderHint.toLocal8Bit();
            if (tryInitializeRendererForUnknownDecoder(avcodec_find_decoder_by_name(decoderString.constData()), params, true)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Using custom H.264 decoder (H264_DECODER_HINT): %s",
                            decoderString.constData());
                return true;
            }
            else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Custom H.264 decoder (H264_DECODER_HINT) failed to load: %s",
                             decoderString.constData());
            }
        }
    }
    {
        QString hevcDecoderHint = qgetenv("HEVC_DECODER_HINT");
        if (!hevcDecoderHint.isEmpty() && (params->videoFormat & VIDEO_FORMAT_MASK_H265)) {
            QByteArray decoderString = hevcDecoderHint.toLocal8Bit();
            if (tryInitializeRendererForUnknownDecoder(avcodec_find_decoder_by_name(decoderString.constData()), params, true)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Using custom HEVC decoder (HEVC_DECODER_HINT): %s",
                            decoderString.constData());
                return true;
            }
            else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Custom HEVC decoder (HEVC_DECODER_HINT) failed to load: %s",
                             decoderString.constData());
            }
        }
    }
    {
        QString av1DecoderHint = qgetenv("AV1_DECODER_HINT");
        if (!av1DecoderHint.isEmpty() && (params->videoFormat & VIDEO_FORMAT_MASK_AV1)) {
            QByteArray decoderString = av1DecoderHint.toLocal8Bit();
            if (tryInitializeRendererForUnknownDecoder(avcodec_find_decoder_by_name(decoderString.constData()), params, true)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Using custom AV1 decoder (AV1_DECODER_HINT): %s",
                            decoderString.constData());
                return true;
            }
            else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Custom AV1 decoder (AV1_DECODER_HINT) failed to load: %s",
                             decoderString.constData());
            }
        }
    }

    const AVCodec* decoder;
    void* codecIterator;

    // Look for a hardware decoder first unless software-only
    if (params->vds != StreamingPreferences::VDS_FORCE_SOFTWARE) {
        QSet<const AVCodec*> terminallyFailedHardwareDecoders;

        // Try tier 1 hwaccel decoders first
        if (tryInitializeHwAccelDecoder(params, 0, terminallyFailedHardwareDecoders)) {
            return true;
        }

        // Iterate through non-hwaccel and non-standard hwaccel hardware decoders that have AV_CODEC_CAP_HARDWARE set.
        //
        // We first try to find decoders with a hwaccel format that can be rendered without CPU copyback.
        // Failing that, we will accept a decoder that only supports copyback (or one with unknown pixfmts).
        if (tryInitializeNonHwAccelDecoder(params, true /* zero copy */, terminallyFailedHardwareDecoders)) {
            return true;
        }
        if (tryInitializeNonHwAccelDecoder(params, false /* zero copy */, terminallyFailedHardwareDecoders)) {
            return true;
        }

        // Try the remaining tiers of hwaccel decoders
        for (int pass = 1; pass <= MAX_DECODER_PASS; pass++) {
            if (tryInitializeHwAccelDecoder(params, pass, terminallyFailedHardwareDecoders)) {
                return true;
            }
        }
    }

    // Iterate through all software decoders if allowed
    if (params->vds != StreamingPreferences::VDS_FORCE_HARDWARE) {
        codecIterator = NULL;
        while ((decoder = av_codec_iterate(&codecIterator))) {
            // Skip codecs that aren't decoders
            if (!av_codec_is_decoder(decoder)) {
                continue;
            }

            // Skip decoders that don't match our decoding parameters
            if (!isDecoderMatchForParams(decoder, params)) {
                continue;
            }

            // Skip hardware decoders
            //
            // NB: We can't skip hwaccel decoders here because they can be both
            // hardware and software depending on whether an hwaccel is supplied.
            // Instead, we tell tryInitializeRendererForUnknownDecoder() not to
            // try hwaccel for this decoder.
            if (getAVCodecCapabilities(decoder) & AV_CODEC_CAP_HARDWARE) {
                continue;
            }

            // Try this decoder without hwaccel
            if (tryInitializeRendererForUnknownDecoder(decoder, params, false)) {
                return true;
            }
        }
    }

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Unable to find working decoder for format: %x",
                 params->videoFormat);
    return false;
}

void FFmpegVideoDecoder::writeBuffer(PLENTRY entry, int& offset)
{
    if (m_NeedsSpsFixup && entry->bufferType == BUFFER_TYPE_SPS) {
        h264_stream_t* stream = h264_new();
        int nalStart, nalEnd;

        // Read the old NALU
        find_nal_unit((uint8_t*)entry->data, entry->length, &nalStart, &nalEnd);
        read_nal_unit(stream,
                      (unsigned char *)&entry->data[nalStart],
                      nalEnd - nalStart);

        SDL_assert(nalStart == 3 || nalStart == 4); // 3 or 4 byte Annex B start sequence
        SDL_assert(nalEnd == entry->length);

        // Fixup the SPS to what OS X needs to use hardware acceleration
        stream->sps->num_ref_frames = 1;
        stream->sps->vui.max_dec_frame_buffering = 1;

        int initialOffset = offset;

        // Copy the modified NALU data. This clobbers byte 0 and starts NALU data at byte 1.
        // Since it prepended one extra byte, subtract one from the returned length.
        offset += write_nal_unit(stream, (uint8_t*)&m_DecodeBuffer.data()[initialOffset + nalStart - 1],
                                 MAX_SPS_EXTRA_SIZE + entry->length - nalStart) - 1;

        // Copy the NALU prefix over from the original SPS
        memcpy(&m_DecodeBuffer.data()[initialOffset], entry->data, nalStart);
        offset += nalStart;

        h264_free(stream);
    }
    else {
        // Write the buffer as-is
        memcpy(&m_DecodeBuffer.data()[offset],
               entry->data,
               entry->length);
        offset += entry->length;
    }
}

int FFmpegVideoDecoder::decoderThreadProcThunk(void *context)
{
    ((FFmpegVideoDecoder*)context)->decoderThreadProc();
    return 0;
}

void FFmpegVideoDecoder::decoderThreadProc()
{
    while (!SDL_AtomicGet(&m_DecoderThreadShouldQuit)) {
        if (m_FramesIn == m_FramesOut) {
            VIDEO_FRAME_HANDLE handle;
            PDECODE_UNIT du;

            // Waiting for input. All output frames have been received.
            // Block until we receive a new frame from the host.
            if (!LiWaitForNextVideoFrame(&handle, &du)) {
                // This might be a signal from the main thread to exit
                continue;
            }

            LiCompleteVideoFrame(handle, submitDecodeUnit(du));
        }

        if (m_FramesIn != m_FramesOut) {
            SDL_assert(m_FramesIn > m_FramesOut);

            // We have output frames to receive. Let's poll until we get one,
            // and submit new input data if/when we get it.
            AVFrame* frame = av_frame_alloc();
            if (!frame) {
                // Failed to allocate a frame but we did submit,
                // so we can return DR_OK
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Failed to allocate frame");
                continue;
            }

            int err;
            do {
                err = avcodec_receive_frame(m_VideoDecoderCtx, frame);
                if (err == 0) {
                    SDL_assert(m_FrameInfoQueue.size() == m_FramesIn - m_FramesOut);
                    m_FramesOut++;

                    // Attach HDR metadata to the frame if it's not already present. We will defer to
                    // any metadata contained in the bitstream itself since that is guaranteed to be
                    // correctly synchronized to each frame, unlike our async HDR metadata message.
                    SS_HDR_METADATA hdrMetadata;
                    if (LiGetHdrMetadata(&hdrMetadata)) {
                        if (av_frame_get_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA) == nullptr) {
                            auto mdm = av_mastering_display_metadata_create_side_data(frame);

                            mdm->display_primaries[0][0] = av_make_q(hdrMetadata.displayPrimaries[0].x, 50000);
                            mdm->display_primaries[0][1] = av_make_q(hdrMetadata.displayPrimaries[0].y, 50000);
                            mdm->display_primaries[1][0] = av_make_q(hdrMetadata.displayPrimaries[1].x, 50000);
                            mdm->display_primaries[1][1] = av_make_q(hdrMetadata.displayPrimaries[1].y, 50000);
                            mdm->display_primaries[2][0] = av_make_q(hdrMetadata.displayPrimaries[2].x, 50000);
                            mdm->display_primaries[2][1] = av_make_q(hdrMetadata.displayPrimaries[2].y, 50000);

                            mdm->white_point[0] = av_make_q(hdrMetadata.whitePoint.x, 50000);
                            mdm->white_point[1] = av_make_q(hdrMetadata.whitePoint.y, 50000);

                            mdm->min_luminance = av_make_q(hdrMetadata.minDisplayLuminance, 10000);
                            mdm->max_luminance = av_make_q(hdrMetadata.maxDisplayLuminance, 1);

                            mdm->has_luminance = hdrMetadata.maxDisplayLuminance != 0 ? 1 : 0;
                            mdm->has_primaries = hdrMetadata.displayPrimaries[0].x != 0 ? 1 : 0;
                        }

                        if ((hdrMetadata.maxContentLightLevel != 0 || hdrMetadata.maxFrameAverageLightLevel != 0) &&
                                av_frame_get_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL) == nullptr) {
                            auto clm = av_content_light_metadata_create_side_data(frame);

                            clm->MaxCLL = hdrMetadata.maxContentLightLevel;
                            clm->MaxFALL = hdrMetadata.maxFrameAverageLightLevel;
                        }
                    }

                    // Reset failed decodes count if we reached this far
                    m_ConsecutiveFailedDecodes = 0;

                    // Restore default log level after a successful decode
                    av_log_set_level(AV_LOG_INFO);

                    // Capture a frame timestamp to measuring pacing delay
                    frame->pkt_dts = LiGetMicroseconds();

                    if (!m_FrameInfoQueue.isEmpty()) {
                        // Data buffers in the DU are not valid here!
                        DECODE_UNIT du = m_FrameInfoQueue.dequeue();

                        // Count time in avcodec_send_packet() and avcodec_receive_frame()
                        // as time spent decoding. Also count time spent in the decode unit
                        // queue because that's directly caused by decoder latency.
                        m_ActiveWndVideoStats.totalDecodeTimeUs += (LiGetMicroseconds() - du.enqueueTimeUs);

                        // Store the presentation time (90 kHz timebase)
                        frame->pts = (int64_t)du.rtpTimestamp;
                    }

                    m_ActiveWndVideoStats.decodedFrames++;

                    // Queue the frame for rendering (or render now if pacer is disabled)
                    m_Pacer->submitFrame(frame);
                }
                else if (err == AVERROR(EAGAIN)) {
                    VIDEO_FRAME_HANDLE handle;
                    PDECODE_UNIT du;

                    // No output data, so let's try to submit more input data,
                    // while we're waiting for this to frame to come back.
                    if (LiPollNextVideoFrame(&handle, &du)) {
                        // FIXME: Handle EAGAIN on avcodec_send_packet() properly?
                        LiCompleteVideoFrame(handle, submitDecodeUnit(du));
                    }
                    else {
                        // No output data or input data. Let's wait a little bit.
                        SDL_Delay(2);
                    }
                }
                else {
                    char errorstring[512];

                    // FIXME: Should we pop an entry off m_FrameInfoQueue here?

                    av_strerror(err, errorstring, sizeof(errorstring));
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "avcodec_receive_frame() failed: %s (frame %d)",
                                errorstring,
                                !m_FrameInfoQueue.isEmpty() ? m_FrameInfoQueue.head().frameNumber : -1);

                    if (++m_ConsecutiveFailedDecodes == FAILED_DECODES_RESET_THRESHOLD) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                     "Resetting decoder due to consistent failure");

                        SDL_Event event;
                        event.type = SDL_RENDER_DEVICE_RESET;
                        SDL_PushEvent(&event);

                        // Don't consume any additional data
                        SDL_AtomicSet(&m_DecoderThreadShouldQuit, 1);
                    }

                    // Just in case the error resulted in the loss of the frame,
                    // request an IDR frame to reset our decoder state.
                    LiRequestIdrFrame();
                }
            } while (err == AVERROR(EAGAIN) && !SDL_AtomicGet(&m_DecoderThreadShouldQuit));

            if (err != 0) {
                // Free the frame if we failed to submit it
                av_frame_free(&frame);
            }
        }
    }
}

int FFmpegVideoDecoder::submitDecodeUnit(PDECODE_UNIT du)
{
    PLENTRY entry = du->bufferList;
    int err;

    SDL_assert(!m_TestOnly);

    // If this is the first frame, reject anything that's not an IDR frame
    if (m_FramesIn == 0 && du->frameType != FRAME_TYPE_IDR) {
        return DR_NEED_IDR;
    }

    if (!m_LastFrameNumber) {
        m_ActiveWndVideoStats.measurementStartUs = LiGetMicroseconds();
        m_LastFrameNumber = du->frameNumber;
    }
    else {
        // Any frame number greater than m_LastFrameNumber + 1 represents a dropped frame
        m_ActiveWndVideoStats.networkDroppedFrames += du->frameNumber - (m_LastFrameNumber + 1);
        m_ActiveWndVideoStats.totalFrames += du->frameNumber - (m_LastFrameNumber + 1);
        m_LastFrameNumber = du->frameNumber;
    }

    m_BwTracker.AddBytes(du->fullLength);

    // Flip stats windows roughly every second
    if (LiGetMicroseconds() > m_ActiveWndVideoStats.measurementStartUs + 1000000) {
        // Update overlay stats if it's enabled
        if (Session::get()->getOverlayManager().isOverlayEnabled(Overlay::OverlayDebug)) {
            VIDEO_STATS lastTwoWndStats = {};
            addVideoStats(m_LastWndVideoStats, lastTwoWndStats);
            addVideoStats(m_ActiveWndVideoStats, lastTwoWndStats);

            stringifyVideoStats(lastTwoWndStats,
                                Session::get()->getOverlayManager().getOverlayText(Overlay::OverlayDebug),
                                Session::get()->getOverlayManager().getOverlayMaxTextLength());
            Session::get()->getOverlayManager().setOverlayTextUpdated(Overlay::OverlayDebug);
        }

        // Accumulate these values into the global stats
        addVideoStats(m_ActiveWndVideoStats, m_GlobalVideoStats);

        // Move this window into the last window slot and clear it for next window
        SDL_memcpy(&m_LastWndVideoStats, &m_ActiveWndVideoStats, sizeof(m_ActiveWndVideoStats));
        SDL_zero(m_ActiveWndVideoStats);
        m_ActiveWndVideoStats.measurementStartUs = LiGetMicroseconds();
    }

    if (du->frameHostProcessingLatency != 0) {
        if (m_ActiveWndVideoStats.minHostProcessingLatency != 0) {
            m_ActiveWndVideoStats.minHostProcessingLatency = qMin(m_ActiveWndVideoStats.minHostProcessingLatency, du->frameHostProcessingLatency);
        }
        else {
            m_ActiveWndVideoStats.minHostProcessingLatency = du->frameHostProcessingLatency;
        }
        m_ActiveWndVideoStats.framesWithHostProcessingLatency += 1;
    }
    m_ActiveWndVideoStats.maxHostProcessingLatency = qMax(m_ActiveWndVideoStats.maxHostProcessingLatency, du->frameHostProcessingLatency);
    m_ActiveWndVideoStats.totalHostProcessingLatency += du->frameHostProcessingLatency;

    m_ActiveWndVideoStats.receivedFrames++;
    m_ActiveWndVideoStats.totalFrames++;

    int requiredBufferSize = du->fullLength;
    if (du->frameType == FRAME_TYPE_IDR) {
        // Add some extra space in case we need to do an SPS fixup
        requiredBufferSize += MAX_SPS_EXTRA_SIZE;
    }

    // Ensure the decoder buffer is large enough
    m_DecodeBuffer.reserve(requiredBufferSize + AV_INPUT_BUFFER_PADDING_SIZE);

    int offset = 0;
    while (entry != nullptr) {
        writeBuffer(entry, offset);
        entry = entry->next;
    }

    m_Pkt->data = reinterpret_cast<uint8_t*>(m_DecodeBuffer.data());
    m_Pkt->size = offset;

    if (du->frameType == FRAME_TYPE_IDR) {
        m_Pkt->flags = AV_PKT_FLAG_KEY;
    }
    else {
        m_Pkt->flags = 0;
    }

    m_ActiveWndVideoStats.totalReassemblyTimeUs += (du->enqueueTimeUs - du->receiveTimeUs);

    err = avcodec_send_packet(m_VideoDecoderCtx, m_Pkt);
    if (err < 0) {
        char errorstring[512];
        av_strerror(err, errorstring, sizeof(errorstring));
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "avcodec_send_packet() failed: %s (frame %d)",
                    errorstring,
                    du->frameNumber);

        // If we've failed a bunch of decodes in a row, the decoder/renderer is
        // clearly unhealthy, so let's generate a synthetic reset event to trigger
        // the event loop to destroy and recreate the decoder.
        if (++m_ConsecutiveFailedDecodes == FAILED_DECODES_RESET_THRESHOLD) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Resetting decoder due to consistent failure");

            SDL_Event event;
            event.type = SDL_RENDER_DEVICE_RESET;
            SDL_PushEvent(&event);

            // Don't consume any additional data
            SDL_AtomicSet(&m_DecoderThreadShouldQuit, 1);
        }

        return DR_NEED_IDR;
    }

    m_FrameInfoQueue.enqueue(*du);

    m_FramesIn++;
    return DR_OK;
}

void FFmpegVideoDecoder::renderFrameOnMainThread()
{
    m_Pacer->renderOnMainThread();
}

