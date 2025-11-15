#include <streaming/session.h>
#include "vdpau.h"
#include <streaming/streamutils.h>
#include <utils.h>

#include <SDL_syswm.h>

#define BAIL_ON_FAIL(status, something) if ((status) != VDP_STATUS_OK) { \
                                            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, \
                                                        #something " failed: %d", (status)); \
                                            return false; \
                                        }

#define GET_PROC_ADDRESS(id, func) status = vdpauCtx->get_proc_address(m_Device, id, (void**)func); \
                                   BAIL_ON_FAIL(status, id)

const VdpRGBAFormat VDPAURenderer::k_OutputFormats8Bit[] = {
    VDP_RGBA_FORMAT_B8G8R8A8,
    VDP_RGBA_FORMAT_R8G8B8A8
};

const VdpRGBAFormat VDPAURenderer::k_OutputFormats10Bit[] = {
    VDP_RGBA_FORMAT_B10G10R10A2,
    VDP_RGBA_FORMAT_R10G10B10A2
};

VDPAURenderer::VDPAURenderer(int decoderSelectionPass)
    : IFFmpegRenderer(RendererType::VDPAU),
      m_DecoderSelectionPass(decoderSelectionPass),
      m_HwContext(nullptr),
      m_PresentationQueueTarget(0),
      m_PresentationQueue(0),
      m_VideoMixer(0),
      m_OverlayMutex(nullptr),
      m_NextSurfaceIndex(0)
{
    SDL_zero(m_OutputSurface);
    SDL_zero(m_OverlaySurface);
}

VDPAURenderer::~VDPAURenderer()
{
    if (m_PresentationQueue != 0) {
        m_VdpPresentationQueueDestroy(m_PresentationQueue);
    }

    if (m_VideoMixer != 0) {
        m_VdpVideoMixerDestroy(m_VideoMixer);
    }

    if (m_PresentationQueueTarget != 0) {
        m_VdpPresentationQueueTargetDestroy(m_PresentationQueueTarget);
    }

    for (int i = 0; i < OUTPUT_SURFACE_COUNT; i++) {
        if (m_OutputSurface[i] != 0) {
            m_VdpOutputSurfaceDestroy(m_OutputSurface[i]);
        }
    }

    for (int i = 0; i < Overlay::OverlayMax; i++) {
        if (m_OverlaySurface[i] != 0) {
            m_VdpBitmapSurfaceDestroy(m_OverlaySurface[i]);
        }
    }

    if (m_OverlayMutex != nullptr) {
        SDL_DestroyMutex(m_OverlayMutex);
    }

    // This must be done last as it frees VDPAU context required to call
    // the functions above.
    if (m_HwContext != nullptr) {
        av_buffer_unref(&m_HwContext);
    }
}

bool VDPAURenderer::initialize(PDECODER_PARAMETERS params)
{
    int err;
    VdpStatus status;
    SDL_SysWMinfo info;

    // Avoid initializing VDPAU on this window on the first selection pass if:
    // a) We know we want HDR compatibility
    // b) The user wants to prefer Vulkan
    //
    // Using VDPAU may lead to side-effects that break our attempts to create
    // a Vulkan swapchain on this window later.
    if (m_DecoderSelectionPass == 0) {
        if (params->videoFormat & VIDEO_FORMAT_MASK_10BIT) {
            return false;
        }
        else if (qgetenv("PREFER_VULKAN") == "1") {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Deprioritizing Vulkan-incompatible VDPAU renderer due to PREFER_VULKAN=1");
            return false;
        }
    }

    SDL_VERSION(&info.version);

    if (!SDL_GetWindowWMInfo(params->window, &info)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_GetWindowWMInfo() failed: %s",
                     SDL_GetError());
        m_InitFailureReason = InitFailureReason::NoSoftwareSupport;
        return false;
    }

    if (info.subsystem == SDL_SYSWM_WAYLAND) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "VDPAU is not supported on Wayland");
        m_InitFailureReason = InitFailureReason::NoSoftwareSupport;
        return false;
    }
    else if (info.subsystem != SDL_SYSWM_X11) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VDPAU is not supported on the current subsystem: %d",
                     info.subsystem);
        m_InitFailureReason = InitFailureReason::NoSoftwareSupport;
        return false;
    }
    else if (qgetenv("VDPAU_XWAYLAND") != "1" && WMUtils::isRunningWayland()) {
        // VDPAU initialization causes Moonlight to crash when using XWayland in a Flatpak
        // on a system with the Nvidia 495.44 driver. VDPAU won't work under XWayland anyway,
        // so let's not risk trying it (unless the user wants to roll the dice).
        // https://gitlab.freedesktop.org/vdpau/libvdpau/-/issues/2
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "VDPAU is disabled on XWayland. Set VDPAU_XWAYLAND=1 to try your luck.");
        m_InitFailureReason = InitFailureReason::NoSoftwareSupport;
        return false;
    }

    m_VideoWidth = params->width;
    m_VideoHeight = params->height;

    err = av_hwdevice_ctx_create(&m_HwContext,
                                 AV_HWDEVICE_TYPE_VDPAU,
                                 nullptr, nullptr, 0);

