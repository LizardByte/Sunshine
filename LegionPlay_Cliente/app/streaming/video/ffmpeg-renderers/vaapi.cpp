#include <QString>

#include <vector>

// HACK: Include before vaapi.h to prevent conflicts with Xlib.h
#include <streaming/session.h>

#include "vaapi.h"
#include "utils.h"
#include <streaming/streamutils.h>

#ifdef HAVE_LIBVA_DRM
#include <xf86drm.h>
#endif

#include <SDL_syswm.h>

#include <unistd.h>
#include <fcntl.h>

VAAPIRenderer::VAAPIRenderer(int decoderSelectionPass)
    : IFFmpegRenderer(RendererType::VAAPI),
      m_DecoderSelectionPass(decoderSelectionPass),
      m_HwContext(nullptr),
      m_BlacklistedForDirectRendering(false),
      m_RequiresExplicitPixelFormat(false),
      m_OverlayMutex(nullptr)
#ifdef HAVE_EGL
    , m_EglExportType(EglExportType::Unknown),
      m_EglImageFactory(this)
#endif
{
#ifdef HAVE_EGL
    SDL_zero(m_PrimeDescriptor);
#endif

#ifdef HAVE_LIBVA_DRM
    m_DrmFd = -1;
#endif

    SDL_zero(m_OverlayImage);
    SDL_zero(m_OverlaySubpicture);
    SDL_zero(m_OverlayFormat);
}

VAAPIRenderer::~VAAPIRenderer()
{
    if (m_HwContext != nullptr) {
        AVHWDeviceContext* deviceContext = (AVHWDeviceContext*)m_HwContext->data;
        AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)deviceContext->hwctx;

        // Hold onto this VADisplay since we'll need it to uninitialize VAAPI
        VADisplay display = vaDeviceContext->display;

        for (int i = 0; i < Overlay::OverlayMax; i++) {
            if (m_OverlaySubpicture[i] != 0) {
                vaDestroySubpicture(display, m_OverlaySubpicture[i]);
            }
            if (m_OverlayImage[i].image_id != 0) {
                vaDestroyImage(display, m_OverlayImage[i].image_id);
            }
        }

        av_buffer_unref(&m_HwContext);

        if (display) {
            vaTerminate(display);
        }
    }

#ifdef HAVE_LIBVA_DRM
    if (m_DrmFd >= 0) {
        close(m_DrmFd);
    }
#endif

    if (m_OverlayMutex != nullptr) {
        SDL_DestroyMutex(m_OverlayMutex);
    }
}

VADisplay
VAAPIRenderer::openDisplay(SDL_Window* window)
{
    SDL_SysWMinfo info;
    VADisplay display;

    SDL_VERSION(&info.version);

    if (!SDL_GetWindowWMInfo(window, &info)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_GetWindowWMInfo() failed: %s",
                     SDL_GetError());
        return nullptr;
    }

    m_WindowSystem = info.subsystem;
    if (info.subsystem == SDL_SYSWM_X11) {
#ifdef HAVE_LIBVA_X11
        m_XWindow = info.info.x11.window;
        display = vaGetDisplay(info.info.x11.display);
        if (display == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Unable to open X11 display for VAAPI");
            return nullptr;
        }
#else
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Moonlight not compiled with VAAPI X11 support!");
        return nullptr;
#endif
    }
    else if (info.subsystem == SDL_SYSWM_WAYLAND) {
#ifdef HAVE_LIBVA_WAYLAND
        display = vaGetDisplayWl(info.info.wl.display);
        if (display == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Unable to open Wayland display for VAAPI");
            return nullptr;
        }
#else
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Moonlight not compiled with VAAPI Wayland support!");
        return nullptr;
#endif
    }
#if defined(SDL_VIDEO_DRIVER_KMSDRM) && defined(HAVE_LIBVA_DRM) && SDL_VERSION_ATLEAST(2, 0, 15)
    else if (info.subsystem == SDL_SYSWM_KMSDRM) {
        // It's possible to enter this function several times as we're probing VA drivers.
        // Make sure to only duplicate the DRM FD the first time through.
        if (m_DrmFd < 0) {
            // Try to get the FD that we're sharing with SDL
            bool mustCloseFd = false;
            int fd = StreamUtils::getDrmFdForWindow(window, &mustCloseFd);
            if (fd < 0) {
                // Try to open any DRM render node
                fd = StreamUtils::getDrmFd(true);
                if (fd < 0) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "Failed to open DRM render node: %d",
                                 errno);
                    return nullptr;
                }
            }

            // If the KMSDRM FD is not a render node FD, open the render node for libva to use.
            // Since libva 2.20, using a primary node will fail in vaGetDriverNames().
            if (drmGetNodeTypeFromFd(fd) != DRM_NODE_RENDER) {
                char* renderNodePath = drmGetRenderDeviceNameFromFd(fd);
                if (renderNodePath) {
                    // Don't need the primary node FD anymore
                    if (mustCloseFd) {
                        close(fd);
                    }

                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "Opening render node for VAAPI: %s",
                                renderNodePath);
                    m_DrmFd = open(renderNodePath, O_RDWR | O_CLOEXEC);
                    free(renderNodePath);
                    if (m_DrmFd < 0) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                     "Failed to open render node: %d",
                                     errno);
                        return nullptr;
                    }
                }
                else {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Failed to get render node path. Using the SDL FD directly.");
                    m_DrmFd = mustCloseFd ? fd : dup(fd);
                }
            }
            else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "KMSDRM FD is already a render node. Using the SDL FD directly.");
                m_DrmFd = mustCloseFd ? fd : dup(fd);
            }
        }

        display = vaGetDisplayDRM(m_DrmFd);
        if (display == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Unable to open DRM display for VAAPI");
            return nullptr;
        }
    }
