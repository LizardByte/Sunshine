#include "streamutils.h"

#include <Qt>
#include <QDir>

#ifdef Q_OS_DARWIN
#include <ApplicationServices/ApplicationServices.h>
#endif

#ifdef Q_OS_WINDOWS
#include <Windows.h>
#endif

#ifdef Q_OS_UNIX
#include <unistd.h>
#include <fcntl.h>

#include <SDL_syswm.h>
#endif

#ifdef Q_OS_LINUX
#include <sys/auxv.h>

#if defined(Q_PROCESSOR_ARM)

#ifndef HWCAP2_AES
#define HWCAP2_AES (1 << 0)
#endif

#elif defined(Q_PROCESSOR_RISCV)

#if __has_include(<sys/hwprobe.h>)
#include <sys/hwprobe.h>
#else
#include <unistd.h>

#if __has_include(<asm/hwprobe.h>)
#include <asm/hwprobe.h>
#include <sys/syscall.h>
#else
#define __NR_riscv_hwprobe 258
struct riscv_hwprobe {
    int64_t key;
    uint64_t value;
};
#define RISCV_HWPROBE_KEY_IMA_EXT_0 4
#endif

// RISC-V Scalar AES [E]ncryption and [D]ecryption
#ifndef RISCV_HWPROBE_EXT_ZKND
#define RISCV_HWPROBE_EXT_ZKND (1 << 11)
#define RISCV_HWPROBE_EXT_ZKNE (1 << 12)
#endif

// RISC-V Vector AES
#ifndef RISCV_HWPROBE_EXT_ZVKNED
#define RISCV_HWPROBE_EXT_ZVKNED (1 << 21)
#endif

static int __riscv_hwprobe(struct riscv_hwprobe *pairs, size_t pair_count,
                           size_t cpu_count, unsigned long *cpus,
                           unsigned int flags)
{
    return syscall(__NR_riscv_hwprobe, pairs, pair_count, cpu_count, cpus, flags);
}

#endif
#endif
#endif

#if defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD)
#include <sys/auxv.h>
#endif

Uint32 StreamUtils::getPlatformWindowFlags()
{
#if defined(Q_OS_DARWIN)
    return SDL_WINDOW_METAL;
#elif defined(HAVE_LIBPLACEBO_VULKAN)
    // We'll fall back to GL if Vulkan fails
    return SDL_WINDOW_VULKAN;
#else
    return 0;
#endif
}

void StreamUtils::scaleSourceToDestinationSurface(SDL_Rect* src, SDL_Rect* dst)
{
    int dstH = SDL_ceilf((float)dst->w * src->h / src->w);
    int dstW = SDL_ceilf((float)dst->h * src->w / src->h);

    if (dstH > dst->h) {
        dst->x += (dst->w - dstW) / 2;
        dst->w = dstW;
    }
    else {
        dst->y += (dst->h - dstH) / 2;
        dst->h = dstH;
    }
}

void StreamUtils::screenSpaceToNormalizedDeviceCoords(SDL_FRect* rect, int viewportWidth, int viewportHeight)
{
    rect->x = (rect->x / (viewportWidth / 2.0f)) - 1.0f;
    rect->y = (rect->y / (viewportHeight / 2.0f)) - 1.0f;
    rect->w = rect->w / (viewportWidth / 2.0f);
    rect->h = rect->h / (viewportHeight / 2.0f);
}

void StreamUtils::screenSpaceToNormalizedDeviceCoords(SDL_Rect* src, SDL_FRect* dst, int viewportWidth, int viewportHeight)
{
    dst->x = ((float)src->x / (viewportWidth / 2.0f)) - 1.0f;
    dst->y = ((float)src->y / (viewportHeight / 2.0f)) - 1.0f;
    dst->w = (float)src->w / (viewportWidth / 2.0f);
    dst->h = (float)src->h / (viewportHeight / 2.0f);
}

int StreamUtils::getDisplayRefreshRate(SDL_Window* window)
{
    int displayIndex = SDL_GetWindowDisplayIndex(window);
    if (displayIndex < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to get current display: %s",
                     SDL_GetError());

        // Assume display 0 if it fails
        displayIndex = 0;
    }

    SDL_DisplayMode mode;
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN) {
        // Use the window display mode for full-screen exclusive mode
        if (SDL_GetWindowDisplayMode(window, &mode) != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL_GetWindowDisplayMode() failed: %s",
                         SDL_GetError());

            // Assume 60 Hz
            return 60;
        }
    }
    else {
        // Use the current display mode for windowed and borderless
        if (SDL_GetCurrentDisplayMode(displayIndex, &mode) != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL_GetCurrentDisplayMode() failed: %s",
                         SDL_GetError());

            // Assume 60 Hz
            return 60;
        }
    }

    // May be zero if undefined
    if (mode.refresh_rate == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Refresh rate unknown; assuming 60 Hz");
        mode.refresh_rate = 60;
    }

    return mode.refresh_rate;
}