#if defined(APP_IMAGE) || defined(USE_FALLBACK_DRIVER_PATHS)
    // AppImages will be running with our libvdpau.so which means they don't know about
    // distro-specific driver paths. To avoid failing in this scenario, we'll hardcode
    // some such paths here for common distros. Non-AppImage packaging mechanisms won't
    // need this fallback because either:
    // a) They are using both distro libvdpau.so and distro VDPAU drivers (native packages)
    // b) They are using both runtime libvdpau.so and runtime VDPAU drivers (Flatpak/Snap)
    if (err < 0 && qEnvironmentVariableIsEmpty("VDPAU_DRIVER_PATH")) {
        static const char* driverPathsToTry[] = {
#if Q_PROCESSOR_WORDSIZE == 8
            "/usr/lib64",
            "/usr/lib64/vdpau", // Fedora x86_64
#endif
            "/usr/lib",
            "/usr/lib/vdpau", // Fedora i386
#if defined(Q_PROCESSOR_X86_64)
            "/usr/lib/x86_64-linux-gnu",
            "/usr/lib/x86_64-linux-gnu/vdpau", // Ubuntu/Debian x86_64
#elif defined(Q_PROCESSOR_X86_32)
            "/usr/lib/i386-linux-gnu",
            "/usr/lib/i386-linux-gnu/vdpau", // Ubuntu/Debian i386
#endif
        };

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Trying fallback VDPAU driver paths");

        // Unlike libva, libvdpau doesn't support multiple paths in
        // their VDPAU_DRIVER_PATH variable, so we must try them all
        // one at a time.
        for (auto& driverPath : driverPathsToTry) {
            qputenv("VDPAU_DRIVER_PATH", driverPath);
            err = av_hwdevice_ctx_create(&m_HwContext,
                                         AV_HWDEVICE_TYPE_VDPAU,
                                         nullptr, nullptr, 0);
            if (err == 0) {
                // Success!
                break;
            }
        }

        if (err < 0) {
            // Unset VDPAU_DRIVER_PATH if we set it ourselves
            // and we didn't find any working VDPAU drivers.
            qunsetenv("VDPAU_DRIVER_PATH");
        }
    }