#endif
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unsupported VAAPI rendering subsystem: %d",
                     info.subsystem);
        return nullptr;
    }

    return display;
}

VAStatus
VAAPIRenderer::tryVaInitialize(AVVAAPIDeviceContext* vaDeviceContext, PDECODER_PARAMETERS params, int* major, int* minor)
{
    VAStatus status;

    SDL_assert(vaDeviceContext->display == nullptr);

    vaDeviceContext->display = openDisplay(params->window);
    if (vaDeviceContext->display == nullptr) {
        // openDisplay() logs the error
        return VA_STATUS_ERROR_INVALID_DISPLAY;
    }

    status = vaInitialize(vaDeviceContext->display, major, minor);
    if (status != VA_STATUS_SUCCESS) {
        // vaInitialize() stores state into the VADisplay even on failure, so we must still
        // call vaTerminate() even if vaInitialize() failed. Similarly, calling vaInitialize()
        // more than once on the same VADisplay can cause resource leaks, even if it failed
        // in the prior call. https://github.com/intel/libva/issues/741
        vaTerminate(vaDeviceContext->display);
        vaDeviceContext->display = nullptr;
    }

    return status;
}

bool
VAAPIRenderer::initialize(PDECODER_PARAMETERS params)
{
    int err;

    m_Window = params->window;
    m_VideoFormat = params->videoFormat;

    m_HwContext = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
    if (!m_HwContext) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to allocate VAAPI context");
        return false;
    }

    AVHWDeviceContext* deviceContext = (AVHWDeviceContext*)m_HwContext->data;
    AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)deviceContext->hwctx;

    int major, minor;
    VAStatus status;
    bool setPathVar = false;

    for (;;) {
        // vaInitialize() will return the libva library version even if the function
        // fails. This has been the case since libva v2.6 from 5 years ago. This
        // doesn't seem to be documented anywhere, so we will be conservative to
        // protect against changes in libva behavior by reinitializing major/minor
        // each time and clamping it to the valid range of versions based upon
        // the version of libva that we compiled with.
        major = minor = 0;
        status = tryVaInitialize(vaDeviceContext, params, &major, &minor);
        if (status != VA_STATUS_SUCCESS) {
            major = std::max(major, VA_MAJOR_VERSION);
            minor = std::max(minor, VA_MINOR_VERSION);

            // If LIBVA_DRIVER_NAME has not been set manually and we're running a
            // version of libva less than 2.20, we'll try our own fallback names.
            // Beginning in libva 2.20, the driver name detection code is much
            // more robust than earlier versions and it includes DRI3 support for
            // driver name detection under Xwayland.
            if (!qEnvironmentVariableIsEmpty("LIBVA_DRIVER_NAME")) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Skipping VAAPI fallback driver names due to LIBVA_DRIVER_NAME");
            }
            else if (major > 1 || (major == 1 && minor >= 20)) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Skipping VAAPI fallback driver names on libva 2.20+");
            }
            else {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Trying fallback VAAPI driver names");

                // It would be nice to use vaSetDriverName() here, but there's no way to unset
                // it and get back to the default driver selection logic once we've overridden
                // the driver name using that API. As a result, we must use LIBVA_DRIVER_NAME.

                if (status != VA_STATUS_SUCCESS) {
                    // The iHD driver supports newer hardware like Ice Lake and Comet Lake.
                    // It should be picked by default on those platforms, but that doesn't
                    // always seem to be the case for some reason.
                    qputenv("LIBVA_DRIVER_NAME", "iHD");
                    status = tryVaInitialize(vaDeviceContext, params, &major, &minor);
                }

                if (status != VA_STATUS_SUCCESS) {
                    // The Iris driver in Mesa 20.0 returns a bogus VA driver (iris_drv_video.so)
                    // even though the correct driver is still i965. If we hit this path, we'll
                    // explicitly try i965 to handle this case.
                    qputenv("LIBVA_DRIVER_NAME", "i965");
                    status = tryVaInitialize(vaDeviceContext, params, &major, &minor);
                }

                if (status != VA_STATUS_SUCCESS) {
                    // The RadeonSI driver is compatible with XWayland but can't be detected by libva
                    // so try it too if all else fails.
                    qputenv("LIBVA_DRIVER_NAME", "radeonsi");
                    status = tryVaInitialize(vaDeviceContext, params, &major, &minor);
                }

                if (status != VA_STATUS_SUCCESS && (m_WindowSystem != SDL_SYSWM_X11 || m_DecoderSelectionPass > 0)) {
                    // The unofficial nvidia VAAPI driver over NVDEC/CUDA works well on Wayland,
                    // but we'd rather use CUDA for XWayland and VDPAU for regular X11.
                    // NB: Remember to update the VA-API NVDEC condition below when modifying this!
                    qputenv("LIBVA_DRIVER_NAME", "nvidia");
                    status = tryVaInitialize(vaDeviceContext, params, &major, &minor);
                }

                if (status != VA_STATUS_SUCCESS) {
                    // Unset LIBVA_DRIVER_NAME if none of the drivers we tried worked. This ensures
                    // we will get a fresh start using the default driver selection behavior after
                    // setting LIBVA_DRIVERS_PATH in the code below.
                    qunsetenv("LIBVA_DRIVER_NAME");
                }
            }
        }

        if (status == VA_STATUS_SUCCESS) {
            // Success!
            break;
        }