bool StreamUtils::hasFastAes()
{
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#if (__has_builtin(__builtin_cpu_supports) || (defined(__GNUC__) && __GNUC__ >= 6)) && defined(Q_PROCESSOR_X86)
    return __builtin_cpu_supports("aes");
#elif defined(__BUILTIN_CPU_SUPPORTS__) && defined(Q_PROCESSOR_POWER)
    return __builtin_cpu_supports("vcrypto");
#elif defined(Q_OS_WINDOWS) && defined(Q_PROCESSOR_ARM)
    return IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE);
#elif defined(_MSC_VER) && defined(Q_PROCESSOR_X86)
    int regs[4];
    __cpuid(regs, 1);
    return regs[2] & (1 << 25); // AES-NI
#elif defined(Q_OS_DARWIN)
    // Everything that runs Catalina and later has AES-NI or ARMv8 crypto instructions
    return true;
#elif (defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD)) && defined(Q_PROCESSOR_ARM) && QT_POINTER_SIZE == 4
    unsigned long hwcap2 = 0;
    elf_aux_info(AT_HWCAP2, &hwcap2, sizeof(hwcap2));
    return (hwcap2 & HWCAP2_AES);
#elif (defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD)) && defined(Q_PROCESSOR_ARM) && QT_POINTER_SIZE == 8
    unsigned long hwcap = 0;
    elf_aux_info(AT_HWCAP, &hwcap, sizeof(hwcap));
    return (hwcap & HWCAP_AES);
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_ARM) && QT_POINTER_SIZE == 4
    return getauxval(AT_HWCAP2) & HWCAP2_AES;
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_ARM) && QT_POINTER_SIZE == 8
    return getauxval(AT_HWCAP) & HWCAP_AES;
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_RISCV)
    riscv_hwprobe pairs[1] = {
        { RISCV_HWPROBE_KEY_IMA_EXT_0, 0 },
    };

    // If this syscall is not implemented, we'll get -ENOSYS
    // and the value field will remain zero.
    __riscv_hwprobe(pairs, SDL_arraysize(pairs), 0, nullptr, 0);

    return (pairs[0].value & (RISCV_HWPROBE_EXT_ZKNE | RISCV_HWPROBE_EXT_ZKND)) ==
               (RISCV_HWPROBE_EXT_ZKNE | RISCV_HWPROBE_EXT_ZKND) ||
           (pairs[0].value & RISCV_HWPROBE_EXT_ZVKNED);
#elif QT_POINTER_SIZE == 4
    #warning Unknown 32-bit platform. Assuming AES is slow on this CPU.
    return false;
#else
    #warning Unknown 64-bit platform. Assuming AES is fast on this CPU.
    return true;
#endif
}

bool StreamUtils::getNativeDesktopMode(int displayIndex, SDL_DisplayMode* mode, SDL_Rect* safeArea)
{
#ifdef Q_OS_DARWIN
#define MAX_DISPLAYS 16
    CGDirectDisplayID displayIds[MAX_DISPLAYS];
    uint32_t displayCount = 0;
    CGGetActiveDisplayList(MAX_DISPLAYS, displayIds, &displayCount);
    if (displayIndex >= (int)displayCount) {
        return false;
    }

    SDL_zerop(mode);

    // Retina displays have non-native resolutions both below and above (!) their
    // native resolution, so it's impossible for us to figure out what's actually
    // native on macOS using the SDL API alone. We'll talk to CoreGraphics to
    // find the correct resolution and match it in our SDL list.
    CFArrayRef modeList = CGDisplayCopyAllDisplayModes(displayIds[displayIndex], nullptr);
    CFIndex count = CFArrayGetCount(modeList);
    for (CFIndex i = 0; i < count; i++) {
        auto cgMode = (CGDisplayModeRef)(CFArrayGetValueAtIndex(modeList, i));
        if ((CGDisplayModeGetIOFlags(cgMode) & kDisplayModeNativeFlag) != 0) {
            mode->w = static_cast<int>(CGDisplayModeGetWidth(cgMode));
            mode->h = static_cast<int>(CGDisplayModeGetHeight(cgMode));
            break;
        }
    }

    safeArea->x = 0;
    safeArea->y = 0;
    safeArea->w = mode->w;
    safeArea->h = mode->h;

#if TARGET_CPU_ARM64
    // Now that we found the native full-screen mode, let's look for one that matches along
    // the width but not the height and we'll assume that's the safe area full-screen mode.
    //
    // There doesn't appear to be a CG API or flag that will tell us that a given mode
    // is a "safe area" mode, so we have to use our own (brittle) heuristics. :(
    //
    // To avoid potential false positives, let's avoid checking for external displays, since
    // we might have scenarios like a 1920x1200 display with an alternate 1920x1080 mode
    // which would falsely trigger our notch detection here.
    if (CGDisplayIsBuiltin(displayIds[displayIndex])) {
        for (CFIndex i = 0; i < count; i++) {
            auto cgMode = (CGDisplayModeRef)(CFArrayGetValueAtIndex(modeList, i));
            auto cgModeWidth = static_cast<int>(CGDisplayModeGetWidth(cgMode));
            auto cgModeHeight = static_cast<int>(CGDisplayModeGetHeight(cgMode));

            // If the modes differ by more than 100, we'll assume it's not a notch mode
            if (mode->w == cgModeWidth && mode->h != cgModeHeight && mode->h <= cgModeHeight + 100) {
                safeArea->w = cgModeWidth;
                safeArea->h = cgModeHeight;
            }
        }
    }
#endif

    CFRelease(modeList);

    // Special case for probing for notched displays prior to video subsystem initialization
    // in Session::initialize() for Darwin only!
    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        // Now find the SDL mode that matches the CG native mode
        for (int i = 0; i < SDL_GetNumDisplayModes(displayIndex); i++) {
            SDL_DisplayMode thisMode;
            if (SDL_GetDisplayMode(displayIndex, i, &thisMode) == 0) {
                if (thisMode.w == mode->w && thisMode.h == mode->h &&
                    thisMode.refresh_rate >= mode->refresh_rate) {
                    *mode = thisMode;
                    break;
                }
            }
        }
    }