#endif

    if (err < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create VDPAU context: %d",
                     err);
        m_InitFailureReason = InitFailureReason::NoSoftwareSupport;
        return false;
    }

    AVHWDeviceContext* devCtx = (AVHWDeviceContext*)m_HwContext->data;
    AVVDPAUDeviceContext* vdpauCtx = (AVVDPAUDeviceContext*)devCtx->hwctx;
    m_Device = vdpauCtx->device;

    GET_PROC_ADDRESS(VDP_FUNC_ID_GET_ERROR_STRING, &m_VdpGetErrorString);
    GET_PROC_ADDRESS(VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_DESTROY, &m_VdpPresentationQueueTargetDestroy);
    GET_PROC_ADDRESS(VDP_FUNC_ID_VIDEO_MIXER_CREATE, &m_VdpVideoMixerCreate);
    GET_PROC_ADDRESS(VDP_FUNC_ID_VIDEO_MIXER_DESTROY, &m_VdpVideoMixerDestroy);
    GET_PROC_ADDRESS(VDP_FUNC_ID_VIDEO_MIXER_RENDER, &m_VdpVideoMixerRender);
    GET_PROC_ADDRESS(VDP_FUNC_ID_PRESENTATION_QUEUE_CREATE, &m_VdpPresentationQueueCreate);
    GET_PROC_ADDRESS(VDP_FUNC_ID_PRESENTATION_QUEUE_DESTROY, &m_VdpPresentationQueueDestroy);
    GET_PROC_ADDRESS(VDP_FUNC_ID_PRESENTATION_QUEUE_DISPLAY, &m_VdpPresentationQueueDisplay);
    GET_PROC_ADDRESS(VDP_FUNC_ID_PRESENTATION_QUEUE_SET_BACKGROUND_COLOR, &m_VdpPresentationQueueSetBackgroundColor);
    GET_PROC_ADDRESS(VDP_FUNC_ID_PRESENTATION_QUEUE_BLOCK_UNTIL_SURFACE_IDLE, &m_VdpPresentationQueueBlockUntilSurfaceIdle);
    GET_PROC_ADDRESS(VDP_FUNC_ID_OUTPUT_SURFACE_CREATE, &m_VdpOutputSurfaceCreate);
    GET_PROC_ADDRESS(VDP_FUNC_ID_OUTPUT_SURFACE_DESTROY, &m_VdpOutputSurfaceDestroy);
    GET_PROC_ADDRESS(VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_CAPABILITIES, &m_VdpOutputSurfaceQueryCapabilities);
    GET_PROC_ADDRESS(VDP_FUNC_ID_BITMAP_SURFACE_CREATE, &m_VdpBitmapSurfaceCreate);
    GET_PROC_ADDRESS(VDP_FUNC_ID_BITMAP_SURFACE_DESTROY, &m_VdpBitmapSurfaceDestroy);
    GET_PROC_ADDRESS(VDP_FUNC_ID_BITMAP_SURFACE_PUT_BITS_NATIVE, &m_VdpBitmapSurfacePutBitsNative);
    GET_PROC_ADDRESS(VDP_FUNC_ID_OUTPUT_SURFACE_RENDER_BITMAP_SURFACE, &m_VdpOutputSurfaceRenderBitmapSurface);
    GET_PROC_ADDRESS(VDP_FUNC_ID_VIDEO_SURFACE_GET_PARAMETERS, &m_VdpVideoSurfaceGetParameters);
    GET_PROC_ADDRESS(VDP_FUNC_ID_GET_INFORMATION_STRING, &m_VdpGetInformationString);

    SDL_GetWindowSize(params->window, (int*)&m_DisplayWidth, (int*)&m_DisplayHeight);

    SDL_assert(info.subsystem == SDL_SYSWM_X11);

    GET_PROC_ADDRESS(VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_CREATE_X11,
                     &m_VdpPresentationQueueTargetCreateX11);
    status = m_VdpPresentationQueueTargetCreateX11(m_Device,
                                                   info.info.x11.window,
                                                   &m_PresentationQueueTarget);
    if (status != VDP_STATUS_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VdpPresentationQueueTargetCreateX11() failed: %s",
                     m_VdpGetErrorString(status));
        return false;
    }

    const char* infoString;
    if (m_VdpGetInformationString(&infoString) == VDP_STATUS_OK) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Driver: %s",
                    infoString);
    }

    // Try our available output formats to find something the GPU supports
    bool foundFormat = false;
    for (int i = 0; i < OUTPUT_SURFACE_FORMAT_COUNT; i++) {
        VdpBool supported;
        uint32_t maxWidth, maxHeight;
        VdpRGBAFormat candidateFormat =
                (params->videoFormat & VIDEO_FORMAT_MASK_10BIT) ?
                    k_OutputFormats10Bit[i] : k_OutputFormats8Bit[i];

        status = m_VdpOutputSurfaceQueryCapabilities(m_Device, candidateFormat,
                                                     &supported, &maxWidth, &maxHeight);
        if (status != VDP_STATUS_OK) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "VdpOutputSurfaceQueryCapabilities() failed: %s",
                         m_VdpGetErrorString(status));
            return false;
        }

        if (supported) {
            if (m_DisplayWidth <= maxWidth && m_DisplayHeight <= maxHeight) {
                m_OutputSurfaceFormat = candidateFormat;
                foundFormat = true;
                break;
            }
            else  {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Display size not within capabilities %dx%d vs %dx%d",
                            m_DisplayWidth, m_DisplayWidth,
                            maxWidth, maxHeight);
            }
        }
    }

    if (!foundFormat) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "No compatible output surface format found!");
        return false;
    }

    // Create the output surfaces
    for (int i = 0; i < OUTPUT_SURFACE_COUNT; i++) {
        // It seems there's some lazy freeing going on or something in VDPAU
        // because we can get VDP_STATUS_RESOURCES, then wait a bit and it'll
        // complete without a problem.
        int tries = 1;
        do {
            status = m_VdpOutputSurfaceCreate(m_Device, m_OutputSurfaceFormat,
                                              m_DisplayWidth, m_DisplayHeight,
                                              &m_OutputSurface[i]);
            if (status != VDP_STATUS_OK) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "VdpOutputSurfaceCreate() try #%d: %s",
                            tries,
                            m_VdpGetErrorString(status));
                SDL_Delay(250);
            }
        } while (status == VDP_STATUS_RESOURCES && ++tries <= 10);

        if (status != VDP_STATUS_OK) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "VdpOutputSurfaceCreate() failed: %s",
                         m_VdpGetErrorString(status));
            return false;
        }
    }

    status = m_VdpPresentationQueueCreate(m_Device, m_PresentationQueueTarget,
                                          &m_PresentationQueue);
    if (status != VDP_STATUS_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VdpPresentationQueueCreate() failed: %s",
                     m_VdpGetErrorString(status));
        return false;
    }

    // Set the background to opaque black
    VdpColor color = {0.0, 0.0, 0.0, 1.0};
    m_VdpPresentationQueueSetBackgroundColor(m_PresentationQueue, &color);

    // Populate blend state for overlays
    m_OverlayBlendState.struct_version = VDP_OUTPUT_SURFACE_RENDER_BLEND_STATE_VERSION;
    m_OverlayBlendState.blend_factor_destination_alpha = VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    m_OverlayBlendState.blend_factor_destination_color = VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    m_OverlayBlendState.blend_factor_source_alpha = VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_SRC_ALPHA;
    m_OverlayBlendState.blend_factor_source_color = VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_SRC_ALPHA;
    m_OverlayBlendState.blend_equation_alpha = VDP_OUTPUT_SURFACE_RENDER_BLEND_EQUATION_ADD;
    m_OverlayBlendState.blend_equation_color = VDP_OUTPUT_SURFACE_RENDER_BLEND_EQUATION_ADD;
    m_OverlayBlendState.blend_constant = {};

    // Allocate mutex to synchronize overlay updates and rendering
    m_OverlayMutex = SDL_CreateMutex();
    if (m_OverlayMutex == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create overlay mutex");
        return false;
    }

    return true;
}