#if defined(APP_IMAGE) || defined(USE_FALLBACK_DRIVER_PATHS)
        // AppImages will be running with our libva.so which means they don't know about
        // distro-specific driver paths. To avoid failing in this scenario, we'll hardcode
        // some such paths here for common distros. Non-AppImage packaging mechanisms won't
        // need this fallback because either:
        // a) They are using both distro libva.so and distro libva drivers (native packages)
        // b) They are using both runtime libva.so and runtime libva drivers (Flatpak/Snap)
        if (qEnvironmentVariableIsEmpty("LIBVA_DRIVERS_PATH")) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Trying fallback VAAPI driver paths");

            qputenv("LIBVA_DRIVERS_PATH",
        #if Q_PROCESSOR_WORDSIZE == 8
                    "/usr/lib64/dri-nonfree:" // Fedora x86_64
                    "/usr/lib64/dri-freeworld:" // Fedora x86_64
                    "/usr/lib64/dri:" // Fedora x86_64
                    "/usr/lib64/va/drivers:" // Gentoo x86_64
        #endif
                    "/usr/lib/dri:" // Arch i386 & x86_64, Fedora i386
                    "/usr/lib/va/drivers:" // Gentoo i386
        #if defined(Q_PROCESSOR_X86_64)
                    "/usr/lib/x86_64-linux-gnu/dri:" // Ubuntu/Debian x86_64
        #elif defined(Q_PROCESSOR_X86_32)
                    "/usr/lib/i386-linux-gnu/dri:" // Ubuntu/Debian i386
        #endif
                    );
           setPathVar = true;
        }
        else
#endif
        {
            if (setPathVar) {
                // Unset LIBVA_DRIVERS_PATH if we set it ourselves
                // and we didn't find any working VAAPI drivers.
                qunsetenv("LIBVA_DRIVERS_PATH");
            }

            // Give up
            break;
        }
    }

    if (status != VA_STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to initialize VAAPI: %d",
                     status);
        m_InitFailureReason = InitFailureReason::NoSoftwareSupport;
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Initialized VAAPI %d.%d",
                major, minor);

    const char* vendorString = vaQueryVendorString(vaDeviceContext->display);
    QString vendorStr(vendorString);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Driver: %s",
                vendorString ? vendorString : "<unknown>");

    // This is the libva-vdpau-driver which is not supported by our VAAPI renderer.
    if (vendorStr.contains("Splitted-Desktop Systems VDPAU backend for VA-API")) {
        // Fail and let our VDPAU renderer pick this up
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Avoiding VDPAU wrapper for VAAPI decoding");
        m_InitFailureReason = InitFailureReason::NoSoftwareSupport;
        return false;
    }

    // The Snap (core22) and Focal/Jammy Mesa drivers have a bug that causes
    // a large amount of video latency when using more than one reference frame
    // and severe rendering glitches on my Ryzen 3300U system.
    m_HasRfiLatencyBug = vendorStr.contains("Gallium", Qt::CaseInsensitive) && qgetenv("IGNORE_RFI_LATENCY_BUG") != "1";
    if (m_HasRfiLatencyBug) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "VAAPI driver is affected by RFI latency bug");
    }

    if (m_DecoderSelectionPass == 0 && qgetenv("FORCE_VAAPI") != "1") {
        // Older versions of the Gallium VAAPI driver have a nasty memory leak that
        // causes memory to be leaked for each submitted frame. I believe this is
        // resolved in the libva2 drivers (VAAPI 1.x). We will try to use VDPAU
        // instead for old VAAPI versions or drivers affected by the RFI latency bug
        // as long as we're not streaming HDR (which is unsupported by VDPAU).
        if ((major == 0 || (m_HasRfiLatencyBug && !(m_VideoFormat & VIDEO_FORMAT_MASK_10BIT))) &&
                vendorStr.contains("Gallium", Qt::CaseInsensitive)) {
            // Fail and let VDPAU pick this up
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Deprioritizing VAAPI on Gallium driver. Set FORCE_VAAPI=1 to override.");
            return false;
        }

        // Prefer CUDA for XWayland and VDPAU for regular X11.
        if (m_WindowSystem == SDL_SYSWM_X11 && vendorStr.contains("VA-API NVDEC", Qt::CaseInsensitive)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Deprioritizing VAAPI for NVIDIA driver on X11/XWayland. Set FORCE_VAAPI=1 to override.");
            return false;
        }
    }

    if (WMUtils::isRunningWayland()) {
        // The iHD VAAPI driver can initialize on XWayland but it crashes in
        // vaPutSurface() so we must also not directly render on XWayland.
        m_BlacklistedForDirectRendering = vendorStr.contains("iHD");
    }

    m_RequiresExplicitPixelFormat = vendorStr.contains("i965");

    // This will populate the driver_quirks
    err = av_hwdevice_ctx_init(m_HwContext);
    if (err < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to initialize VAAPI context: %d",
                     err);
        m_InitFailureReason = InitFailureReason::NoSoftwareSupport;
        return false;
    }

    // Allocate mutex to synchronize overlay updates and rendering
    m_OverlayMutex = SDL_CreateMutex();
    if (m_OverlayMutex == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create overlay mutex");
        return false;
    }

    unsigned int formatCount = vaMaxNumSubpictureFormats(vaDeviceContext->display);
    if (formatCount != 0) {
        auto formats = new VAImageFormat[formatCount];
        auto flags = new unsigned int[formatCount];

        status = vaQuerySubpictureFormats(vaDeviceContext->display, formats, flags, &formatCount);
        if (status == VA_STATUS_SUCCESS) {
            for (unsigned int i = 0; i < formatCount; i++) {
                // Format must have 32-bit color depth
                if (formats[i].depth != 32) {
                    continue;
                }

                // Select an RGB format with alpha
                if (formats[i].byte_order == VA_MSB_FIRST) {
                    switch (formats[i].fourcc) {
                    case VA_FOURCC_RGBA:
                        m_OverlaySdlPixelFormat = SDL_PIXELFORMAT_RGBA8888;
                        break;
                    case VA_FOURCC_ARGB:
                        m_OverlaySdlPixelFormat = SDL_PIXELFORMAT_ARGB8888;
                        break;
                    case VA_FOURCC_BGRA:
                        m_OverlaySdlPixelFormat = SDL_PIXELFORMAT_BGRA8888;
                        break;
                    case VA_FOURCC_ABGR:
                        m_OverlaySdlPixelFormat = SDL_PIXELFORMAT_ABGR8888;
                        break;
                    default:
                        continue;
                    }
                }
                else {
                    SDL_assert(formats[i].byte_order == VA_LSB_FIRST);
                    switch (formats[i].fourcc) {
                    case VA_FOURCC_RGBA:
                        m_OverlaySdlPixelFormat = SDL_PIXELFORMAT_ABGR8888;
                        break;
                    case VA_FOURCC_ARGB:
                        m_OverlaySdlPixelFormat = SDL_PIXELFORMAT_BGRA8888;
                        break;
                    case VA_FOURCC_BGRA:
                        m_OverlaySdlPixelFormat = SDL_PIXELFORMAT_ARGB8888;
                        break;
                    case VA_FOURCC_ABGR:
                        m_OverlaySdlPixelFormat = SDL_PIXELFORMAT_RGBA8888;
                        break;
                    default:
                        continue;
                    }
                }

                // If we made it here, we found a format that works for us
                m_OverlayFormat = formats[i];

                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Selected overlay subpicture format: %c%c%c%c8888",
                            (m_OverlayFormat.fourcc >> 0) & 0xff,
                            (m_OverlayFormat.fourcc >> 8) & 0xff,
                            (m_OverlayFormat.fourcc >> 16) & 0xff,
                            (m_OverlayFormat.fourcc >> 24) & 0xff);
                break;
            }
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "vaQuerySubpictureFormats() failed: %d",
                         status);
        }

        delete[] formats;
        delete[] flags;
    }

    return true;
}