#else
    SDL_assert(SDL_WasInit(SDL_INIT_VIDEO));

    if (displayIndex >= SDL_GetNumVideoDisplays()) {
        return false;
    }

    // We need to get the true display resolution without DPI scaling (since we use High DPI).
    // Windows returns the real display resolution here, even if DPI scaling is enabled.
    // macOS and Wayland report a resolution that includes the DPI scaling factor. Picking
    // the first mode on Wayland will get the native resolution without the scaling factor
    // (and macOS is handled in the #ifdef above).
    if (!strcmp(SDL_GetCurrentVideoDriver(), "wayland")) {
        if (SDL_GetDisplayMode(displayIndex, 0, mode) != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL_GetDisplayMode() failed: %s",
                         SDL_GetError());
            return false;
        }
    }
    else {
        if (SDL_GetDesktopDisplayMode(displayIndex, mode) != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL_GetDesktopDisplayMode() failed: %s",
                         SDL_GetError());
            return false;
        }
    }

    safeArea->x = 0;
    safeArea->y = 0;
    safeArea->w = mode->w;
    safeArea->h = mode->h;
#endif

    return true;
}

int StreamUtils::getDrmFdForWindow(SDL_Window* window, bool* mustClose)
{
    *mustClose = false;

#if defined(SDL_VIDEO_DRIVER_KMSDRM) && SDL_VERSION_ATLEAST(2, 0, 15)
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_GetWindowWMInfo() failed: %s",
                     SDL_GetError());
        return -1;
    }

    if (info.subsystem == SDL_SYSWM_KMSDRM) {
        // If SDL has an FD, share that
        if (info.info.kmsdrm.drm_fd >= 0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Sharing DRM FD with SDL");
            return info.info.kmsdrm.drm_fd;
        }
        else {
            char path[128];
            snprintf(path, sizeof(path), "/dev/dri/card%u", info.info.kmsdrm.dev_index);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Opening DRM FD from SDL by path: %s",
                        path);
            int fd = open(path, O_RDWR | O_CLOEXEC);
            if (fd >= 0) {
                *mustClose = true;
            }
            return fd;
        }
    }
#else
    Q_UNUSED(window);
#endif

    return -1;
}

int StreamUtils::getDrmFd(bool preferRenderNode)
{
#ifdef Q_OS_UNIX
    const char* userDevice = SDL_getenv("DRM_DEV");
    if (userDevice != nullptr) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Opening user-specified DRM device: %s",
                    userDevice);

        return open(userDevice, O_RDWR | O_CLOEXEC);
    }
    else {
        QDir driDir("/dev/dri");
        int fd;

        // We have to explicitly ask for devices to be returned
        driDir.setFilter(QDir::Files | QDir::System);

        if (preferRenderNode) {
            // Try a render node first since we aren't using DRM for output in this codepath
            for (QFileInfo& node : driDir.entryInfoList(QStringList("renderD*"))) {
                QByteArray absolutePath = node.absoluteFilePath().toUtf8();
                fd = open(absolutePath.constData(), O_RDWR | O_CLOEXEC);
                if (fd >= 0) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "Opened DRM render node: %s",
                                absolutePath.constData());
                    return fd;
                }
            }
        }

        // If that fails, try to use a primary node and hope for the best
        for (QFileInfo& node : driDir.entryInfoList(QStringList("card*"))) {
            QByteArray absolutePath = node.absoluteFilePath().toUtf8();
            fd = open(absolutePath.constData(), O_RDWR | O_CLOEXEC);
            if (fd >= 0) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Opened DRM primary node: %s",
                            absolutePath.constData());
                return fd;
            }
        }
    }
#else
    Q_UNUSED(preferRenderNode);
#endif

    return -1;
}

extern QAtomicInt g_AsyncLoggingEnabled;

void StreamUtils::enterAsyncLoggingMode()
{
    g_AsyncLoggingEnabled.ref();
}

void StreamUtils::exitAsyncLoggingMode()
{
    g_AsyncLoggingEnabled.deref();
}