bool VDPAURenderer::prepareDecoderContext(AVCodecContext* context, AVDictionary**)
{
    context->hw_device_ctx = av_buffer_ref(m_HwContext);

    // Allow HEVC usage on VDPAU. This was disabled by FFmpeg due to
    // GL interop issues, but we use VDPAU for rendering so it's no issue.
    // https://github.com/FFmpeg/FFmpeg/commit/64ecb78b7179cab2dbdf835463104679dbb7c895
    context->hwaccel_flags |= AV_HWACCEL_FLAG_ALLOW_PROFILE_MISMATCH;

    // This flag is recommended due to hardware underreporting supported levels
    context->hwaccel_flags |= AV_HWACCEL_FLAG_IGNORE_LEVEL;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Using VDPAU accelerated renderer");

    return true;
}

void VDPAURenderer::notifyOverlayUpdated(Overlay::OverlayType type)
{
    VdpStatus status;

    SDL_Surface* newSurface = Session::get()->getOverlayManager().getUpdatedOverlaySurface(type);
    bool overlayEnabled = Session::get()->getOverlayManager().isOverlayEnabled(type);
    if (newSurface == nullptr && overlayEnabled) {
        // There's no updated surface and the overlay is enabled, so just leave the old surface alone.
        return;
    }

    // Destroy the old surface
    // NB: The mutex ensures the surface is not currently being read for rendering.
    // NB 2: It is safe to unlock here because this thread is the only surface producer.
    SDL_LockMutex(m_OverlayMutex);
    VdpBitmapSurface oldBitmapSurface = m_OverlaySurface[type];
    m_OverlaySurface[type] = 0;
    SDL_UnlockMutex(m_OverlayMutex);

    if (oldBitmapSurface != 0) {
        status = m_VdpBitmapSurfaceDestroy(oldBitmapSurface);
        if (status != VDP_STATUS_OK) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "VdpBitmapSurfaceDestroy() failed: %s",
                         m_VdpGetErrorString(status));

            // This should never happen.
            SDL_assert(false);
        }
    }

    if (!overlayEnabled) {
        SDL_FreeSurface(newSurface);
        return;
    }

    if (newSurface != nullptr) {
        SDL_assert(!SDL_MUSTLOCK(newSurface));
        SDL_assert(newSurface->format->format == SDL_PIXELFORMAT_ARGB8888);

        VdpBitmapSurface newBitmapSurface = 0;
        status = m_VdpBitmapSurfaceCreate(m_Device,
                                          VDP_RGBA_FORMAT_B8G8R8A8,
                                          newSurface->w,
                                          newSurface->h,
                                          VDP_TRUE, // Is this correct?
                                          &newBitmapSurface);
        if (status != VDP_STATUS_OK) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "VdpBitmapSurfaceCreate() failed: %s",
                         m_VdpGetErrorString(status));
            SDL_FreeSurface(newSurface);
            return;
        }

        status = m_VdpBitmapSurfacePutBitsNative(newBitmapSurface,
                                                 &newSurface->pixels,
                                                 (const uint32_t*)&newSurface->pitch,
                                                 nullptr);
        if (status != VDP_STATUS_OK) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "VdpBitmapSurfacePutBitsNative() failed: %s",
                         m_VdpGetErrorString(status));
            m_VdpBitmapSurfaceDestroy(newBitmapSurface);
            SDL_FreeSurface(newSurface);
            return;
        }

        VdpRect overlayRect;

        if (type == Overlay::OverlayStatusUpdate) {
            // Bottom Left
            overlayRect.x0 = 0;
            overlayRect.y0 = m_DisplayHeight - newSurface->h;
        }
        else if (type == Overlay::OverlayDebug) {
            // Top left
            overlayRect.x0 = 0;
            overlayRect.y0 = 0;
        }

        overlayRect.x1 = overlayRect.x0 + newSurface->w;
        overlayRect.y1 = overlayRect.y0 + newSurface->h;

        // Surface data is no longer needed
        SDL_FreeSurface(newSurface);

        SDL_LockMutex(m_OverlayMutex);
        m_OverlaySurface[type] = newBitmapSurface;
        m_OverlayRect[type] = overlayRect;
        SDL_UnlockMutex(m_OverlayMutex);
    }
}