bool
VAAPIRenderer::prepareDecoderContext(AVCodecContext* context, AVDictionary**)
{
    context->hw_device_ctx = av_buffer_ref(m_HwContext);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Using VAAPI accelerated renderer on %s",
                SDL_GetCurrentVideoDriver());

    return true;
}

bool
VAAPIRenderer::needsTestFrame()
{
    // We need a test frame to see if this VAAPI driver
    // supports the profile used for streaming
    return true;
}

bool
VAAPIRenderer::isDirectRenderingSupported()
{
    if (qgetenv("VAAPI_FORCE_DIRECT") == "1") {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Using direct rendering due to environment variable");
        return true;
    }
    else if (qgetenv("VAAPI_FORCE_INDIRECT") == "1") {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Using indirect rendering due to environment variable");
        return false;
    }

    // We only support direct rendering on X11 with VAEntrypointVideoProc support
    if (m_WindowSystem != SDL_SYSWM_X11 || m_BlacklistedForDirectRendering) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Using indirect rendering due to WM or blacklist");
        return false;
    }
    else if (m_VideoFormat & VIDEO_FORMAT_MASK_10BIT) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Using indirect rendering for 10-bit video");
        return false;
    }
    else if (m_VideoFormat & VIDEO_FORMAT_MASK_YUV444) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Using indirect rendering for YUV 4:4:4 video");
        return false;
    }

    AVHWDeviceContext* deviceContext = (AVHWDeviceContext*)m_HwContext->data;
    AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)deviceContext->hwctx;
    std::vector<VAEntrypoint> entrypoints(vaMaxNumEntrypoints(vaDeviceContext->display));
    int entrypointCount;
    VAStatus status = vaQueryConfigEntrypoints(vaDeviceContext->display, VAProfileNone, entrypoints.data(), &entrypointCount);
    if (status == VA_STATUS_SUCCESS) {
        for (int i = 0; i < entrypointCount; i++) {
            // Without VAEntrypointVideoProc support, the driver will crash inside vaPutSurface()
            if (entrypoints[i] == VAEntrypointVideoProc) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Using direct rendering with VAEntrypointVideoProc");

                if (m_OverlayFormat.fourcc == 0) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Unable to find supported subpicture format. Overlays will be unavailable!");
                }

                return true;
            }
        }
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Using indirect rendering due to lack of VAEntrypointVideoProc");
    return false;
}

int VAAPIRenderer::getDecoderColorspace()
{
    // Gallium drivers don't support Rec 709 yet - https://gitlab.freedesktop.org/mesa/mesa/issues/1915
    // Intel-vaapi-driver defaults to Rec 601 - https://github.com/intel/intel-vaapi-driver/blob/021bcb79d1bd873bbd9fbca55f40320344bab866/src/i965_output_dri.c#L186
    return COLORSPACE_REC_601;
}

int VAAPIRenderer::getDecoderCapabilities()
{
    int caps = 0;

    if (!m_HasRfiLatencyBug) {
        caps |= CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC |
                CAPABILITY_REFERENCE_FRAME_INVALIDATION_AV1;
    }

    return caps;
}