bool VDPAURenderer::needsTestFrame()
{
    // We need a test frame to see if this VDPAU driver
    // supports the profile used for streaming
    return true;
}

int VDPAURenderer::getDecoderColorspace()
{
    // VDPAU defaults to Rec 601.
    // https://http.download.nvidia.com/XFree86/vdpau/doxygen/html/group___vdp_video_mixer.html#ga65580813e9045d94b739ed2bb8b62b46
    //
    // AMD and Nvidia GPUs both correctly process Rec 601, so let's not try our luck using a non-default colorspace.
    return COLORSPACE_REC_601;
}

int VDPAURenderer::getDecoderCapabilities()
{
    return CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC |
           CAPABILITY_REFERENCE_FRAME_INVALIDATION_AV1;
}

void VDPAURenderer::renderOverlay(VdpOutputSurface destination, Overlay::OverlayType type)
{
    VdpStatus status;

    // Don't even bother locking the mutex if the overlay is disabled
    if (!Session::get()->getOverlayManager().isOverlayEnabled(type)) {
        return;
    }

    if (SDL_TryLockMutex(m_OverlayMutex) != 0) {
        // If the overlay is currently being updated, skip rendering it this frame.
        return;
    }

    // Check if there's a surface to render
    if (m_OverlaySurface[type] == 0) {
        SDL_UnlockMutex(m_OverlayMutex);
        return;
    }

    status = m_VdpOutputSurfaceRenderBitmapSurface(destination,
                                                   &m_OverlayRect[type],
                                                   m_OverlaySurface[type],
                                                   nullptr,
                                                   nullptr,
                                                   &m_OverlayBlendState,
                                                   0);

    SDL_UnlockMutex(m_OverlayMutex);

    if (status != VDP_STATUS_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VdpOutputSurfaceRenderBitmapSurface() failed: %s",
                     m_VdpGetErrorString(status));
        return;
    }
}

void VDPAURenderer::waitToRender()
{
    VdpOutputSurface chosenSurface = m_OutputSurface[m_NextSurfaceIndex];

    // Wait for the next render target surface to be idle before proceeding
    VdpTime pts;
    m_VdpPresentationQueueBlockUntilSurfaceIdle(m_PresentationQueue, chosenSurface, &pts);
}

void VDPAURenderer::renderFrame(AVFrame* frame)
{
    VdpStatus status;
    VdpVideoSurface videoSurface = (VdpVideoSurface)(uintptr_t)frame->data[3];

    // This is safe without locking because this is always called on the main thread
    VdpOutputSurface chosenSurface = m_OutputSurface[m_NextSurfaceIndex];
    m_NextSurfaceIndex = (m_NextSurfaceIndex + 1) % OUTPUT_SURFACE_COUNT;

    // We need to create the mixer on the fly, because we don't know the dimensions
    // of our video surfaces in advance of decoding. We also need to recreate it when
    // the frame format or size changes.
    if (hasFrameFormatChanged(frame)) {
        if (m_VideoMixer != 0) {
            m_VdpVideoMixerDestroy(m_VideoMixer);
        }

        VdpChromaType videoSurfaceChroma;
        uint32_t videoSurfaceWidth, videoSurfaceHeight;
        status = m_VdpVideoSurfaceGetParameters(videoSurface, &videoSurfaceChroma,
                                                &videoSurfaceWidth, &videoSurfaceHeight);
        if (status != VDP_STATUS_OK) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "VdpVideoSurfaceGetParameters() failed: %s",
                         m_VdpGetErrorString(status));
            return;
        }

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "VDPAU surface size: %dx%d",
                    videoSurfaceWidth, videoSurfaceHeight);

    #define PARAM_COUNT 3
        const VdpVideoMixerParameter params[PARAM_COUNT] = {
            VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH,
            VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT,
            VDP_VIDEO_MIXER_PARAMETER_CHROMA_TYPE,
        };
        const void* const paramValues[PARAM_COUNT] = {
            &videoSurfaceWidth,
            &videoSurfaceHeight,
            &videoSurfaceChroma,
        };

        status = m_VdpVideoMixerCreate(m_Device, 0, nullptr,
                                       PARAM_COUNT, params, paramValues,
                                       &m_VideoMixer);
        if (status != VDP_STATUS_OK) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "VdpVideoMixerCreate() failed: %s",
                         m_VdpGetErrorString(status));
            return;
        }
    }

    // Wait for this frame to be off the screen. This will usually be a no-op
    // since it already happened in waitToRender(). However, that won't be the
    // case is when frame pacing is enabled.
    VdpTime pts;
    m_VdpPresentationQueueBlockUntilSurfaceIdle(m_PresentationQueue, chosenSurface, &pts);

    VdpRect sourceRect, outputRect;

    SDL_Rect src, dst;
    src.x = src.y = 0;
    src.w = m_VideoWidth;
    src.h = m_VideoHeight;
    dst.x = dst.y = 0;
    dst.w = m_DisplayWidth;
    dst.h = m_DisplayHeight;

    StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

    outputRect.x0 = dst.x;
    outputRect.x1 = dst.x + dst.w;
    outputRect.y0 = dst.y;
    outputRect.y1 = dst.y + dst.h;

    sourceRect.x0 = sourceRect.y0 = 0;
    sourceRect.x1 = m_VideoWidth;
    sourceRect.y1 = m_VideoHeight;

    // Render the next frame into the output surface
    status = m_VdpVideoMixerRender(m_VideoMixer,
                                   VDP_INVALID_HANDLE, nullptr,
                                   VDP_VIDEO_MIXER_PICTURE_STRUCTURE_FRAME,
                                   0, nullptr,
                                   videoSurface,
                                   0, nullptr,
                                   &sourceRect,
                                   chosenSurface,
                                   nullptr,
                                   &outputRect,
                                   0,
                                   nullptr);
    if (status != VDP_STATUS_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VdpVideoMixerRender() failed: %s",
                     m_VdpGetErrorString(status));
        return;
    }

    // Render overlays into the output surface before display
    for (int i = 0; i < Overlay::OverlayMax; i++) {
        renderOverlay(chosenSurface, (Overlay::OverlayType)i);
    }

    // Queue the frame for display immediately
    status = m_VdpPresentationQueueDisplay(m_PresentationQueue, chosenSurface, 0, 0, 0);
    if (status != VDP_STATUS_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VdpPresentationQueueDisplay() failed: %s",
                     m_VdpGetErrorString(status));
        return;
    }
}