void VAAPIRenderer::notifyOverlayUpdated(Overlay::OverlayType type)
{
    AVHWDeviceContext* deviceContext = (AVHWDeviceContext*)m_HwContext->data;
    AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)deviceContext->hwctx;
    VAStatus status;

    if (m_OverlayFormat.fourcc == 0) {
        // We already logged for this in isDirectRenderingSupported()
        return;
    }

    SDL_Surface* newSurface = Session::get()->getOverlayManager().getUpdatedOverlaySurface(type);
    bool overlayEnabled = Session::get()->getOverlayManager().isOverlayEnabled(type);
    if (newSurface == nullptr && overlayEnabled) {
        // There's no updated surface and the overlay is enabled, so just leave the old surface alone.
        return;
    }

    // Destroy the old image and subpicture
    // NB: The mutex ensures the overlay is not currently being read for rendering.
    // NB 2: It is safe to unlock here because this thread is the only surface producer.
    SDL_LockMutex(m_OverlayMutex);
    VAImageID oldImageId = m_OverlayImage[type].image_id;
    SDL_zero(m_OverlayImage[type]);

    VASubpictureID oldSubpictureId = m_OverlaySubpicture[type];
    m_OverlaySubpicture[type] = 0;
    SDL_UnlockMutex(m_OverlayMutex);

    if (oldSubpictureId != 0) {
        status = vaDestroySubpicture(vaDeviceContext->display, oldSubpictureId);
        if (status != VA_STATUS_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "vaDestroySubpicture() failed: %d",
                         status);
        }
    }
    if (oldImageId != 0) {
        status = vaDestroyImage(vaDeviceContext->display, oldImageId);
        if (status != VA_STATUS_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "vaDestroyImage() failed: %d",
                         status);
        }
    }

    if (!overlayEnabled) {
        SDL_FreeSurface(newSurface);
        return;
    }

    if (newSurface != nullptr) {
        VAImage newImage;

        SDL_assert(!SDL_MUSTLOCK(newSurface));

        status = vaCreateImage(vaDeviceContext->display, &m_OverlayFormat, newSurface->w, newSurface->h, &newImage);
        if (status != VA_STATUS_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "vaCreateImage() failed: %d",
                         status);
            SDL_FreeSurface(newSurface);
            return;
        }

        void* imagePixels;
        status = vaMapBuffer(vaDeviceContext->display, newImage.buf, &imagePixels);
        if (status != VA_STATUS_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "vaMapBuffer() failed: %d",
                         status);
            SDL_FreeSurface(newSurface);
            vaDestroyImage(vaDeviceContext->display, newImage.image_id);
            return;
        }

        // Convert the surface to the proper format for the VAImage
        SDL_ConvertPixels(newSurface->w, newSurface->h, newSurface->format->format,
                          newSurface->pixels, newSurface->pitch, m_OverlaySdlPixelFormat,
                          imagePixels, (int)newImage.pitches[0]);

        status = vaUnmapBuffer(vaDeviceContext->display, newImage.buf);
        if (status != VA_STATUS_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "vaUnmapBuffer() failed: %d",
                         status);
            SDL_FreeSurface(newSurface);
            vaDestroyImage(vaDeviceContext->display, newImage.image_id);
            return;
        }

        SDL_Rect overlayRect;

        if (type == Overlay::OverlayStatusUpdate) {
            // Bottom Left
            overlayRect.x = 0;
            overlayRect.y = -newSurface->h;
        }
        else if (type == Overlay::OverlayDebug) {
            // Top left
            overlayRect.x = 0;
            overlayRect.y = 0;
        }

        overlayRect.w = newSurface->w;
        overlayRect.h = newSurface->h;

        // Surface data is no longer needed
        SDL_FreeSurface(newSurface);

        VASubpictureID newSubpicture;
        status = vaCreateSubpicture(vaDeviceContext->display, newImage.image_id, &newSubpicture);
        if (status != VA_STATUS_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "vaCreateSubpicture() failed: %d",
                         status);
            vaDestroyImage(vaDeviceContext->display, newImage.image_id);
            return;
        }

        SDL_LockMutex(m_OverlayMutex);
        m_OverlayImage[type] = newImage;
        m_OverlaySubpicture[type] = newSubpicture;
        m_OverlayRect[type] = overlayRect;
        SDL_UnlockMutex(m_OverlayMutex);
    }
}

bool VAAPIRenderer::notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO info)
{
    // We can transparently handle size and display changes
    return !(info->stateChangeFlags & ~(WINDOW_STATE_CHANGE_SIZE | WINDOW_STATE_CHANGE_DISPLAY));
}

void
VAAPIRenderer::renderFrame(AVFrame* frame)
{
    VASurfaceID surface = (VASurfaceID)(uintptr_t)frame->data[3];
    AVHWDeviceContext* deviceContext = (AVHWDeviceContext*)m_HwContext->data;
    AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)deviceContext->hwctx;

    int windowWidth, windowHeight;
    SDL_GetWindowSize(m_Window, &windowWidth, &windowHeight);

    SDL_Rect src, dst;
    src.x = src.y = 0;
    src.w = frame->width;
    src.h = frame->height;
    dst.x = dst.y = 0;
    dst.w = windowWidth;
    dst.h = windowHeight;

    StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

    if (m_WindowSystem == SDL_SYSWM_X11) {
#ifdef HAVE_LIBVA_X11
        unsigned int flags = 0;

        // NB: Not all VAAPI drivers respect these flags. Many drivers
        // just ignore them and do the color conversion as Rec 601.
        switch (getFrameColorspace(frame)) {
        case COLORSPACE_REC_709:
            flags |= VA_SRC_BT709;
            break;
        case COLORSPACE_REC_601:
            flags |= VA_SRC_BT601;
            break;
        default:
            // Unsupported colorspace
            SDL_assert(false);
            break;
        }

        SDL_LockMutex(m_OverlayMutex);

        VAImage associatedOverlayImages[Overlay::OverlayMax] = {};
        VASubpictureID associatedOverlaySubpictures[Overlay::OverlayMax] = {};

        // Associate our overlay subpictures to the current surface
        for (int type = 0; type < Overlay::OverlayMax; type++) {
            VAStatus status;

            if (m_OverlaySubpicture[type] == 0) {
                continue;
            }

            SDL_Rect overlayRect = m_OverlayRect[type];

            // Negative values are relative to the other side of the window
            if (overlayRect.x < 0) {
                overlayRect.x += windowWidth;
            }
            if (overlayRect.y < 0) {
                overlayRect.y += windowHeight;
            }

            status = vaAssociateSubpicture(vaDeviceContext->display,
                                           m_OverlaySubpicture[type],
                                           &surface,
                                           1,
                                           0,
                                           0,
                                           m_OverlayImage[type].width,
                                           m_OverlayImage[type].height,
                                           overlayRect.x,
                                           overlayRect.y,
                                           overlayRect.w,
                                           overlayRect.h,
                                           0);
            if (status == VA_STATUS_SUCCESS) {
                // Take temporary ownership of the overlay to prevent notifyOverlayUpdated()
                // from freeing them frum underneath us. We need to release the lock while
                // we render for performance reasons.
                associatedOverlayImages[type] = m_OverlayImage[type];
                associatedOverlaySubpictures[type] = m_OverlaySubpicture[type];

                SDL_zero(m_OverlayImage[type]);
                m_OverlaySubpicture[type] = 0;
            }
            else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "vaAssociateSubpicture() failed: %d",
                             status);
            }
        }

        SDL_UnlockMutex(m_OverlayMutex);

        // This will draw the surface and any associated subpictures
        // NB: This can take a full VBlank period to complete!
        vaPutSurface(vaDeviceContext->display,
                     surface,
                     m_XWindow,
                     0, 0,
                     frame->width, frame->height,
                     dst.x, dst.y,
                     dst.w, dst.h,
                     NULL, 0, flags);

        SDL_LockMutex(m_OverlayMutex);

        // Now that we've reacquired the lock, we will need to reconcile the current
        // state of the overlay with our saved state from before we unlocked.
        for (int type = 0; type < Overlay::OverlayMax; type++) {
            VAStatus status;

            if (associatedOverlaySubpictures[type] == 0) {
                continue;
            }

            // Deassociate the subpicture so it can be safely destroyed/replaced
            status = vaDeassociateSubpicture(vaDeviceContext->display, associatedOverlaySubpictures[type], &surface, 1);
            if (status != VA_STATUS_SUCCESS) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "vaDeassociateSubpicture() failed: %d",
                             status);
            }

            // If a new subpicture was populated while we were unlocked, free the old one we took ownership of
            if (m_OverlaySubpicture[type] != 0) {
                status = vaDestroySubpicture(vaDeviceContext->display, associatedOverlaySubpictures[type]);
                if (status != VA_STATUS_SUCCESS) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "vaDestroySubpicture() failed: %d",
                                 status);
                }
            }
            else {
                // If no new subpicture was populated, return ownership of this one
                m_OverlaySubpicture[type] = associatedOverlaySubpictures[type];
            }

            // If a new image was populated while we were unlocked, free the one old we took ownership of
            if (m_OverlayImage[type].image_id != 0) {
                status = vaDestroyImage(vaDeviceContext->display, associatedOverlayImages[type].image_id);
                if (status != VA_STATUS_SUCCESS) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "vaDestroyImage() failed: %d",
                                 status);
                }
            }
            else {
                // If no new image was populated, return ownership of this one
                m_OverlayImage[type] = associatedOverlayImages[type];
            }
        }

        SDL_UnlockMutex(m_OverlayMutex);
#endif
    }
    else if (m_WindowSystem == SDL_SYSWM_WAYLAND) {
        // We don't support direct rendering on Wayland, so we should
        // never get called there. Many common Wayland compositors don't
        // support YUV surfaces, so direct rendering would fail.
        SDL_assert(false);
    }
    else {
        // We don't accept anything else in initialize().
        SDL_assert(false);
    }
}

#if defined(HAVE_EGL) || defined(HAVE_DRM)

// Ensure that vaExportSurfaceHandle() is supported by the VA-API driver
bool
VAAPIRenderer::canExportSurfaceHandle(int layerTypeFlag, VADRMPRIMESurfaceDescriptor* descriptor) {
    AVHWDeviceContext* deviceContext = (AVHWDeviceContext*)m_HwContext->data;
    AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)deviceContext->hwctx;
    VASurfaceID surfaceId;
    VAStatus st;
    VASurfaceAttrib attrs[2];
    int attributeCount = 0;

    if (qgetenv("VAAPI_FORCE_DIRECT") == "1") {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Using direct rendering due to environment variable");
        return false;
    }

    // FFmpeg handles setting these quirk flags for us
    if (!(vaDeviceContext->driver_quirks & AV_VAAPI_DRIVER_QUIRK_ATTRIB_MEMTYPE)) {
        attrs[attributeCount].type = VASurfaceAttribMemoryType;
        attrs[attributeCount].flags = VA_SURFACE_ATTRIB_SETTABLE;
        attrs[attributeCount].value.type = VAGenericValueTypeInteger;
        attrs[attributeCount].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_VA;
        attributeCount++;
    }

    // These attributes are required for i965 to create a surface that can
    // be successfully exported via vaExportSurfaceHandle(). YUV444 is not
    // handled here but i965 supports no hardware with YUV444 decoding.
    if (m_RequiresExplicitPixelFormat && !(m_VideoFormat & VIDEO_FORMAT_MASK_YUV444)) {
        attrs[attributeCount].type = VASurfaceAttribPixelFormat;
        attrs[attributeCount].flags = VA_SURFACE_ATTRIB_SETTABLE;
        attrs[attributeCount].value.type = VAGenericValueTypeInteger;
        attrs[attributeCount].value.value.i =
            (m_VideoFormat & VIDEO_FORMAT_MASK_10BIT) ? VA_FOURCC_P010 : VA_FOURCC_NV12;
        attributeCount++;
    }

    st = vaCreateSurfaces(vaDeviceContext->display,
                          (m_VideoFormat & VIDEO_FORMAT_MASK_10BIT) ?
                              ((m_VideoFormat & VIDEO_FORMAT_MASK_YUV444) ? VA_RT_FORMAT_YUV444_10 : VA_RT_FORMAT_YUV420_10) :
                              ((m_VideoFormat & VIDEO_FORMAT_MASK_YUV444) ? VA_RT_FORMAT_YUV444 : VA_RT_FORMAT_YUV420),
                          1280,
                          720,
                          &surfaceId,
                          1,
                          attrs,
                          attributeCount);
    if (st != VA_STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "vaCreateSurfaces() failed: %d", st);
        return false;
    }

    st = vaExportSurfaceHandle(vaDeviceContext->display,
                               surfaceId,
                               VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                               VA_EXPORT_SURFACE_READ_ONLY | layerTypeFlag,
                               descriptor);

    vaDestroySurfaces(vaDeviceContext->display, &surfaceId, 1);

    if (st != VA_STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "vaExportSurfaceHandle() failed: %d", st);
        return false;
    }

    for (size_t i = 0; i < descriptor->num_objects; ++i) {
        close(descriptor->objects[i].fd);
        descriptor->objects[i].fd = -1;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "VAAPI driver supports exporting DRM PRIME surface handles with %s layers",
                layerTypeFlag == VA_EXPORT_SURFACE_COMPOSED_LAYERS ? "composed" : "separate");
    return true;
}

#endif

#ifdef HAVE_EGL

bool
VAAPIRenderer::canExportEGL() {
    VADRMPRIMESurfaceDescriptor descriptor;

    return (qgetenv("VAAPI_EGL_SEPARATE_LAYERS") != "1" && canExportSurfaceHandle(VA_EXPORT_SURFACE_COMPOSED_LAYERS, &descriptor)) ||
           canExportSurfaceHandle(VA_EXPORT_SURFACE_SEPARATE_LAYERS, &descriptor);
}

AVPixelFormat VAAPIRenderer::getEGLImagePixelFormat() {
    switch (m_EglExportType) {
    case EglExportType::Separate:
        // YUV444 surfaces can be in a variety of different formats, so we need to
        // use the composed export that returns an opaque format-agnostic texture.
        SDL_assert(!(m_VideoFormat & VIDEO_FORMAT_MASK_YUV444));
        return (m_VideoFormat & VIDEO_FORMAT_MASK_10BIT) ?
                   AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;

    case EglExportType::Composed:
        // This tells EGLRenderer to treat the EGLImage as a single opaque texture
        return AV_PIX_FMT_DRM_PRIME;

    case EglExportType::Unknown:
        SDL_assert(m_EglExportType != EglExportType::Unknown);
        break;
    }

    return AV_PIX_FMT_NONE;
}

bool
VAAPIRenderer::initializeEGL(EGLDisplay dpy,
                             const EGLExtensions &ext) {
    VADRMPRIMESurfaceDescriptor descriptor;

    if (!m_EglImageFactory.initializeEGL(dpy, ext)) {
        return false;
    }

    // Prefer exporting composed images absent a user override or lack of support for exporting or importing
    if (qgetenv("VAAPI_EGL_SEPARATE_LAYERS") == "1") {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Exporting separate layers due to environment variable override");
        m_EglExportType = EglExportType::Separate;
    }
    else if (!canExportSurfaceHandle(VA_EXPORT_SURFACE_COMPOSED_LAYERS, &descriptor)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Exporting separate layers due to lack of support for VA_EXPORT_SURFACE_COMPOSED_LAYERS");
        m_EglExportType = EglExportType::Separate;
    }
    else if (!m_EglImageFactory.supportsImportingFormat(dpy, descriptor.layers[0].drm_format)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Exporting separate layers due to lack of support for importing format: %08x", descriptor.layers[0].drm_format);
        m_EglExportType = EglExportType::Separate;
    }
    else if (!m_EglImageFactory.supportsImportingModifier(dpy, descriptor.layers[0].drm_format, descriptor.objects[0].drm_format_modifier)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Exporting separate layers due to lack of support for importing format and modifier: %08x %016" PRIx64,
                    descriptor.layers[0].drm_format,
                    descriptor.objects[0].drm_format_modifier);
        m_EglExportType = EglExportType::Separate;
    }
    else {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Exporting composed layers with format and modifier: %08x %016" PRIx64,
                    descriptor.layers[0].drm_format,
                    descriptor.objects[0].drm_format_modifier);
        m_EglExportType = EglExportType::Composed;
    }

    // Let's probe for EGL import support on separate layers too, but only warn if it's not supported
    if (m_EglExportType == EglExportType::Separate) {
        if (!canExportSurfaceHandle(VA_EXPORT_SURFACE_SEPARATE_LAYERS, &descriptor)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Exporting separate layers is not supported by the VAAPI driver");
            return false;
        }

        for (uint32_t i = 0; i < descriptor.num_layers; i++) {
            if (!m_EglImageFactory.supportsImportingFormat(dpy, descriptor.layers[i].drm_format)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "EGL implementation lacks support for importing format: %08x", descriptor.layers[0].drm_format);
            }
            else if (!m_EglImageFactory.supportsImportingModifier(dpy, descriptor.layers[i].drm_format,
                                                                  descriptor.objects[descriptor.layers[i].object_index[0]].drm_format_modifier)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "EGL implementation lacks support for importing format and modifier: %08x %016" PRIx64,
                            descriptor.layers[i].drm_format,
                            descriptor.objects[descriptor.layers[i].object_index[0]].drm_format_modifier);
            }
        }
    }

    return true;
}

ssize_t
VAAPIRenderer::exportEGLImages(AVFrame *frame, EGLDisplay dpy,
                               EGLImage images[EGL_MAX_PLANES]) {
    ssize_t count;
    uint32_t exportFlags = VA_EXPORT_SURFACE_READ_ONLY;

    switch (m_EglExportType) {
    case EglExportType::Separate:
        exportFlags |= VA_EXPORT_SURFACE_SEPARATE_LAYERS;
        break;
    case EglExportType::Composed:
        exportFlags |= VA_EXPORT_SURFACE_COMPOSED_LAYERS;
        break;
    case EglExportType::Unknown:
        SDL_assert(m_EglExportType != EglExportType::Unknown);
        return -1;
    }

    auto hwFrameCtx = (AVHWFramesContext*)frame->hw_frames_ctx->data;
    AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)hwFrameCtx->device_ctx->hwctx;
    VASurfaceID surface_id = (VASurfaceID)(uintptr_t)frame->data[3];

    VAStatus st = vaExportSurfaceHandle(vaDeviceContext->display,
                                        surface_id,
                                        VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                        exportFlags,
                                        &m_PrimeDescriptor);
    if (st != VA_STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "vaExportSurfaceHandle failed: %d", st);
        return -1;
    }

    st = vaSyncSurface(vaDeviceContext->display, surface_id);
    if (st != VA_STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "vaSyncSurface() failed: %d", st);
        goto fail;
    }

    count = m_EglImageFactory.exportVAImages(frame, &m_PrimeDescriptor, dpy, images);
    if (count < 0) {
        goto fail;
    }

    return count;

fail:
    for (size_t i = 0; i < m_PrimeDescriptor.num_objects; ++i) {
        close(m_PrimeDescriptor.objects[i].fd);
    }
    m_PrimeDescriptor.num_layers = 0;
    m_PrimeDescriptor.num_objects = 0;
    return -1;
}

void
VAAPIRenderer::freeEGLImages(EGLDisplay dpy, EGLImage images[EGL_MAX_PLANES]) {
    m_EglImageFactory.freeEGLImages(dpy, images);
    for (size_t i = 0; i < m_PrimeDescriptor.num_objects; ++i) {
        close(m_PrimeDescriptor.objects[i].fd);
    }
    m_PrimeDescriptor.num_layers = 0;
    m_PrimeDescriptor.num_objects = 0;
}

#endif

#ifdef HAVE_DRM

bool VAAPIRenderer::canExportDrmPrime()
{
    // Our DRM renderer requires composed layers
    VADRMPRIMESurfaceDescriptor descriptor;
    return canExportSurfaceHandle(VA_EXPORT_SURFACE_COMPOSED_LAYERS, &descriptor);
}

bool VAAPIRenderer::mapDrmPrimeFrame(AVFrame* frame, AVDRMFrameDescriptor* drmDescriptor)
{
    auto hwFrameCtx = (AVHWFramesContext*)frame->hw_frames_ctx->data;
    AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)hwFrameCtx->device_ctx->hwctx;
    VASurfaceID vaSurfaceId = (VASurfaceID)(uintptr_t)frame->data[3];
    VADRMPRIMESurfaceDescriptor vaDrmPrimeDescriptor;

    VAStatus st = vaExportSurfaceHandle(vaDeviceContext->display,
                                        vaSurfaceId,
                                        VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                        VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_COMPOSED_LAYERS,
                                        &vaDrmPrimeDescriptor);
    if (st != VA_STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "vaExportSurfaceHandle() failed: %d", st);
        return false;
    }

    st = vaSyncSurface(vaDeviceContext->display, vaSurfaceId);
    if (st != VA_STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "vaSyncSurface() failed: %d", st);
        for (uint32_t i = 0; i < vaDrmPrimeDescriptor.num_objects; i++) {
            close(vaDrmPrimeDescriptor.objects[i].fd);
        }
        return false;
    }

    // Map our VADRMPRIMESurfaceDescriptor to the AVDRMFrameDescriptor our caller wants
    drmDescriptor->nb_objects = vaDrmPrimeDescriptor.num_objects;
    for (uint32_t i = 0; i < vaDrmPrimeDescriptor.num_objects; i++) {
        drmDescriptor->objects[i].fd = vaDrmPrimeDescriptor.objects[i].fd;
        drmDescriptor->objects[i].size = vaDrmPrimeDescriptor.objects[i].size;
        drmDescriptor->objects[i].format_modifier = vaDrmPrimeDescriptor.objects[i].drm_format_modifier;
    }
    drmDescriptor->nb_layers = vaDrmPrimeDescriptor.num_layers;
    for (uint32_t i = 0; i < vaDrmPrimeDescriptor.num_layers; i++) {
        drmDescriptor->layers[i].format = vaDrmPrimeDescriptor.layers[i].drm_format;
        drmDescriptor->layers[i].nb_planes = vaDrmPrimeDescriptor.layers[i].num_planes;
        for (uint32_t j = 0; j < vaDrmPrimeDescriptor.layers[i].num_planes; j++) {
            drmDescriptor->layers[i].planes[j].object_index = vaDrmPrimeDescriptor.layers[i].object_index[j];
            drmDescriptor->layers[i].planes[j].offset = vaDrmPrimeDescriptor.layers[i].offset[j];
            drmDescriptor->layers[i].planes[j].pitch = vaDrmPrimeDescriptor.layers[i].pitch[j];
        }
    }

    return true;
}

void VAAPIRenderer::unmapDrmPrimeFrame(AVDRMFrameDescriptor* drmDescriptor)
{
    for (int i = 0; i < drmDescriptor->nb_objects; i++) {
        close(drmDescriptor->objects[i].fd);
    }
}

#endif
