#include "session.h"
#include "settings/streamingpreferences.h"
#include "streaming/streamutils.h"
#include "backend/richpresencemanager.h"

#include <Limelight.h>
#include "SDL_compat.h"
#include "utils.h"

#ifdef HAVE_FFMPEG
#include "video/ffmpeg.h"
#endif

#ifdef HAVE_SLVIDEO
#include "video/slvid.h"
#endif

#ifdef Q_OS_WIN32
// Scaling the icon down on Win32 looks dreadful, so render at lower res
#define ICON_SIZE 32
#else
#define ICON_SIZE 64
#endif

// HACK: Remove once proper Dark Mode support lands in SDL
#ifdef Q_OS_WIN32
#include <SDL_syswm.h>
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_OLD
#define DWMWA_USE_IMMERSIVE_DARK_MODE_OLD 19
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif


#define SDL_CODE_FLUSH_WINDOW_EVENT_BARRIER 100
#define SDL_CODE_GAMECONTROLLER_RUMBLE 101
#define SDL_CODE_GAMECONTROLLER_RUMBLE_TRIGGERS 102
#define SDL_CODE_GAMECONTROLLER_SET_MOTION_EVENT_STATE 103
#define SDL_CODE_GAMECONTROLLER_SET_CONTROLLER_LED 104
#define SDL_CODE_GAMECONTROLLER_SET_ADAPTIVE_TRIGGERS 105

#include <openssl/rand.h>

#include <QtEndian>
#include <QCoreApplication>
#include <QThreadPool>
#include <QSvgRenderer>
#include <QPainter>
#include <QImage>
#include <QGuiApplication>
#include <QCursor>
#include <QWindow>
#include <QScreen>

#define CONN_TEST_SERVER "qt.conntest.moonlight-stream.org"

CONNECTION_LISTENER_CALLBACKS Session::k_ConnCallbacks = {
    Session::clStageStarting,
    nullptr,
    Session::clStageFailed,
    nullptr,
    Session::clConnectionTerminated,
    Session::clLogMessage,
    Session::clRumble,
    Session::clConnectionStatusUpdate,
    Session::clSetHdrMode,
    Session::clRumbleTriggers,
    Session::clSetMotionEventState,
    Session::clSetControllerLED,
    Session::clSetAdaptiveTriggers
};

Session* Session::s_ActiveSession;
QSemaphore Session::s_ActiveSessionSemaphore(1);

void Session::clStageStarting(int stage)
{
    // We know this is called on the same thread as LiStartConnection()
    // which happens to be the main thread, so it's cool to interact
    // with the GUI in these callbacks.
    emit s_ActiveSession->stageStarting(QString::fromLocal8Bit(LiGetStageName(stage)));
}

void Session::clStageFailed(int stage, int errorCode)
{
    // Perform the port test now, while we're on the async connection thread and not blocking the UI.
    unsigned int portFlags = LiGetPortFlagsFromStage(stage);
    s_ActiveSession->m_PortTestResults = LiTestClientConnectivity(CONN_TEST_SERVER, 443, portFlags);

    char failingPorts[128];
    LiStringifyPortFlags(portFlags, ", ", failingPorts, sizeof(failingPorts));
    emit s_ActiveSession->stageFailed(QString::fromLocal8Bit(LiGetStageName(stage)), errorCode, QString(failingPorts));
}

void Session::clConnectionTerminated(int errorCode)
{
    unsigned int portFlags = LiGetPortFlagsFromTerminationErrorCode(errorCode);
    s_ActiveSession->m_PortTestResults = LiTestClientConnectivity(CONN_TEST_SERVER, 443, portFlags);

    // Display the termination dialog if this was not intended
    switch (errorCode) {
    case ML_ERROR_GRACEFUL_TERMINATION:
        break;

    case ML_ERROR_NO_VIDEO_TRAFFIC:
        s_ActiveSession->m_UnexpectedTermination = true;

        char ports[128];
        SDL_assert(portFlags != 0);
        LiStringifyPortFlags(portFlags, ", ", ports, sizeof(ports));
        emit s_ActiveSession->displayLaunchError(tr("No video received from host.") + "\n\n"+
                                                 tr("Check your firewall and port forwarding rules for port(s): %1").arg(ports));
        break;

    case ML_ERROR_NO_VIDEO_FRAME:
        s_ActiveSession->m_UnexpectedTermination = true;
        emit s_ActiveSession->displayLaunchError(tr("Your network connection isn't performing well. Reduce your video bitrate setting or try a faster connection."));
        break;

    case ML_ERROR_PROTECTED_CONTENT:
    case ML_ERROR_UNEXPECTED_EARLY_TERMINATION:
        s_ActiveSession->m_UnexpectedTermination = true;
        emit s_ActiveSession->displayLaunchError(tr("Something went wrong on your host PC when starting the stream.") + "\n\n" +
                                                 tr("Make sure you don't have any DRM-protected content open on your host PC. You can also try restarting your host PC."));
        break;

    case ML_ERROR_FRAME_CONVERSION:
        s_ActiveSession->m_UnexpectedTermination = true;
        emit s_ActiveSession->displayLaunchError(tr("The host PC reported a fatal video encoding error.") + "\n\n" +
                                                 tr("Try disabling HDR mode, changing the streaming resolution, or changing your host PC's display resolution."));
        break;

    default:
        s_ActiveSession->m_UnexpectedTermination = true;

        // We'll assume large errors are hex values
        bool hexError = qAbs(errorCode) > 1000;
        emit s_ActiveSession->displayLaunchError(tr("Connection terminated") + "\n\n" +
                                                 tr("Error code: %1").arg(errorCode, hexError ? 8 : 0, hexError ? 16 : 10, QChar('0')));
        break;
    }

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Connection terminated: %d",
                 errorCode);

    // Push a quit event to the main loop
    SDL_Event event;
    event.type = SDL_QUIT;
    event.quit.timestamp = SDL_GetTicks();
    SDL_PushEvent(&event);
}

void Session::clLogMessage(const char* format, ...)
{
    va_list ap;

    va_start(ap, format);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION,
                    SDL_LOG_PRIORITY_INFO,
                    format,
                    ap);
    va_end(ap);
}

void Session::clRumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor)
{
    // We push an event for the main thread to handle in order to properly synchronize
    // with the removal of game controllers that could result in our game controller
    // going away during this callback.
    SDL_Event rumbleEvent = {};
    rumbleEvent.type = SDL_USEREVENT;
    rumbleEvent.user.code = SDL_CODE_GAMECONTROLLER_RUMBLE;
    rumbleEvent.user.data1 = (void*)(uintptr_t)controllerNumber;
    rumbleEvent.user.data2 = (void*)(uintptr_t)((lowFreqMotor << 16) | highFreqMotor);
    SDL_PushEvent(&rumbleEvent);
}

void Session::clConnectionStatusUpdate(int connectionStatus)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Connection status update: %d",
                connectionStatus);

    if (!s_ActiveSession->m_Preferences->connectionWarnings) {
        return;
    }

    if (s_ActiveSession->m_MouseEmulationRefCount > 0) {
        // Don't display the overlay if mouse emulation is already using it
        return;
    }

    switch (connectionStatus)
    {
    case CONN_STATUS_POOR:
        s_ActiveSession->m_OverlayManager.updateOverlayText(Overlay::OverlayStatusUpdate,
                                                            s_ActiveSession->m_StreamConfig.bitrate > 5000 ?
                                                                "Slow connection to PC\nReduce your bitrate" : "Poor connection to PC");
        s_ActiveSession->m_OverlayManager.setOverlayState(Overlay::OverlayStatusUpdate, true);
        break;
    case CONN_STATUS_OKAY:
        s_ActiveSession->m_OverlayManager.setOverlayState(Overlay::OverlayStatusUpdate, false);
        break;
    }
}

void Session::clSetHdrMode(bool enabled)
{
    // If we're in the process of recreating our decoder when we get
    // this callback, we'll drop it. The main thread will make the
    // callback when it finishes creating the new decoder.
    if (SDL_TryLockMutex(s_ActiveSession->m_DecoderLock) == 0) {
        IVideoDecoder* decoder = s_ActiveSession->m_VideoDecoder;
        if (decoder != nullptr) {
            decoder->setHdrMode(enabled);
        }
        SDL_UnlockMutex(s_ActiveSession->m_DecoderLock);
    }
}

void Session::clRumbleTriggers(uint16_t controllerNumber, uint16_t leftTrigger, uint16_t rightTrigger)
{
    // We push an event for the main thread to handle in order to properly synchronize
    // with the removal of game controllers that could result in our game controller
    // going away during this callback.
    SDL_Event rumbleEvent = {};
    rumbleEvent.type = SDL_USEREVENT;
    rumbleEvent.user.code = SDL_CODE_GAMECONTROLLER_RUMBLE_TRIGGERS;
    rumbleEvent.user.data1 = (void*)(uintptr_t)controllerNumber;
    rumbleEvent.user.data2 = (void*)(uintptr_t)((leftTrigger << 16) | rightTrigger);
    SDL_PushEvent(&rumbleEvent);
}

void Session::clSetMotionEventState(uint16_t controllerNumber, uint8_t motionType, uint16_t reportRateHz)
{
    // We push an event for the main thread to handle in order to properly synchronize
    // with the removal of game controllers that could result in our game controller
    // going away during this callback.
    SDL_Event setMotionEventStateEvent = {};
    setMotionEventStateEvent.type = SDL_USEREVENT;
    setMotionEventStateEvent.user.code = SDL_CODE_GAMECONTROLLER_SET_MOTION_EVENT_STATE;
    setMotionEventStateEvent.user.data1 = (void*)(uintptr_t)controllerNumber;
    setMotionEventStateEvent.user.data2 = (void*)(uintptr_t)((motionType << 16) | reportRateHz);
    SDL_PushEvent(&setMotionEventStateEvent);
}

void Session::clSetControllerLED(uint16_t controllerNumber, uint8_t r, uint8_t g, uint8_t b)
{
    // We push an event for the main thread to handle in order to properly synchronize
    // with the removal of game controllers that could result in our game controller
    // going away during this callback.
    SDL_Event setControllerLEDEvent = {};
    setControllerLEDEvent.type = SDL_USEREVENT;
    setControllerLEDEvent.user.code = SDL_CODE_GAMECONTROLLER_SET_CONTROLLER_LED;
    setControllerLEDEvent.user.data1 = (void*)(uintptr_t)controllerNumber;
    setControllerLEDEvent.user.data2 = (void*)(uintptr_t)(r << 16 | g << 8 | b);
    SDL_PushEvent(&setControllerLEDEvent);
}

void Session::clSetAdaptiveTriggers(uint16_t controllerNumber, uint8_t eventFlags, uint8_t typeLeft, uint8_t typeRight, uint8_t *left, uint8_t *right){
    // We push an event for the main thread to handle in order to properly synchronize
    // with the removal of game controllers that could result in our game controller
    // going away during this callback.
    SDL_Event setControllerLEDEvent = {};
    setControllerLEDEvent.type = SDL_USEREVENT;
    setControllerLEDEvent.user.code = SDL_CODE_GAMECONTROLLER_SET_ADAPTIVE_TRIGGERS;
    setControllerLEDEvent.user.data1 = (void*)(uintptr_t)controllerNumber;

    // Based on the following SDL code:
    // https://github.com/libsdl-org/SDL/blob/120c76c84bbce4c1bfed4e9eb74e10678bd83120/test/testgamecontroller.c#L286-L307
    DualSenseOutputReport *state = (DualSenseOutputReport *) SDL_malloc(sizeof(DualSenseOutputReport));
    SDL_zero(*state);
    state->validFlag0 = (eventFlags & DS_EFFECT_RIGHT_TRIGGER) | (eventFlags & DS_EFFECT_LEFT_TRIGGER);
    state->rightTriggerEffectType = typeRight;
    SDL_memcpy(state->rightTriggerEffect, right, sizeof(state->rightTriggerEffect));
    state->leftTriggerEffectType = typeLeft;
    SDL_memcpy(state->leftTriggerEffect, left, sizeof(state->leftTriggerEffect));

    setControllerLEDEvent.user.data2 = (void *) state;
    SDL_PushEvent(&setControllerLEDEvent);
}


bool Session::chooseDecoder(StreamingPreferences::VideoDecoderSelection vds,
                            SDL_Window* window, int videoFormat, int width, int height,
                            int frameRate, bool enableVsync, bool enableFramePacing, bool testOnly, IVideoDecoder*& chosenDecoder)
{
    DECODER_PARAMETERS params;

    // We should never have vsync enabled for test-mode.
    // It introduces unnecessary delay for renderers that may
    // block while waiting for a backbuffer swap.
    SDL_assert(!enableVsync || !testOnly);

    params.width = width;
    params.height = height;
    params.frameRate = frameRate;
    params.videoFormat = videoFormat;
    params.window = window;
    params.enableVsync = enableVsync;
    params.enableFramePacing = enableFramePacing;
    params.testOnly = testOnly;
    params.vds = vds;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "V-sync %s",
                enableVsync ? "enabled" : "disabled");

#ifdef HAVE_SLVIDEO
    chosenDecoder = new SLVideoDecoder(testOnly);
    if (chosenDecoder->initialize(&params)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "SLVideo video decoder chosen");
        return true;
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to load SLVideo decoder");
        delete chosenDecoder;
        chosenDecoder = nullptr;
    }
#endif

#ifdef HAVE_FFMPEG
    chosenDecoder = new FFmpegVideoDecoder(testOnly);
    if (chosenDecoder->initialize(&params)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "FFmpeg-based video decoder chosen");
        return true;
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to load FFmpeg decoder");
        delete chosenDecoder;
        chosenDecoder = nullptr;
    }
#endif

#if !defined(HAVE_FFMPEG) && !defined(HAVE_SLVIDEO)
#error No video decoding libraries available!
#endif

    // If we reach this, we didn't initialize any decoders successfully
    return false;
}

int Session::drSetup(int videoFormat, int width, int height, int frameRate, void *, int)
{
    s_ActiveSession->m_ActiveVideoFormat = videoFormat;
    s_ActiveSession->m_ActiveVideoWidth = width;
    s_ActiveSession->m_ActiveVideoHeight = height;
    s_ActiveSession->m_ActiveVideoFrameRate = frameRate;

    // Defer decoder setup until we've started streaming so we
    // don't have to hide and show the SDL window (which seems to
    // cause pointer hiding to break on Windows).

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Video stream is %dx%dx%d (format 0x%x)",
                width, height, frameRate, videoFormat);

    return 0;
}

int Session::drSubmitDecodeUnit(PDECODE_UNIT du)
{
    // Use a lock since we'll be yanking this decoder out
    // from underneath the session when we initiate destruction.
    // We need to destroy the decoder on the main thread to satisfy
    // some API constraints (like DXVA2). If we can't acquire it,
    // that means the decoder is about to be destroyed, so we can
    // safely return DR_OK and wait for the IDR frame request by
    // the decoder reinitialization code.

    if (SDL_TryLockMutex(s_ActiveSession->m_DecoderLock) == 0) {
        IVideoDecoder* decoder = s_ActiveSession->m_VideoDecoder;
        if (decoder != nullptr) {
            int ret = decoder->submitDecodeUnit(du);
            SDL_UnlockMutex(s_ActiveSession->m_DecoderLock);
            return ret;
        }
        else {
            SDL_UnlockMutex(s_ActiveSession->m_DecoderLock);
            return DR_OK;
        }
    }
    else {
        // Decoder is going away. Ignore anything coming in until
        // the lock is released.
        return DR_OK;
    }
}

void Session::getDecoderInfo(SDL_Window* window,
                             bool& isHardwareAccelerated, bool& isFullScreenOnly,
                             bool& isHdrSupported, QSize& maxResolution)
{
    IVideoDecoder* decoder;

    // Since AV1 support on the host side is in its infancy, let's not consider
    // _only_ a working AV1 decoder to be acceptable and still show the warning
    // dialog indicating lack of hardware decoding support.

    // Try an HEVC Main10 decoder first to see if we have HDR support
    if (chooseDecoder(StreamingPreferences::VDS_FORCE_HARDWARE,
                      window, VIDEO_FORMAT_H265_MAIN10, 1920, 1080, 60,
                      false, false, true, decoder)) {
        isHardwareAccelerated = decoder->isHardwareAccelerated();
        isFullScreenOnly = decoder->isAlwaysFullScreen();
        isHdrSupported = decoder->isHdrSupported();
        maxResolution = decoder->getDecoderMaxResolution();
        delete decoder;

        return;
    }

    // Try an AV1 Main10 decoder next to see if we have HDR support
    if (chooseDecoder(StreamingPreferences::VDS_FORCE_HARDWARE,
                      window, VIDEO_FORMAT_AV1_MAIN10, 1920, 1080, 60,
                      false, false, true, decoder)) {
        // If we've got a working AV1 Main 10-bit decoder, we'll enable the HDR checkbox
        // but we will still continue probing to get other attributes for HEVC or H.264
        // decoders. See the AV1 comment at the top of the function for more info.
        isHdrSupported = decoder->isHdrSupported();
        delete decoder;
    }
    else {
        // If we found no hardware decoders with HDR, check for a renderer
        // that supports HDR rendering with software decoded frames.
        if (chooseDecoder(StreamingPreferences::VDS_FORCE_SOFTWARE,
                          window, VIDEO_FORMAT_H265_MAIN10, 1920, 1080, 60,
                          false, false, true, decoder) ||
            chooseDecoder(StreamingPreferences::VDS_FORCE_SOFTWARE,
                          window, VIDEO_FORMAT_AV1_MAIN10, 1920, 1080, 60,
                          false, false, true, decoder)) {
            isHdrSupported = decoder->isHdrSupported();
            delete decoder;
        }
        else {
            // We weren't compiled with an HDR-capable renderer or we don't
            // have the required GPU driver support for any HDR renderers.
            isHdrSupported = false;
        }
    }

    // Try a regular hardware accelerated HEVC decoder now
    if (chooseDecoder(StreamingPreferences::VDS_FORCE_HARDWARE,
                      window, VIDEO_FORMAT_H265, 1920, 1080, 60,
                      false, false, true, decoder)) {
        isHardwareAccelerated = decoder->isHardwareAccelerated();
        isFullScreenOnly = decoder->isAlwaysFullScreen();
        maxResolution = decoder->getDecoderMaxResolution();
        delete decoder;

        return;
    }


#if 0 // See AV1 comment at the top of this function
    if (chooseDecoder(StreamingPreferences::VDS_FORCE_HARDWARE,
                      window, VIDEO_FORMAT_AV1_MAIN8, 1920, 1080, 60,
                      false, false, true, decoder)) {
        isHardwareAccelerated = decoder->isHardwareAccelerated();
        isFullScreenOnly = decoder->isAlwaysFullScreen();
        maxResolution = decoder->getDecoderMaxResolution();
        delete decoder;

        return;
    }
#endif

    // If we still didn't find a hardware decoder, try H.264 now.
    // This will fall back to software decoding, so it should always work.
    if (chooseDecoder(StreamingPreferences::VDS_AUTO,
                      window, VIDEO_FORMAT_H264, 1920, 1080, 60,
                      false, false, true, decoder)) {
        isHardwareAccelerated = decoder->isHardwareAccelerated();
        isFullScreenOnly = decoder->isAlwaysFullScreen();
        maxResolution = decoder->getDecoderMaxResolution();
        delete decoder;

        return;
    }

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Failed to find ANY working H.264 or HEVC decoder!");
}

Session::DecoderAvailability
Session::getDecoderAvailability(SDL_Window* window,
                                StreamingPreferences::VideoDecoderSelection vds,
                                int videoFormat, int width, int height, int frameRate)
{
    IVideoDecoder* decoder;

    if (!chooseDecoder(vds, window, videoFormat, width, height, frameRate, false, false, true, decoder)) {
        return DecoderAvailability::None;
    }

    bool hw = decoder->isHardwareAccelerated();

    delete decoder;

    return hw ? DecoderAvailability::Hardware : DecoderAvailability::Software;
}

bool Session::populateDecoderProperties(SDL_Window* window)
{
    IVideoDecoder* decoder;

    if (!chooseDecoder(m_Preferences->videoDecoderSelection,
                       window,
                       m_SupportedVideoFormats.first(),
                       m_StreamConfig.width,
                       m_StreamConfig.height,
                       m_StreamConfig.fps,
                       false, false, true, decoder)) {
        return false;
    }

    m_VideoCallbacks.capabilities = decoder->getDecoderCapabilities();
    if (m_VideoCallbacks.capabilities & CAPABILITY_PULL_RENDERER) {
        // It is an error to pass a push callback when in pull mode
        m_VideoCallbacks.submitDecodeUnit = nullptr;
    }
    else {
        m_VideoCallbacks.submitDecodeUnit = drSubmitDecodeUnit;
    }

    {
        bool ok;

        m_StreamConfig.colorSpace = qEnvironmentVariableIntValue("COLOR_SPACE_OVERRIDE", &ok);
        if (ok) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Using colorspace override: %d",
                        m_StreamConfig.colorSpace);
        }
        else {
            m_StreamConfig.colorSpace = decoder->getDecoderColorspace();
        }

        m_StreamConfig.colorRange = qEnvironmentVariableIntValue("COLOR_RANGE_OVERRIDE", &ok);
        if (ok) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Using color range override: %d",
                        m_StreamConfig.colorRange);
        }
        else {
            m_StreamConfig.colorRange = decoder->getDecoderColorRange();
        }
    }

    if (decoder->isAlwaysFullScreen()) {
        m_IsFullScreen = true;
    }

    delete decoder;

    return true;
}

Session::Session(NvComputer* computer, NvApp& app, StreamingPreferences *preferences)
    : m_Preferences(preferences ? preferences : StreamingPreferences::get()),
      m_IsFullScreen(m_Preferences->windowMode != StreamingPreferences::WM_WINDOWED || !WMUtils::isRunningDesktopEnvironment()),
      m_Computer(computer),
      m_App(app),
      m_Window(nullptr),
      m_VideoDecoder(nullptr),
      m_DecoderLock(SDL_CreateMutex()),
      m_AudioMuted(false),
      m_QtWindow(nullptr),
      m_UnexpectedTermination(true), // Failure prior to streaming is unexpected
      m_InputHandler(nullptr),
      m_MouseEmulationRefCount(0),
      m_FlushingWindowEventsRef(0),
      m_ShouldExitAfterQuit(false),
      m_AsyncConnectionSuccess(false),
      m_PortTestResults(0),
      m_OpusDecoder(nullptr),
      m_AudioRenderer(nullptr),
      m_AudioSampleCount(0),
      m_DropAudioEndTime(0)
{
}

Session::~Session()
{
    // NB: This may not get destroyed for a long time! Don't put any non-trivial cleanup here.
    // Use Session::exec() or DeferredSessionCleanupTask instead.

    SDL_DestroyMutex(m_DecoderLock);
}

bool Session::initialize()
{
#ifdef Q_OS_DARWIN
    if (qEnvironmentVariableIntValue("I_WANT_BUGGY_FULLSCREEN") == 0) {
        // If we have a notch and the user specified one of the two native display modes
        // (notched or notchless), override the fullscreen mode to ensure it works as expected.
        // - SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES=0 will place the video underneath the notch
        // - SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES=1 will place the video below the notch
        bool shouldUseFullScreenSpaces = m_Preferences->windowMode != StreamingPreferences::WM_FULLSCREEN;
        SDL_DisplayMode desktopMode;
        SDL_Rect safeArea;
        for (int displayIndex = 0; StreamUtils::getNativeDesktopMode(displayIndex, &desktopMode, &safeArea); displayIndex++) {
            // Check if this display has a notch (safeArea != desktopMode)
            if (desktopMode.h != safeArea.h || desktopMode.w != safeArea.w) {
                // Check if we're trying to stream at the full native resolution (including notch)
                if (m_Preferences->width == desktopMode.w && m_Preferences->height == desktopMode.h) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "Overriding default fullscreen mode for native fullscreen resolution");
                    shouldUseFullScreenSpaces = false;
                    break;
                }
                else if (m_Preferences->width == safeArea.w && m_Preferences->height == safeArea.h) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "Overriding default fullscreen mode for native safe area resolution");
                    shouldUseFullScreenSpaces = true;
                    break;
                }
            }
        }

        // Using modesetting on modern versions of macOS is extremely unreliable
        // and leads to hangs, deadlocks, and other nasty stuff. The only time
        // people seem to use it is to get the full screen on notched Macs,
        // which setting SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES=1 also accomplishes
        // with much less headache.
        //
        // https://github.com/moonlight-stream/moonlight-qt/issues/973
        // https://github.com/moonlight-stream/moonlight-qt/issues/999
        // https://github.com/moonlight-stream/moonlight-qt/issues/1211
        // https://github.com/moonlight-stream/moonlight-qt/issues/1218
        SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, shouldUseFullScreenSpaces ? "1" : "0");
    }
#endif

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s",
                     SDL_GetError());
        return false;
    }

    LiInitializeStreamConfiguration(&m_StreamConfig);
    m_StreamConfig.width = m_Preferences->width;
    m_StreamConfig.height = m_Preferences->height;

    int x, y, width, height;
    getWindowDimensions(x, y, width, height);

    // Create a hidden window to use for decoder initialization tests
    SDL_Window* testWindow = SDL_CreateWindow("", x, y, width, height,
                                              SDL_WINDOW_HIDDEN | StreamUtils::getPlatformWindowFlags());
    if (!testWindow) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to create test window with platform flags: %s",
                    SDL_GetError());

        testWindow = SDL_CreateWindow("", x, y, width, height, SDL_WINDOW_HIDDEN);
        if (!testWindow) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to create window for hardware decode test: %s",
                         SDL_GetError());
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return false;
        }
    }

    qInfo() << "Server GPU:" << m_Computer->gpuModel;
    qInfo() << "Server GFE version:" << m_Computer->gfeVersion;

    LiInitializeVideoCallbacks(&m_VideoCallbacks);
    m_VideoCallbacks.setup = drSetup;

    m_StreamConfig.fps = m_Preferences->fps;
    m_StreamConfig.bitrate = m_Preferences->bitrateKbps;

#ifndef STEAM_LINK
    // Opt-in to all encryption features if we detect that the platform
    // has AES cryptography acceleration instructions and more than 2 cores.
    if (StreamUtils::hasFastAes() && SDL_GetCPUCount() > 2) {
        m_StreamConfig.encryptionFlags = ENCFLG_ALL;
    }
    else {
        // Enable audio encryption as long as we're not on Steam Link.
        // That hardware can hardly handle Opus decoding at all.
        m_StreamConfig.encryptionFlags = ENCFLG_AUDIO;
    }
#endif

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Video bitrate: %d kbps",
                m_StreamConfig.bitrate);

    RAND_bytes(reinterpret_cast<unsigned char*>(m_StreamConfig.remoteInputAesKey),
               sizeof(m_StreamConfig.remoteInputAesKey));

    // Only the first 4 bytes are populated in the RI key IV
    RAND_bytes(reinterpret_cast<unsigned char*>(m_StreamConfig.remoteInputAesIv), 4);

    switch (m_Preferences->audioConfig)
    {
    case StreamingPreferences::AC_STEREO:
        m_StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
        break;
    case StreamingPreferences::AC_51_SURROUND:
        m_StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_51_SURROUND;
        break;
    case StreamingPreferences::AC_71_SURROUND:
        m_StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_71_SURROUND;
        break;
    }

    LiInitializeAudioCallbacks(&m_AudioCallbacks);
    m_AudioCallbacks.init = arInit;
    m_AudioCallbacks.cleanup = arCleanup;
    m_AudioCallbacks.decodeAndPlaySample = arDecodeAndPlaySample;
    m_AudioCallbacks.capabilities = getAudioRendererCapabilities(m_StreamConfig.audioConfiguration);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Audio channel count: %d",
                CHANNEL_COUNT_FROM_AUDIO_CONFIGURATION(m_StreamConfig.audioConfiguration));
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Audio channel mask: %X",
                CHANNEL_MASK_FROM_AUDIO_CONFIGURATION(m_StreamConfig.audioConfiguration));

    // Start with all codecs and profiles in priority order
    m_SupportedVideoFormats.append(VIDEO_FORMAT_AV1_HIGH10_444);
    m_SupportedVideoFormats.append(VIDEO_FORMAT_AV1_MAIN10);
    m_SupportedVideoFormats.append(VIDEO_FORMAT_H265_REXT10_444);
    m_SupportedVideoFormats.append(VIDEO_FORMAT_H265_MAIN10);
    m_SupportedVideoFormats.append(VIDEO_FORMAT_AV1_HIGH8_444);
    m_SupportedVideoFormats.append(VIDEO_FORMAT_AV1_MAIN8);
    m_SupportedVideoFormats.append(VIDEO_FORMAT_H265_REXT8_444);
    m_SupportedVideoFormats.append(VIDEO_FORMAT_H265);
    m_SupportedVideoFormats.append(VIDEO_FORMAT_H264_HIGH8_444);
    m_SupportedVideoFormats.append(VIDEO_FORMAT_H264);

    switch (m_Preferences->videoCodecConfig)
    {
    case StreamingPreferences::VCC_AUTO:
    {
        // Codecs are checked in order of ascending decode complexity to ensure
        // the the deprioritized list prefers lighter codecs for software decoding

        // H.264 is already the lowest priority codec, so we don't need to do
        // any probing for deprioritization for it here.

        auto hevcDA = getDecoderAvailability(testWindow,
                                             m_Preferences->videoDecoderSelection,
                                             m_Preferences->enableYUV444 ?
                                                 (m_Preferences->enableHdr ? VIDEO_FORMAT_H265_REXT10_444 : VIDEO_FORMAT_H265_REXT8_444) :
                                                 (m_Preferences->enableHdr ? VIDEO_FORMAT_H265_MAIN10 : VIDEO_FORMAT_H265),
                                             m_StreamConfig.width,
                                             m_StreamConfig.height,
                                             m_StreamConfig.fps);
        if (hevcDA == DecoderAvailability::None && m_Preferences->enableHdr) {
            // Remove all 10-bit HEVC profiles
            m_SupportedVideoFormats.removeByMask(VIDEO_FORMAT_MASK_H265 & VIDEO_FORMAT_MASK_10BIT);

            // Check if we have 10-bit AV1 support
            auto av1DA = getDecoderAvailability(testWindow,
                                                m_Preferences->videoDecoderSelection,
                                                m_Preferences->enableYUV444 ? VIDEO_FORMAT_AV1_HIGH10_444 : VIDEO_FORMAT_AV1_MAIN10,
                                                m_StreamConfig.width,
                                                m_StreamConfig.height,
                                                m_StreamConfig.fps);
            if (av1DA == DecoderAvailability::None) {
                // Remove all 10-bit AV1 profiles
                m_SupportedVideoFormats.removeByMask(VIDEO_FORMAT_MASK_AV1 & VIDEO_FORMAT_MASK_10BIT);

                // There are no available 10-bit profiles, so reprobe for 8-bit HEVC
                // and we'll proceed as normal for an SDR streaming scenario.
                SDL_assert(!(m_SupportedVideoFormats & VIDEO_FORMAT_MASK_10BIT));
                hevcDA = getDecoderAvailability(testWindow,
                                                m_Preferences->videoDecoderSelection,
                                                m_Preferences->enableYUV444 ? VIDEO_FORMAT_H265_REXT8_444 : VIDEO_FORMAT_H265,
                                                m_StreamConfig.width,
                                                m_StreamConfig.height,
                                                m_StreamConfig.fps);
            }
        }

        if (hevcDA != DecoderAvailability::Hardware) {
            // Deprioritize HEVC unless the user forced software decoding and enabled HDR.
            // We need HEVC in that case because we cannot support 10-bit content with H.264,
            // which would ordinarily be prioritized for software decoding performance.
            if (m_Preferences->videoDecoderSelection != StreamingPreferences::VDS_FORCE_SOFTWARE || !m_Preferences->enableHdr) {
                m_SupportedVideoFormats.deprioritizeByMask(VIDEO_FORMAT_MASK_H265);
            }
        }

#if 0
        // TODO: Determine if AV1 is better depending on the decoder
        if (getDecoderAvailability(testWindow,
                                   m_Preferences->videoDecoderSelection,
                                   m_Preferences->enableYUV444 ?
                                        (m_Preferences->enableHdr ? VIDEO_FORMAT_AV1_HIGH10_444 : VIDEO_FORMAT_AV1_HIGH8_444) :
                                        (m_Preferences->enableHdr ? VIDEO_FORMAT_AV1_MAIN10 : VIDEO_FORMAT_AV1_MAIN8),
                                   m_StreamConfig.width,
                                   m_StreamConfig.height,
                                   m_StreamConfig.fps) != DecoderAvailability::Hardware) {
            // Deprioritize AV1 unless we can't hardware decode HEVC and have HDR enabled.
            // We want to keep AV1 at the top of the list for HDR with software decoding
            // because dav1d is higher performance than FFmpeg's HEVC software decoder.
            if (hevcDA == DecoderAvailability::Hardware || !m_Preferences->enableHdr) {
                m_SupportedVideoFormats.deprioritizeByMask(VIDEO_FORMAT_MASK_AV1);
            }
        }
#else
        // Deprioritize AV1 unless we can't hardware decode HEVC and have HDR enabled.
        // We want to keep AV1 at the top of the list for HDR with software decoding
        // because dav1d is higher performance than FFmpeg's HEVC software decoder.
        if (hevcDA == DecoderAvailability::Hardware || !m_Preferences->enableHdr) {
            m_SupportedVideoFormats.deprioritizeByMask(VIDEO_FORMAT_MASK_AV1);
        }
#endif

#ifdef Q_OS_DARWIN
        {
            // Prior to GFE 3.11, GFE did not allow us to constrain
            // the number of reference frames, so we have to fixup the SPS
            // to allow decoding via VideoToolbox on macOS. Since we don't
            // have fixup code for HEVC, just avoid it if GFE is too old.
            QVector<int> gfeVersion = NvHTTP::parseQuad(m_Computer->gfeVersion);
            if (gfeVersion.isEmpty() || // Very old versions don't have GfeVersion at all
                    gfeVersion[0] < 3 ||
                    (gfeVersion[0] == 3 && gfeVersion[1] < 11)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Disabling HEVC on macOS due to old GFE version");
                m_SupportedVideoFormats.removeByMask(VIDEO_FORMAT_MASK_H265);
            }
        }
#endif
        break;
    }
    case StreamingPreferences::VCC_FORCE_H264:
        m_SupportedVideoFormats.removeByMask(~VIDEO_FORMAT_MASK_H264);
        break;
    case StreamingPreferences::VCC_FORCE_HEVC:
    case StreamingPreferences::VCC_FORCE_HEVC_HDR_DEPRECATED:
        m_SupportedVideoFormats.removeByMask(~VIDEO_FORMAT_MASK_H265);
        break;
    case StreamingPreferences::VCC_FORCE_AV1:
        // We'll try to fall back to HEVC first if AV1 fails. We'd rather not fall back
        // straight to H.264 if the user asked for AV1 and the host doesn't support it.
        m_SupportedVideoFormats.removeByMask(~(VIDEO_FORMAT_MASK_AV1 | VIDEO_FORMAT_MASK_H265));
        break;
    }

    // NB: Since deprioritization puts codecs in reverse order (at the bottom of the list),
    // we want to deprioritize for the most critical attributes last to ensure they are the
    // lowest priority codecs during server negotiation. Here we do that with YUV 4:4:4 and
    // HDR to ensure we never pick a codec profile that doesn't meet the user's requirement
    // if we can avoid it.

    // Mask off YUV 4:4:4 codecs if the option is not enabled
    if (!m_Preferences->enableYUV444) {
        m_SupportedVideoFormats.removeByMask(VIDEO_FORMAT_MASK_YUV444);
    }
    else {
        // Deprioritize YUV 4:2:0 codecs if the user wants YUV 4:4:4
        //
        // NB: Since this happens first before deprioritizing HDR, we will
        // pick a YUV 4:4:4 profile instead of a 10-bit profile if they
        // aren't both available together for any codec.
        m_SupportedVideoFormats.deprioritizeByMask(~VIDEO_FORMAT_MASK_YUV444);
    }

    // Mask off 10-bit codecs if HDR is not enabled
    if (!m_Preferences->enableHdr) {
        m_SupportedVideoFormats.removeByMask(VIDEO_FORMAT_MASK_10BIT);
    }
    else {
        // Deprioritize 8-bit codecs if HDR is enabled
        m_SupportedVideoFormats.deprioritizeByMask(~VIDEO_FORMAT_MASK_10BIT);
    }

    switch (m_Preferences->windowMode)
    {
    default:
    case StreamingPreferences::WM_FULLSCREEN_DESKTOP:
        // Only use full-screen desktop mode if we're running a desktop environment
        if (WMUtils::isRunningDesktopEnvironment()) {
            m_FullScreenFlag = SDL_WINDOW_FULLSCREEN_DESKTOP;
            break;
        }
        // Fall-through
    case StreamingPreferences::WM_FULLSCREEN:
#ifdef Q_OS_DARWIN
        if (qEnvironmentVariableIntValue("I_WANT_BUGGY_FULLSCREEN") == 0) {
            // Don't use "real" fullscreen on macOS by default. See comments above.
            m_FullScreenFlag = SDL_WINDOW_FULLSCREEN_DESKTOP;
        }
        else {
            m_FullScreenFlag = SDL_WINDOW_FULLSCREEN;
        }
#else
        m_FullScreenFlag = SDL_WINDOW_FULLSCREEN;
#endif
        break;
    }

#if !SDL_VERSION_ATLEAST(2, 0, 11)
    // HACK: Using a full-screen window breaks mouse capture on the Pi's LXDE
    // GUI environment. Force the session to use windowed mode (which won't
    // really matter anyway because the MMAL renderer always draws full-screen).
    if (qgetenv("DESKTOP_SESSION") == "LXDE-pi") {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Forcing windowed mode on LXDE-Pi");
        m_FullScreenFlag = 0;
    }
#endif

    // Check for validation errors/warnings and emit
    // signals for them, if appropriate
    bool ret = validateLaunch(testWindow);

    if (ret) {
        // Video format is now locked in
        m_StreamConfig.supportedVideoFormats = m_SupportedVideoFormats.front();

        // Populate decoder-dependent properties.
        // Must be done after validateLaunch() since m_StreamConfig is finalized.
        ret = populateDecoderProperties(testWindow);
    }

    SDL_DestroyWindow(testWindow);

    if (!ret) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

    if (m_Preferences->configurationWarnings) {
        // Display launch warnings in Qt only after destroying SDL's window.
        // This avoids conflicts between the windows on display subsystems
        // such as KMSDRM that only support a single window.
        for (const auto &text : m_LaunchWarnings) {
            // Emit the warning to the UI
            emit displayLaunchWarning(text);

            // Wait a little bit so the user can actually read what we just said.
            // This wait is a little longer than the actual toast timeout (3 seconds)
            // to allow it to transition off the screen before continuing.
            uint32_t start = SDL_GetTicks();
            while (!SDL_TICKS_PASSED(SDL_GetTicks(), start + 3500)) {
                SDL_Delay(5);

                if (!m_ThreadedExec) {
                    // Pump the UI loop while we wait if we're on the main thread
                    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                    QCoreApplication::sendPostedEvents();
                }
            }
        }
    }

    return true;
}

void Session::emitLaunchWarning(QString text)
{
    // Queue this launch warning to be displayed after validation
    m_LaunchWarnings.append(text);
}

bool Session::validateLaunch(SDL_Window* testWindow)
{
    if (!m_Computer->isSupportedServerVersion) {
        emit displayLaunchError(tr("The version of GeForce Experience on %1 is not supported by this build of Moonlight. You must update Moonlight to stream from %1.").arg(m_Computer->name));
        return false;
    }

    if (m_Preferences->absoluteMouseMode && !m_App.isAppCollectorGame) {
        emitLaunchWarning(tr("Your selection to enable remote desktop mouse mode may cause problems in games."));
    }

    if (m_Preferences->videoDecoderSelection == StreamingPreferences::VDS_FORCE_SOFTWARE) {
        emitLaunchWarning(tr("Your settings selection to force software decoding may cause poor streaming performance."));
    }

    if (m_SupportedVideoFormats & VIDEO_FORMAT_MASK_AV1) {
        if (m_SupportedVideoFormats.maskByServerCodecModes(m_Computer->serverCodecModeSupport & SCM_MASK_AV1) == 0) {
            if (m_Preferences->videoCodecConfig == StreamingPreferences::VCC_FORCE_AV1) {
                emitLaunchWarning(tr("Your host software or GPU doesn't support encoding AV1."));
            }

            // Moonlight-common-c will handle this case already, but we want
            // to set this explicitly here so we can do our hardware acceleration
            // check below.
            m_SupportedVideoFormats.removeByMask(VIDEO_FORMAT_MASK_AV1);
        }
        else if (!m_Preferences->enableHdr && // HDR is checked below
                 m_Preferences->videoDecoderSelection == StreamingPreferences::VDS_AUTO && // Force hardware decoding checked below
                 m_Preferences->videoCodecConfig != StreamingPreferences::VCC_AUTO && // Auto VCC is already checked in initialize()
                 getDecoderAvailability(testWindow,
                                        m_Preferences->videoDecoderSelection,
                                        VIDEO_FORMAT_AV1_MAIN8,
                                        m_StreamConfig.width,
                                        m_StreamConfig.height,
                                        m_StreamConfig.fps) != DecoderAvailability::Hardware) {
            emitLaunchWarning(tr("Using software decoding due to your selection to force AV1 without GPU support. This may cause poor streaming performance."));
        }
    }

    if (m_SupportedVideoFormats & VIDEO_FORMAT_MASK_H265) {
        if (m_Computer->maxLumaPixelsHEVC == 0) {
            if (m_Preferences->videoCodecConfig == StreamingPreferences::VCC_FORCE_HEVC) {
                emitLaunchWarning(tr("Your host PC doesn't support encoding HEVC."));
            }

            // Moonlight-common-c will handle this case already, but we want
            // to set this explicitly here so we can do our hardware acceleration
            // check below.
            m_SupportedVideoFormats.removeByMask(VIDEO_FORMAT_MASK_H265);
        }
        else if (!m_Preferences->enableHdr && // HDR is checked below
                 m_Preferences->videoDecoderSelection == StreamingPreferences::VDS_AUTO && // Force hardware decoding checked below
                 m_Preferences->videoCodecConfig != StreamingPreferences::VCC_AUTO && // Auto VCC is already checked in initialize()
                 getDecoderAvailability(testWindow,
                                        m_Preferences->videoDecoderSelection,
                                        VIDEO_FORMAT_H265,
                                        m_StreamConfig.width,
                                        m_StreamConfig.height,
                                        m_StreamConfig.fps) != DecoderAvailability::Hardware) {
            emitLaunchWarning(tr("Using software decoding due to your selection to force HEVC without GPU support. This may cause poor streaming performance."));
        }
    }

    if (!(m_SupportedVideoFormats & VIDEO_FORMAT_MASK_H265) &&
            m_Preferences->videoDecoderSelection == StreamingPreferences::VDS_AUTO &&
            getDecoderAvailability(testWindow,
                                   m_Preferences->videoDecoderSelection,
                                   VIDEO_FORMAT_H264,
                                   m_StreamConfig.width,
                                   m_StreamConfig.height,
                                   m_StreamConfig.fps) != DecoderAvailability::Hardware) {

        if (m_Preferences->videoCodecConfig == StreamingPreferences::VCC_FORCE_H264) {
            emitLaunchWarning(tr("Using software decoding due to your selection to force H.264 without GPU support. This may cause poor streaming performance."));
        }
        else {
            if (m_Computer->maxLumaPixelsHEVC == 0 &&
                    getDecoderAvailability(testWindow,
                                           m_Preferences->videoDecoderSelection,
                                           VIDEO_FORMAT_H265,
                                           m_StreamConfig.width,
                                           m_StreamConfig.height,
                                           m_StreamConfig.fps) == DecoderAvailability::Hardware) {
                emitLaunchWarning(tr("Your host PC and client PC don't support the same video codecs. This may cause poor streaming performance."));
            }
            else {
                emitLaunchWarning(tr("Your client GPU doesn't support H.264 decoding. This may cause poor streaming performance."));
            }
        }
    }

    if (m_Preferences->enableHdr) {
        if (m_Preferences->videoCodecConfig == StreamingPreferences::VCC_FORCE_H264) {
            emitLaunchWarning(tr("HDR is not supported using the H.264 codec."));
            m_SupportedVideoFormats.removeByMask(VIDEO_FORMAT_MASK_10BIT);
        }
        else if (!(m_SupportedVideoFormats & VIDEO_FORMAT_MASK_10BIT)) {
            emitLaunchWarning(tr("This PC's GPU doesn't support 10-bit HEVC or AV1 decoding for HDR streaming."));
        }
        // Check that the server GPU supports HDR
        else if (m_SupportedVideoFormats.maskByServerCodecModes(m_Computer->serverCodecModeSupport & SCM_MASK_10BIT) == 0) {
            emitLaunchWarning(tr("Your host PC doesn't support HDR streaming."));
            m_SupportedVideoFormats.removeByMask(VIDEO_FORMAT_MASK_10BIT);
        }
        else if (m_Preferences->videoCodecConfig != StreamingPreferences::VCC_AUTO) { // Auto was already checked during init
            bool displayedHdrSoftwareDecodeWarning = false;

            // Check that the available HDR-capable codecs on the client and server are compatible
            if (m_SupportedVideoFormats.maskByServerCodecModes(m_Computer->serverCodecModeSupport & SCM_AV1_MAIN10)) {
                auto da = getDecoderAvailability(testWindow,
                                                 m_Preferences->videoDecoderSelection,
                                                 VIDEO_FORMAT_AV1_MAIN10,
                                                 m_StreamConfig.width,
                                                 m_StreamConfig.height,
                                                 m_StreamConfig.fps);
                if (da == DecoderAvailability::None) {
                    emitLaunchWarning(tr("This PC's GPU doesn't support AV1 Main10 decoding for HDR streaming."));
                    m_SupportedVideoFormats.removeByMask(VIDEO_FORMAT_AV1_MAIN10);
                }
                else if (da == DecoderAvailability::Software &&
                           m_Preferences->videoDecoderSelection != StreamingPreferences::VDS_FORCE_SOFTWARE &&
                           !displayedHdrSoftwareDecodeWarning) {
                    emitLaunchWarning(tr("Using software decoding due to your selection to force HDR without GPU support. This may cause poor streaming performance."));
                    displayedHdrSoftwareDecodeWarning = true;
                }
            }
            if (m_SupportedVideoFormats.maskByServerCodecModes(m_Computer->serverCodecModeSupport & SCM_HEVC_MAIN10)) {
                auto da = getDecoderAvailability(testWindow,
                                                 m_Preferences->videoDecoderSelection,
                                                 VIDEO_FORMAT_H265_MAIN10,
                                                 m_StreamConfig.width,
                                                 m_StreamConfig.height,
                                                 m_StreamConfig.fps);
                if (da == DecoderAvailability::None) {
                    emitLaunchWarning(tr("This PC's GPU doesn't support HEVC Main10 decoding for HDR streaming."));
                    m_SupportedVideoFormats.removeByMask(VIDEO_FORMAT_H265_MAIN10);
                }
                else if (da == DecoderAvailability::Software &&
                         m_Preferences->videoDecoderSelection != StreamingPreferences::VDS_FORCE_SOFTWARE &&
                         !displayedHdrSoftwareDecodeWarning) {
                    emitLaunchWarning(tr("Using software decoding due to your selection to force HDR without GPU support. This may cause poor streaming performance."));
                    displayedHdrSoftwareDecodeWarning = true;
                }
            }
        }

        // Check for compatibility between server and client codecs
        if ((m_SupportedVideoFormats & VIDEO_FORMAT_MASK_10BIT) && // Ignore this check if we already failed one above
            !(m_SupportedVideoFormats.maskByServerCodecModes(m_Computer->serverCodecModeSupport) & VIDEO_FORMAT_MASK_10BIT)) {
            emitLaunchWarning(tr("Your host PC and client PC don't support the same HDR video codecs."));
            m_SupportedVideoFormats.removeByMask(VIDEO_FORMAT_MASK_10BIT);
        }
    }

    if (m_Preferences->enableYUV444) {
        if (!(m_Computer->serverCodecModeSupport & SCM_MASK_YUV444)) {
            emitLaunchWarning(tr("Your host PC doesn't support YUV 4:4:4 streaming."));
            m_SupportedVideoFormats.removeByMask(VIDEO_FORMAT_MASK_YUV444);
        }
        else {
            m_SupportedVideoFormats.removeByMask(~m_SupportedVideoFormats.maskByServerCodecModes(m_Computer->serverCodecModeSupport));

            if (!m_SupportedVideoFormats.isEmpty() &&
                !(m_SupportedVideoFormats.front() & VIDEO_FORMAT_MASK_YUV444)) {
                emitLaunchWarning(tr("Your host PC doesn't support YUV 4:4:4 streaming for selected video codec."));
            }
            else if (m_Preferences->videoDecoderSelection != StreamingPreferences::VDS_FORCE_SOFTWARE) {
                while (!m_SupportedVideoFormats.isEmpty() &&
                       (m_SupportedVideoFormats.front() & VIDEO_FORMAT_MASK_YUV444) &&
                       getDecoderAvailability(testWindow,
                                              m_Preferences->videoDecoderSelection,
                                              m_SupportedVideoFormats.front(),
                                              m_StreamConfig.width,
                                              m_StreamConfig.height,
                                              m_StreamConfig.fps) != DecoderAvailability::Hardware) {
                    if (m_Preferences->videoDecoderSelection == StreamingPreferences::VDS_FORCE_HARDWARE) {
                        m_SupportedVideoFormats.removeFirst();
                    }
                    else {
                        emitLaunchWarning(tr("Using software decoding due to your selection to force YUV 4:4:4 without GPU support. This may cause poor streaming performance."));
                        break;
                    }
                }
                if (!m_SupportedVideoFormats.isEmpty() &&
                    !(m_SupportedVideoFormats.front() & VIDEO_FORMAT_MASK_YUV444)) {
                    emitLaunchWarning(tr("This PC's GPU doesn't support YUV 4:4:4 decoding for selected video codec."));
                }
            }
        }
    }

    if (m_StreamConfig.width >= 3840) {
        // Only allow 4K on GFE 3.x+
        if (m_Computer->gfeVersion.isEmpty() || m_Computer->gfeVersion.startsWith("2.")) {
            emitLaunchWarning(tr("GeForce Experience 3.0 or higher is required for 4K streaming."));

            m_StreamConfig.width = 1920;
            m_StreamConfig.height = 1080;
        }
    }

    // Test if audio works at the specified audio configuration
    bool audioTestPassed = testAudio(m_StreamConfig.audioConfiguration);

    // Gracefully degrade to stereo if surround sound doesn't work
    if (!audioTestPassed && CHANNEL_COUNT_FROM_AUDIO_CONFIGURATION(m_StreamConfig.audioConfiguration) > 2) {
        audioTestPassed = testAudio(AUDIO_CONFIGURATION_STEREO);
        if (audioTestPassed) {
            m_StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
            emitLaunchWarning(tr("Your selected surround sound setting is not supported by the current audio device."));
        }
    }

    // If nothing worked, warn the user that audio will not work
    if (!audioTestPassed) {
        emitLaunchWarning(tr("Failed to open audio device. Audio will be unavailable during this session."));
    }

    // Check for unmapped gamepads
    if (!SdlInputHandler::getUnmappedGamepads().isEmpty()) {
        emitLaunchWarning(tr("An attached gamepad has no mapping and won't be usable. Visit the Moonlight help to resolve this."));
    }

    // If we removed all codecs with the checks above, use H.264 as the codec of last resort.
    if (m_SupportedVideoFormats.empty()) {
        m_SupportedVideoFormats.append(VIDEO_FORMAT_H264);
    }

    // NVENC will fail to initialize when any dimension exceeds 4096 using:
    // - H.264 on all versions of NVENC
    // - HEVC prior to Pascal
    //
    // However, if we aren't using Nvidia hosting software, don't assume anything about
    // encoding capabilities by using HEVC Main 10 support. It will likely be wrong.
    if ((m_StreamConfig.width > 4096 || m_StreamConfig.height > 4096) && m_Computer->isNvidiaServerSoftware) {
        // Pascal added support for 8K HEVC encoding support. Maxwell 2 could encode HEVC but only up to 4K.
        // We can't directly identify Pascal, but we can look for HEVC Main10 which was added in the same generation.
        if (m_Computer->maxLumaPixelsHEVC == 0 || !(m_Computer->serverCodecModeSupport & SCM_HEVC_MAIN10)) {
            emit displayLaunchError(tr("Your host PC's GPU doesn't support streaming video resolutions over 4K."));
            return false;
        }
        else if ((m_SupportedVideoFormats & ~VIDEO_FORMAT_MASK_H264) == 0) {
            emit displayLaunchError(tr("Video resolutions over 4K are not supported by the H.264 codec."));
            return false;
        }
    }

    if (m_Preferences->videoDecoderSelection == StreamingPreferences::VDS_FORCE_HARDWARE &&
            !(m_SupportedVideoFormats & VIDEO_FORMAT_MASK_10BIT) && // HDR was already checked for hardware decode support above
            getDecoderAvailability(testWindow,
                                   m_Preferences->videoDecoderSelection,
                                   m_SupportedVideoFormats.front(),
                                   m_StreamConfig.width,
                                   m_StreamConfig.height,
                                   m_StreamConfig.fps) != DecoderAvailability::Hardware) {
        if (m_Preferences->videoCodecConfig == StreamingPreferences::VCC_AUTO) {
            emit displayLaunchError(tr("Your selection to force hardware decoding cannot be satisfied due to missing hardware decoding support on this PC's GPU."));
        }
        else {
            emit displayLaunchError(tr("Your codec selection and force hardware decoding setting are not compatible. This PC's GPU lacks support for decoding your chosen codec."));
        }

        // Fail the launch, because we won't manage to get a decoder for the actual stream
        return false;
    }

    return true;
}


class DeferredSessionCleanupTask : public QRunnable
{
public:
    DeferredSessionCleanupTask(Session* session) :
        m_Session(session) {}

private:
    virtual ~DeferredSessionCleanupTask() override
    {
        // Allow another session to start now that we're cleaned up
        Session::s_ActiveSession = nullptr;
        Session::s_ActiveSessionSemaphore.release();

        // Notify that the session is ready to be cleaned up
        emit m_Session->readyForDeletion();
    }

    void run() override
    {
        // Only quit the running app if our session terminated gracefully
        bool shouldQuit =
                !m_Session->m_UnexpectedTermination &&
                (m_Session->m_Preferences->quitAppAfter ||
                 m_Session->m_ShouldExitAfterQuit);

        // Notify the UI
        if (shouldQuit) {
            emit m_Session->quitStarting();
        }
        else {
            emit m_Session->sessionFinished(m_Session->m_PortTestResults);
        }

        // The video decoder must already be destroyed, since it could
        // try to interact with APIs that can only be called between
        // LiStartConnection() and LiStopConnection().
        SDL_assert(m_Session->m_VideoDecoder == nullptr);

        // Finish cleanup of the connection state
        LiStopConnection();

        // Perform a best-effort app quit
        if (shouldQuit) {
            NvHTTP http(m_Session->m_Computer);

            // Logging is already done inside NvHTTP
            try {
                http.quitApp();
            } catch (const GfeHttpResponseException&) {
            } catch (const QtNetworkReplyException&) {
            }

            // Exit the entire program if requested
            if (m_Session->m_ShouldExitAfterQuit) {
                QCoreApplication::instance()->quit();
            }

            // Session is finished now
            emit m_Session->sessionFinished(m_Session->m_PortTestResults);
        }
    }

    Session* m_Session;
};

void Session::getWindowDimensions(int& x, int& y,
                                  int& width, int& height)
{
    int displayIndex = 0;

    if (m_Window != nullptr) {
        displayIndex = SDL_GetWindowDisplayIndex(m_Window);
        SDL_assert(displayIndex >= 0);
    }
    // Create our window on the same display that Qt's UI
    // was being displayed on.
    else {
        Q_ASSERT(m_QtWindow != nullptr);
        if (m_QtWindow != nullptr) {
            QScreen* screen = m_QtWindow->screen();
            if (screen != nullptr) {
                QRect displayRect = screen->geometry();

                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Qt UI screen is at (%d,%d)",
                            displayRect.x(), displayRect.y());
                for (int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
                    SDL_Rect displayBounds;

                    if (SDL_GetDisplayBounds(i, &displayBounds) == 0) {
                        if (displayBounds.x == displayRect.x() &&
                            displayBounds.y == displayRect.y()) {
                            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                        "SDL found matching display %d",
                                        i);
                            displayIndex = i;
                            break;
                        }
                    }
                    else {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "SDL_GetDisplayBounds(%d) failed: %s",
                                    i, SDL_GetError());
                    }
                }
            }
            else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Qt window is not associated with a QScreen!");
            }
        }
    }

    SDL_Rect usableBounds;
    if (SDL_GetDisplayUsableBounds(displayIndex, &usableBounds) == 0) {
        // If the stream resolution fits within the usable display area, use it directly
        if (m_StreamConfig.width <= usableBounds.w &&
            m_StreamConfig.height <= usableBounds.h) {
            width = m_StreamConfig.width;
            height = m_StreamConfig.height;
        } else {
            // Otherwise, use 80% of usable bounds and preserve aspect ratio
            SDL_Rect src, dst;
            src.x = src.y = dst.x = dst.y = 0;
            src.w = m_StreamConfig.width;
            src.h = m_StreamConfig.height;

            dst.w = ((int)(usableBounds.w * 0.80f)) & ~0x1;  // even width
            dst.h = ((int)(usableBounds.h * 0.80f)) & ~0x1;  // even height

            StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

            width = dst.w;
            height = dst.h;
        }
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_GetDisplayUsableBounds() failed: %s",
                     SDL_GetError());

        width = m_StreamConfig.width;
        height = m_StreamConfig.height;
    }

    x = y = SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex);
}

void Session::updateOptimalWindowDisplayMode()
{
    SDL_DisplayMode desktopMode, bestMode, mode;
    int displayIndex = SDL_GetWindowDisplayIndex(m_Window);

    // Try the current display mode first. On macOS, this will be the normal
    // scaled desktop resolution setting.
    if (SDL_GetDesktopDisplayMode(displayIndex, &desktopMode) == 0) {
        // If this doesn't fit the selected resolution, use the native
        // resolution of the panel (unscaled).
        if (desktopMode.w < m_ActiveVideoWidth || desktopMode.h < m_ActiveVideoHeight) {
            SDL_Rect safeArea;
            if (!StreamUtils::getNativeDesktopMode(displayIndex, &desktopMode, &safeArea)) {
                return;
            }
        }
    }
    else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL_GetDesktopDisplayMode() failed: %s",
                    SDL_GetError());
        return;
    }

    // Start with the native desktop resolution and try to find
    // the highest refresh rate that our stream FPS evenly divides.
    bestMode = desktopMode;
    bestMode.refresh_rate = 0;
    for (int i = 0; i < SDL_GetNumDisplayModes(displayIndex); i++) {
        if (SDL_GetDisplayMode(displayIndex, i, &mode) == 0) {
            if (mode.w == desktopMode.w && mode.h == desktopMode.h &&
                    mode.refresh_rate % m_StreamConfig.fps == 0) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Found display mode with desktop resolution: %dx%dx%d",
                            mode.w, mode.h, mode.refresh_rate);
                if (mode.refresh_rate > bestMode.refresh_rate) {
                    bestMode = mode;
                }
            }
        }
    }

    // If we didn't find a mode that matched the current resolution and
    // had a high enough refresh rate, start looking for lower resolution
    // modes that can meet the required refresh rate and minimum video
    // resolution. We will also try to pick a display mode that matches
    // aspect ratio closest to the video stream.
    if (bestMode.refresh_rate == 0) {
        float bestModeAspectRatio = 0;
        float videoAspectRatio = (float)m_ActiveVideoWidth / (float)m_ActiveVideoHeight;
        for (int i = 0; i < SDL_GetNumDisplayModes(displayIndex); i++) {
            if (SDL_GetDisplayMode(displayIndex, i, &mode) == 0) {
                float modeAspectRatio = (float)mode.w / (float)mode.h;
                if (mode.w >= m_ActiveVideoWidth && mode.h >= m_ActiveVideoHeight &&
                        mode.refresh_rate % m_StreamConfig.fps == 0) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "Found display mode with video resolution: %dx%dx%d",
                                mode.w, mode.h, mode.refresh_rate);
                    if (mode.refresh_rate >= bestMode.refresh_rate &&
                            (bestModeAspectRatio == 0 || fabs(videoAspectRatio - modeAspectRatio) <= fabs(videoAspectRatio - bestModeAspectRatio))) {
                        bestMode = mode;
                        bestModeAspectRatio = modeAspectRatio;
                    }
                }
            }
        }
    }

    if (bestMode.refresh_rate == 0) {
        // We may find no match if the user has moved a 120 FPS
        // stream onto a 60 Hz monitor (since no refresh rate can
        // divide our FPS setting). We'll stick to the default in
        // this case.
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "No matching display mode found; using desktop mode");
        bestMode = desktopMode;
    }

    if ((SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN) {
        // Only print when the window is actually in full-screen exclusive mode,
        // otherwise we're not actually using the mode we've set here
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Chosen best display mode: %dx%dx%d",
                    bestMode.w, bestMode.h, bestMode.refresh_rate);
    }

    SDL_SetWindowDisplayMode(m_Window, &bestMode);
}

void Session::toggleFullscreen()
{
    bool fullScreen = !(SDL_GetWindowFlags(m_Window) & m_FullScreenFlag);

#if defined(Q_OS_WIN32) || defined(Q_OS_DARWIN)
    // Destroy the video decoder before toggling full-screen because D3D9 can try
    // to put the window back into full-screen before we've managed to destroy
    // the renderer. This leads to excessive flickering and can cause the window
    // decorations to get messed up as SDL and D3D9 fight over the window style.
    //
    // On Apple Silicon Macs, the AVSampleBufferDisplayLayer may cause WindowServer
    // to deadlock when transitioning out of fullscreen. Destroy the decoder before
    // exiting fullscreen as a workaround. See issue #973.
    SDL_LockMutex(m_DecoderLock);
    delete m_VideoDecoder;
    m_VideoDecoder = nullptr;
    SDL_UnlockMutex(m_DecoderLock);
#endif

    // Actually enter/leave fullscreen
    SDL_SetWindowFullscreen(m_Window, fullScreen ? m_FullScreenFlag : 0);

#ifdef Q_OS_DARWIN
    // SDL on macOS has a bug that causes the window size to be reset to crazy
    // large dimensions when exiting out of true fullscreen mode. We can work
    // around the issue by manually resetting the position and size here.
    if (!fullScreen && m_FullScreenFlag == SDL_WINDOW_FULLSCREEN) {
        int x, y, width, height;
        getWindowDimensions(x, y, width, height);
        SDL_SetWindowSize(m_Window, width, height);
        SDL_SetWindowPosition(m_Window, x, y);
    }
#endif

    // Input handler might need to start/stop keyboard grab after changing modes
    m_InputHandler->updateKeyboardGrabState();

    // Input handler might need stop/stop mouse grab after changing modes
    m_InputHandler->updatePointerRegionLock();
}

void Session::notifyMouseEmulationMode(bool enabled)
{
    m_MouseEmulationRefCount += enabled ? 1 : -1;
    SDL_assert(m_MouseEmulationRefCount >= 0);

    // We re-use the status update overlay for mouse mode notification
    if (m_MouseEmulationRefCount > 0) {
        m_OverlayManager.updateOverlayText(Overlay::OverlayStatusUpdate, "Gamepad mouse mode active\nLong press Start to deactivate");
        m_OverlayManager.setOverlayState(Overlay::OverlayStatusUpdate, true);
    }
    else {
        m_OverlayManager.setOverlayState(Overlay::OverlayStatusUpdate, false);
    }
}

class AsyncConnectionStartThread : public QThread
{
public:
    AsyncConnectionStartThread(Session* session) :
        QThread(nullptr),
        m_Session(session)
    {
        setObjectName("Async Conn Start");
    }

    void run() override
    {
        m_Session->m_AsyncConnectionSuccess = m_Session->startConnectionAsync();
    }

    Session* m_Session;
};

// Called in a non-main thread
bool Session::startConnectionAsync()
{
    // Wait 1.5 seconds before connecting to let the user
    // have time to read any messages present on the segue
    SDL_Delay(1500);

    // The UI should have ensured the old game was already quit
    // if we decide to stream a different game.
    Q_ASSERT(m_Computer->currentGameId == 0 ||
             m_Computer->currentGameId == m_App.id);

    bool enableGameOptimizations;
    if (m_Computer->isNvidiaServerSoftware) {
        // GFE will set all settings to 720p60 if it doesn't recognize
        // the chosen resolution. Avoid that by disabling SOPS when it
        // is not streaming a supported resolution.
        enableGameOptimizations = false;
        for (const NvDisplayMode &mode : m_Computer->displayModes) {
            if (mode.width == m_StreamConfig.width &&
                    mode.height == m_StreamConfig.height) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Found host supported resolution: %dx%d",
                            mode.width, mode.height);
                enableGameOptimizations = m_Preferences->gameOptimizations;
                break;
            }
        }
    }
    else {
        // Always send SOPS to Sunshine because we may repurpose the
        // option to control whether the display mode is adjusted
        enableGameOptimizations = m_Preferences->gameOptimizations;
    }

    QString rtspSessionUrl;

    try {
        NvHTTP http(m_Computer);
        http.startApp(m_Computer->currentGameId != 0 ? "resume" : "launch",
                      m_Computer->isNvidiaServerSoftware,
                      m_App.id, &m_StreamConfig,
                      enableGameOptimizations,
                      m_Preferences->playAudioOnHost,
                      m_InputHandler->getAttachedGamepadMask(),
                      !m_Preferences->multiController,
                      rtspSessionUrl);
    } catch (const GfeHttpResponseException& e) {
        emit displayLaunchError(tr("Host returned error: %1").arg(e.toQString()));
        return false;
    } catch (const QtNetworkReplyException& e) {
        emit displayLaunchError(e.toQString());
        return false;
    }

    QByteArray hostnameStr = m_Computer->activeAddress.address().toLatin1();
    QByteArray siAppVersion = m_Computer->appVersion.toLatin1();

    SERVER_INFORMATION hostInfo;
    hostInfo.address = hostnameStr.data();
    hostInfo.serverInfoAppVersion = siAppVersion.data();
    hostInfo.serverCodecModeSupport = m_Computer->serverCodecModeSupport;

    // Older GFE versions didn't have this field
    QByteArray siGfeVersion;
    if (!m_Computer->gfeVersion.isEmpty()) {
        siGfeVersion = m_Computer->gfeVersion.toLatin1();
    }
    if (!siGfeVersion.isEmpty()) {
        hostInfo.serverInfoGfeVersion = siGfeVersion.data();
    }

    // Older GFE and Sunshine versions didn't have this field
    QByteArray rtspSessionUrlStr;
    if (!rtspSessionUrl.isEmpty()) {
        rtspSessionUrlStr = rtspSessionUrl.toLatin1();
        hostInfo.rtspSessionUrl = rtspSessionUrlStr.data();
    }

    if (m_Preferences->packetSize != 0) {
        // Override default packet size and remote streaming detection
        // NB: Using STREAM_CFG_AUTO will cap our packet size at 1024 for remote hosts.
        m_StreamConfig.streamingRemotely = STREAM_CFG_LOCAL;
        m_StreamConfig.packetSize = m_Preferences->packetSize;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Using custom packet size: %d bytes",
                    m_Preferences->packetSize);
    }
    else {
        // Use 1392 byte video packets by default
        m_StreamConfig.packetSize = 1392;

        // getActiveAddressReachability() does network I/O, so we only attempt to check
        // reachability if we've already contacted the PC successfully.
        switch (m_Computer->getActiveAddressReachability()) {
        case NvComputer::RI_LAN:
            // This address is on-link, so treat it as a local address
            // even if it's not in RFC 1918 space or it's an IPv6 address.
            m_StreamConfig.streamingRemotely = STREAM_CFG_LOCAL;
            break;
        case NvComputer::RI_VPN:
            // It looks like our route to this PC is over a VPN, so cap at 1024 bytes.
            // Treat it as remote even if the target address is in RFC 1918 address space.
            m_StreamConfig.streamingRemotely = STREAM_CFG_REMOTE;
            m_StreamConfig.packetSize = 1024;
            break;
        default:
            // If we don't have reachability info, let moonlight-common-c decide.
            m_StreamConfig.streamingRemotely = STREAM_CFG_AUTO;
            break;
        }
    }

    // If the user has chosen YUV444 without adjusting the bitrate but the host doesn't
    // support YUV444 streaming, use the default non-444 bitrate for the stream instead.
    // This should provide equivalent image quality for YUV420 as the stream would have
    // had if the host supported YUV444 (though obviously with 4:2:0 subsampling).
    // If the user has adjusted the bitrate from default, we'll assume they really wanted
    // that value and not second guess them.
    if (m_Preferences->enableYUV444 &&
        !(m_StreamConfig.supportedVideoFormats & VIDEO_FORMAT_MASK_YUV444) &&
        m_StreamConfig.bitrate == StreamingPreferences::getDefaultBitrate(m_StreamConfig.width,
                                                                          m_StreamConfig.height,
                                                                          m_StreamConfig.fps,
                                                                          true)) {
        m_StreamConfig.bitrate = StreamingPreferences::getDefaultBitrate(m_StreamConfig.width,
                                                                         m_StreamConfig.height,
                                                                         m_StreamConfig.fps,
                                                                         false);
    }

    int err = LiStartConnection(&hostInfo, &m_StreamConfig, &k_ConnCallbacks,
                                &m_VideoCallbacks, &m_AudioCallbacks,
                                NULL, 0, NULL, 0);
    if (err != 0) {
        // We already displayed an error dialog in the stage failure
        // listener.
        return false;
    }

    emit connectionStarted();
    return true;
}

void Session::flushWindowEvents()
{
    // Pump events to ensure all pending OS events are posted
    SDL_PumpEvents();

    // Insert a barrier to discard any additional window events.
    // We don't use SDL_FlushEvent() here because it could cause
    // important events to be lost.
    m_FlushingWindowEventsRef++;

    // This event will cause us to set m_FlushingWindowEvents back to false.
    SDL_Event flushEvent = {};
    flushEvent.type = SDL_USEREVENT;
    flushEvent.user.code = SDL_CODE_FLUSH_WINDOW_EVENT_BARRIER;
    SDL_PushEvent(&flushEvent);
}

void Session::setShouldExitAfterQuit()
{
    m_ShouldExitAfterQuit = true;
}

class ExecThread : public QThread
{
public:
    ExecThread(Session* session) :
        QThread(nullptr),
        m_Session(session)
    {
        setObjectName("Session Exec");
    }

    void run() override
    {
        m_Session->execInternal();
    }

    Session* m_Session;
};

void Session::exec(QWindow* qtWindow)
{
    m_QtWindow = qtWindow;

    // Use a separate thread for the streaming session on X11 or Wayland
    // to ensure we don't stomp on Qt's GL context. This breaks when using
    // the Qt EGLFS backend, so we will restrict this to X11
    m_ThreadedExec = WMUtils::isRunningX11() || WMUtils::isRunningWayland();

    if (m_ThreadedExec) {
        // Run the streaming session on a separate thread for Linux/BSD
        ExecThread execThread(this);
        execThread.start();

        // Until the SDL streaming window is created, we should continue
        // to update the Qt UI to allow warning messages to display and
        // make sure that the Qt window can hide itself.
        while (!execThread.wait(10) && m_Window == nullptr) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            QCoreApplication::sendPostedEvents();
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QCoreApplication::sendPostedEvents();

        // SDL is in charge now. Wait until the streaming thread exits
        // to further update the Qt window.
        execThread.wait();
    }
    else {
        // Run the streaming session on the main thread for Windows and macOS
        execInternal();
    }
}

void Session::execInternal()
{
    // Complete initialization in this deferred context to avoid
    // calling expensive functions in the constructor (during the
    // process of loading the StreamSegue).
    //
    // NB: This initializes the SDL video subsystem, so it must be
    // called on the main thread.
    if (!initialize()) {
        emit sessionFinished(0);
        emit readyForDeletion();
        return;
    }

    // Wait for any old session to finish cleanup
    s_ActiveSessionSemaphore.acquire();

    // We're now active
    s_ActiveSession = this;

    // Initialize the gamepad code with our preferences
    // NB: m_InputHandler must be initialize before starting the connection.
    m_InputHandler = new SdlInputHandler(*m_Preferences, m_StreamConfig.width, m_StreamConfig.height);

    AsyncConnectionStartThread asyncConnThread(this);
    if (!m_ThreadedExec) {
        // Kick off the async connection thread while we sit here and pump the event loop
        asyncConnThread.start();
        while (!asyncConnThread.wait(10)) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            QCoreApplication::sendPostedEvents();
        }

        // Pump the event loop one last time to ensure we pick up any events from
        // the thread that happened while it was in the final successful QThread::wait().
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QCoreApplication::sendPostedEvents();
    }
    else {
        // We're already in a separate thread so run the connection operations
        // synchronously and don't pump the event loop. The main thread is already
        // pumping the event loop for us.
        asyncConnThread.run();
    }

    // If the connection failed, clean up and abort the connection.
    if (!m_AsyncConnectionSuccess) {
        delete m_InputHandler;
        m_InputHandler = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        QThreadPool::globalInstance()->start(new DeferredSessionCleanupTask(this));
        return;
    }

    int x, y, width, height;
    getWindowDimensions(x, y, width, height);

#ifdef STEAM_LINK
    // We need a little delay before creating the window or we will trigger some kind
    // of graphics driver bug on Steam Link that causes a jagged overlay to appear in
    // the top right corner randomly.
    SDL_Delay(500);
#endif

    // Request at least 8 bits per color for GL
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);

    // Disable depth and stencil buffers
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    // We always want a resizable window with High DPI enabled
    Uint32 defaultWindowFlags = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;

    // If we're starting in windowed mode and the Moonlight GUI is maximized or
    // minimized, match that with the streaming window.
    if (!m_IsFullScreen && m_QtWindow != nullptr) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        // Qt 5.10+ can propagate multiple states together
        if (m_QtWindow->windowStates() & Qt::WindowMaximized) {
            defaultWindowFlags |= SDL_WINDOW_MAXIMIZED;
        }
        if (m_QtWindow->windowStates() & Qt::WindowMinimized) {
            defaultWindowFlags |= SDL_WINDOW_MINIMIZED;
        }
#else
        // Qt 5.9 only supports a single state at a time
        if (m_QtWindow->windowState() == Qt::WindowMaximized) {
            defaultWindowFlags |= SDL_WINDOW_MAXIMIZED;
        }
        else if (m_QtWindow->windowState() == Qt::WindowMinimized) {
            defaultWindowFlags |= SDL_WINDOW_MINIMIZED;
        }
#endif
    }

    // We use only the computer name on macOS to match Apple conventions where the
    // app name is featured in the menu bar and the document name is in the title bar.
#ifdef Q_OS_DARWIN
    std::string windowName = QString(m_Computer->name).toStdString();
#else
    std::string windowName = QString(m_Computer->name + " - Moonlight").toStdString();
#endif

    m_Window = SDL_CreateWindow(windowName.c_str(),
                                x,
                                y,
                                width,
                                height,
                                defaultWindowFlags | StreamUtils::getPlatformWindowFlags());
    if (!m_Window) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL_CreateWindow() failed with platform flags: %s",
                    SDL_GetError());

        m_Window = SDL_CreateWindow(windowName.c_str(),
                                    x,
                                    y,
                                    width,
                                    height,
                                    defaultWindowFlags);
        if (!m_Window) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL_CreateWindow() failed: %s",
                         SDL_GetError());

            delete m_InputHandler;
            m_InputHandler = nullptr;
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            QThreadPool::globalInstance()->start(new DeferredSessionCleanupTask(this));
            return;
        }
    }

    // HACK: Remove once proper Dark Mode support lands in SDL
#ifdef Q_OS_WIN32
    if (m_QtWindow != nullptr) {
        BOOL darkModeEnabled;

        // Query whether dark mode is enabled for our Qt window (which tracks the OS dark mode state)
        if (FAILED(DwmGetWindowAttribute((HWND)m_QtWindow->winId(), DWMWA_USE_IMMERSIVE_DARK_MODE, &darkModeEnabled, sizeof(darkModeEnabled))) &&
            FAILED(DwmGetWindowAttribute((HWND)m_QtWindow->winId(), DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &darkModeEnabled, sizeof(darkModeEnabled)))) {
            darkModeEnabled = FALSE;
        }

        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);

        if (SDL_GetWindowWMInfo(m_Window, &info) && info.subsystem == SDL_SYSWM_WINDOWS) {
            // If dark mode is enabled, propagate that to our SDL window
            if (darkModeEnabled) {
                if (FAILED(DwmSetWindowAttribute(info.info.win.window, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkModeEnabled, sizeof(darkModeEnabled)))) {
                    DwmSetWindowAttribute(info.info.win.window, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &darkModeEnabled, sizeof(darkModeEnabled));
                }

                // Toggle non-client rendering off and back on to ensure dark mode takes effect on Windows 10.
                // DWM doesn't seem to correctly invalidate the non-client area after enabling dark mode.
                DWMNCRENDERINGPOLICY ncPolicy = DWMNCRP_DISABLED;
                DwmSetWindowAttribute(info.info.win.window, DWMWA_NCRENDERING_POLICY, &ncPolicy, sizeof(ncPolicy));
                ncPolicy = DWMNCRP_ENABLED;
                DwmSetWindowAttribute(info.info.win.window, DWMWA_NCRENDERING_POLICY, &ncPolicy, sizeof(ncPolicy));
            }
        }
    }
#endif

    m_InputHandler->setWindow(m_Window);

    QSvgRenderer svgIconRenderer(QString(":/res/moonlight.svg"));
    QImage svgImage(ICON_SIZE, ICON_SIZE, QImage::Format_RGBA8888);
    svgImage.fill(0);

    QPainter svgPainter(&svgImage);
    svgIconRenderer.render(&svgPainter);
    SDL_Surface* iconSurface = SDL_CreateRGBSurfaceWithFormatFrom((void*)svgImage.constBits(),
                                                                  svgImage.width(),
                                                                  svgImage.height(),
                                                                  32,
                                                                  4 * svgImage.width(),
                                                                  SDL_PIXELFORMAT_RGBA32);
#ifndef Q_OS_DARWIN
    // Other platforms seem to preserve our Qt icon when creating a new window.
    if (iconSurface != nullptr) {
        // This must be called before entering full-screen mode on Windows
        // or our icon will not persist when toggling to windowed mode
        SDL_SetWindowIcon(m_Window, iconSurface);
    }
#endif

    // Update the window display mode based on our current monitor
    // for if/when we enter full-screen mode.
    updateOptimalWindowDisplayMode();

    // Enter full screen if requested
    if (m_IsFullScreen) {
        SDL_SetWindowFullscreen(m_Window, m_FullScreenFlag);
    }

    bool needsFirstEnterCapture = false;
    bool needsPostDecoderCreationCapture = false;

    // HACK: For Wayland, we wait until we get the first SDL_WINDOWEVENT_ENTER
    // event where it seems to work consistently on GNOME. For other platforms,
    // especially where SDL may call SDL_RecreateWindow(), we must only capture
    // after the decoder is created.
    if (strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        // Native Wayland: Capture on SDL_WINDOWEVENT_ENTER
        needsFirstEnterCapture = true;
    }
    else {
        // X11/XWayland: Capture after decoder creation
        needsPostDecoderCreationCapture = true;
    }

    // Stop text input. SDL enables it by default
    // when we initialize the video subsystem, but this
    // causes an IME popup when certain keys are held down
    // on macOS.
    SDL_StopTextInput();

    // Disable the screen saver if requested
    if (m_Preferences->keepAwake) {
        SDL_DisableScreenSaver();
    }

    // Hide Qt's fake mouse cursor on EGLFS systems
    if (QGuiApplication::platformName() == "eglfs") {
        QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
    }

    // Set timer resolution to 1 ms on Windows for greater
    // sleep precision and more accurate callback timing.
    SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "1");

    int currentDisplayIndex = SDL_GetWindowDisplayIndex(m_Window);

    // Now that we're about to stream, any SDL_QUIT event is expected
    // unless it comes from the connection termination callback where
    // (m_UnexpectedTermination is set back to true).
    m_UnexpectedTermination = false;

    // Start rich presence to indicate we're in game
    RichPresenceManager presence(*m_Preferences, m_App.name);

    // Toggle the stats overlay if requested by the user
    m_OverlayManager.setOverlayState(Overlay::OverlayDebug, m_Preferences->showPerformanceOverlay);

    // Switch to async logging mode when we enter the SDL loop
    StreamUtils::enterAsyncLoggingMode();

    // Hijack this thread to be the SDL main thread. We have to do this
    // because we want to suspend all Qt processing until the stream is over.
    SDL_Event event;
    for (;;) {
#if SDL_VERSION_ATLEAST(2, 0, 18) && !defined(STEAM_LINK)
        // SDL 2.0.18 has a proper wait event implementation that uses platform
        // support to block on events rather than polling on Windows, macOS, X11,
        // and Wayland. It will fall back to 1 ms polling if a joystick is
        // connected, so we don't use it for STEAM_LINK to ensure we only poll
        // every 10 ms.
        //
        // NB: This behavior was introduced in SDL 2.0.16, but had a few critical
        // issues that could cause indefinite timeouts, delayed joystick detection,
        // and other problems.
        if (!SDL_WaitEventTimeout(&event, 1000)) {
            presence.runCallbacks();
            continue;
        }
#else
        // We explicitly use SDL_PollEvent() and SDL_Delay() because
        // SDL_WaitEvent() has an internal SDL_Delay(10) inside which
        // blocks this thread too long for high polling rate mice and high
        // refresh rate displays.
        if (!SDL_PollEvent(&event)) {
#ifndef STEAM_LINK
            SDL_Delay(1);
#else
            // Waking every 1 ms to process input is too much for the low performance
            // ARM core in the Steam Link, so we will wait 10 ms instead.
            SDL_Delay(10);
#endif
            presence.runCallbacks();
            continue;
        }
#endif
        switch (event.type) {
        case SDL_QUIT:
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Quit event received");
            goto DispatchDeferredCleanup;

        case SDL_USEREVENT:
            switch (event.user.code) {
            case SDL_CODE_FRAME_READY:
                if (m_VideoDecoder != nullptr) {
                    m_VideoDecoder->renderFrameOnMainThread();
                }
                break;
            case SDL_CODE_FLUSH_WINDOW_EVENT_BARRIER:
                m_FlushingWindowEventsRef--;
                break;
            case SDL_CODE_GAMECONTROLLER_RUMBLE:
                m_InputHandler->rumble((uint16_t)(uintptr_t)event.user.data1,
                                       (uint16_t)((uintptr_t)event.user.data2 >> 16),
                                       (uint16_t)((uintptr_t)event.user.data2 & 0xFFFF));
                break;
            case SDL_CODE_GAMECONTROLLER_RUMBLE_TRIGGERS:
                m_InputHandler->rumbleTriggers((uint16_t)(uintptr_t)event.user.data1,
                                               (uint16_t)((uintptr_t)event.user.data2 >> 16),
                                               (uint16_t)((uintptr_t)event.user.data2 & 0xFFFF));
                break;
            case SDL_CODE_GAMECONTROLLER_SET_MOTION_EVENT_STATE:
                m_InputHandler->setMotionEventState((uint16_t)(uintptr_t)event.user.data1,
                                                    (uint8_t)((uintptr_t)event.user.data2 >> 16),
                                                    (uint16_t)((uintptr_t)event.user.data2 & 0xFFFF));
                break;
            case SDL_CODE_GAMECONTROLLER_SET_CONTROLLER_LED:
                m_InputHandler->setControllerLED((uint16_t)(uintptr_t)event.user.data1,
                                                 (uint8_t)((uintptr_t)event.user.data2 >> 16),
                                                 (uint8_t)((uintptr_t)event.user.data2 >> 8),
                                                 (uint8_t)((uintptr_t)event.user.data2));
                break;
            case SDL_CODE_GAMECONTROLLER_SET_ADAPTIVE_TRIGGERS:
                m_InputHandler->setAdaptiveTriggers((uint16_t)(uintptr_t)event.user.data1,
                                                    (DualSenseOutputReport *)event.user.data2);
                break;
            default:
                SDL_assert(false);
            }
            break;

        case SDL_WINDOWEVENT:
            // Early handling of some events
            switch (event.window.event) {
            case SDL_WINDOWEVENT_FOCUS_LOST:
                if (m_Preferences->muteOnFocusLoss) {
                    m_AudioMuted = true;
                }
                m_InputHandler->notifyFocusLost();
                break;
            case SDL_WINDOWEVENT_FOCUS_GAINED:
                if (m_Preferences->muteOnFocusLoss) {
                    m_AudioMuted = false;
                }
                m_InputHandler->notifyFocusGained();
                break;
            case SDL_WINDOWEVENT_LEAVE:
                m_InputHandler->notifyMouseLeave();
                break;
            }

            presence.runCallbacks();

            // Capture the mouse on SDL_WINDOWEVENT_ENTER if needed
            if (needsFirstEnterCapture && event.window.event == SDL_WINDOWEVENT_ENTER) {
                m_InputHandler->setCaptureActive(true);
                needsFirstEnterCapture = false;
            }

            // We want to recreate the decoder for resizes (full-screen toggles) and the initial shown event.
            // We use SDL_WINDOWEVENT_SIZE_CHANGED rather than SDL_WINDOWEVENT_RESIZED because the latter doesn't
            // seem to fire when switching from windowed to full-screen on X11.
            if (event.window.event != SDL_WINDOWEVENT_SIZE_CHANGED &&
                (event.window.event != SDL_WINDOWEVENT_SHOWN || m_VideoDecoder != nullptr)) {
                // Check that the window display hasn't changed. If it has, we want
                // to recreate the decoder to allow it to adapt to the new display.
                // This will allow Pacer to pull the new display refresh rate.
#if SDL_VERSION_ATLEAST(2, 0, 18)
                // On SDL 2.0.18+, there's an event for this specific situation
                if (event.window.event != SDL_WINDOWEVENT_DISPLAY_CHANGED) {
                    break;
                }
#else
                // Prior to SDL 2.0.18, we must check the display index for each window event
                if (SDL_GetWindowDisplayIndex(m_Window) == currentDisplayIndex) {
                    break;
                }
#endif
            }
#ifdef Q_OS_WIN32
            // We can get a resize event after being minimized. Recreating the renderer at that time can cause
            // us to start drawing on the screen even while our window is minimized. Minimizing on Windows also
            // moves the window to -32000, -32000 which can cause a false window display index change. Avoid
            // that whole mess by never recreating the decoder if we're minimized.
            else if (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_MINIMIZED) {
                break;
            }
#endif

            if (m_FlushingWindowEventsRef > 0) {
                // Ignore window events for renderer reset if flushing
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Dropping window event during flush: %d (%d %d)",
                            event.window.event,
                            event.window.data1,
                            event.window.data2);
                break;
            }

            // Allow the renderer to handle the state change without being recreated
            if (m_VideoDecoder) {
                bool forceRecreation = false;

                WINDOW_STATE_CHANGE_INFO windowChangeInfo = {};
                windowChangeInfo.window = m_Window;

                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    windowChangeInfo.stateChangeFlags |= WINDOW_STATE_CHANGE_SIZE;

                    windowChangeInfo.width = event.window.data1;
                    windowChangeInfo.height = event.window.data2;
                }

                int newDisplayIndex = SDL_GetWindowDisplayIndex(m_Window);
                if (newDisplayIndex != currentDisplayIndex) {
                    windowChangeInfo.stateChangeFlags |= WINDOW_STATE_CHANGE_DISPLAY;

                    windowChangeInfo.displayIndex = newDisplayIndex;

                    // If the refresh rates have changed, we will need to go through the full
                    // decoder recreation path to ensure Pacer is switched to the new display
                    // and that we apply any V-Sync disablement rules that may be needed for
                    // this display.
                    SDL_DisplayMode oldMode, newMode;
                    if (SDL_GetCurrentDisplayMode(currentDisplayIndex, &oldMode) < 0 ||
                            SDL_GetCurrentDisplayMode(newDisplayIndex, &newMode) < 0 ||
                            oldMode.refresh_rate != newMode.refresh_rate) {
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                    "Forcing renderer recreation due to refresh rate change between displays");
                        forceRecreation = true;
                    }
                }

                if (!forceRecreation && m_VideoDecoder->notifyWindowChanged(&windowChangeInfo)) {
                    // Update the window display mode based on our current monitor
                    // NB: Avoid a useless modeset by only doing this if it changed.
                    if (newDisplayIndex != currentDisplayIndex) {
                        currentDisplayIndex = newDisplayIndex;
                        updateOptimalWindowDisplayMode();
                    }

                    break;
                }
            }

            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Recreating renderer for window event: %d (%d %d)",
                        event.window.event,
                        event.window.data1,
                        event.window.data2);

            // Fall through
        case SDL_RENDER_DEVICE_RESET:
        case SDL_RENDER_TARGETS_RESET:

            if (event.type != SDL_WINDOWEVENT) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Recreating renderer by internal request: %d",
                            event.type);
            }

            SDL_LockMutex(m_DecoderLock);

            // Destroy the old decoder
            delete m_VideoDecoder;

            // Insert a barrier to discard any additional window events
            // that could cause the renderer to be and recreated again.
            // We don't use SDL_FlushEvent() here because it could cause
            // important events to be lost.
            flushWindowEvents();

            // Update the window display mode based on our current monitor
            // NB: Avoid a useless modeset by only doing this if it changed.
            if (currentDisplayIndex != SDL_GetWindowDisplayIndex(m_Window)) {
                currentDisplayIndex = SDL_GetWindowDisplayIndex(m_Window);
                updateOptimalWindowDisplayMode();
            }

            // Now that the old decoder is dead, flush any events it may
            // have queued to reset itself (if this reset was the result
            // of state loss).
            SDL_PumpEvents();
            SDL_FlushEvent(SDL_RENDER_DEVICE_RESET);
            SDL_FlushEvent(SDL_RENDER_TARGETS_RESET);

            {
                // If the stream exceeds the display refresh rate (plus some slack),
                // forcefully disable V-sync to allow the stream to render faster
                // than the display.
                int displayHz = StreamUtils::getDisplayRefreshRate(m_Window);
                bool enableVsync = m_Preferences->enableVsync;
                if (displayHz + 5 < m_StreamConfig.fps) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Disabling V-sync because refresh rate limit exceeded");
                    enableVsync = false;
                }

                // Choose a new decoder (hopefully the same one, but possibly
                // not if a GPU was removed or something).
                if (!chooseDecoder(m_Preferences->videoDecoderSelection,
                                   m_Window, m_ActiveVideoFormat, m_ActiveVideoWidth,
                                   m_ActiveVideoHeight, m_ActiveVideoFrameRate,
                                   enableVsync,
                                   enableVsync && m_Preferences->framePacing,
                                   false,
                                   s_ActiveSession->m_VideoDecoder)) {
                    SDL_UnlockMutex(m_DecoderLock);
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "Failed to recreate decoder after reset");
                    emit displayLaunchError(tr("Unable to initialize video decoder. Please check your streaming settings and try again."));
                    goto DispatchDeferredCleanup;
                }

                // As of SDL 2.0.12, SDL_RecreateWindow() doesn't carry over mouse capture
                // or mouse hiding state to the new window. By capturing after the decoder
                // is set up, this ensures the window re-creation is already done.
                if (needsPostDecoderCreationCapture) {
                    m_InputHandler->setCaptureActive(true);
                    needsPostDecoderCreationCapture = false;
                }
            }

            // Request an IDR frame to complete the reset
            LiRequestIdrFrame();

            // Set HDR mode. We may miss the callback if we're in the middle
            // of recreating our decoder at the time the HDR transition happens.
            m_VideoDecoder->setHdrMode(LiGetCurrentHostDisplayHdrMode());

            // After a window resize, we need to reset the pointer lock region
            m_InputHandler->updatePointerRegionLock();

            SDL_UnlockMutex(m_DecoderLock);
            break;

        case SDL_KEYUP:
        case SDL_KEYDOWN:
            presence.runCallbacks();
            m_InputHandler->handleKeyEvent(&event.key);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            presence.runCallbacks();
            m_InputHandler->handleMouseButtonEvent(&event.button);
            break;
        case SDL_MOUSEMOTION:
            m_InputHandler->handleMouseMotionEvent(&event.motion);
            break;
        case SDL_MOUSEWHEEL:
            m_InputHandler->handleMouseWheelEvent(&event.wheel);
            break;
        case SDL_CONTROLLERAXISMOTION:
            m_InputHandler->handleControllerAxisEvent(&event.caxis);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            presence.runCallbacks();
            m_InputHandler->handleControllerButtonEvent(&event.cbutton);
            break;
#if SDL_VERSION_ATLEAST(2, 0, 14)
        case SDL_CONTROLLERSENSORUPDATE:
            m_InputHandler->handleControllerSensorEvent(&event.csensor);
            break;
        case SDL_CONTROLLERTOUCHPADDOWN:
        case SDL_CONTROLLERTOUCHPADUP:
        case SDL_CONTROLLERTOUCHPADMOTION:
            m_InputHandler->handleControllerTouchpadEvent(&event.ctouchpad);
            break;
#endif
#if SDL_VERSION_ATLEAST(2, 24, 0)
        case SDL_JOYBATTERYUPDATED:
            m_InputHandler->handleJoystickBatteryEvent(&event.jbattery);
            break;
#endif
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            m_InputHandler->handleControllerDeviceEvent(&event.cdevice);
            break;
        case SDL_JOYDEVICEADDED:
            m_InputHandler->handleJoystickArrivalEvent(&event.jdevice);
            break;
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP:
            m_InputHandler->handleTouchFingerEvent(&event.tfinger);
            break;
        case SDL_DISPLAYEVENT:
            switch (event.display.event) {
            case SDL_DISPLAYEVENT_CONNECTED:
            case SDL_DISPLAYEVENT_DISCONNECTED:
                m_InputHandler->updatePointerRegionLock();
                break;
            }
            break;
        }
    }

DispatchDeferredCleanup:
    // Switch back to synchronous logging mode
    StreamUtils::exitAsyncLoggingMode();

    // Uncapture the mouse and hide the window immediately,
    // so we can return to the Qt GUI ASAP.
    m_InputHandler->setCaptureActive(false);
    SDL_EnableScreenSaver();
    SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "0");
    if (QGuiApplication::platformName() == "eglfs") {
        QGuiApplication::restoreOverrideCursor();
    }

    // Raise any keys that are still down
    m_InputHandler->raiseAllKeys();

    // Destroy the input handler now. This must be destroyed
    // before allowwing the UI to continue execution or it could
    // interfere with SDLGamepadKeyNavigation.
    delete m_InputHandler;
    m_InputHandler = nullptr;

    // Destroy the decoder, since this must be done on the main thread
    // NB: This must happen before LiStopConnection() for pull-based
    // decoders.
    SDL_LockMutex(m_DecoderLock);
    delete m_VideoDecoder;
    m_VideoDecoder = nullptr;
    SDL_UnlockMutex(m_DecoderLock);

    // Propagate state changes from the SDL window back to the Qt window
    //
    // NB: We're making a conscious decision not to propagate the maximized
    // or normal state of the window here. The thinking is that users may
    // routinely maximize the streaming window simply to view the stream
    // in a larger window, but they don't necessarily want the UI in such
    // a large window.
    if (!m_IsFullScreen && m_QtWindow != nullptr && m_Window != nullptr) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        if (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_MINIMIZED) {
            m_QtWindow->setWindowStates(m_QtWindow->windowStates() | Qt::WindowMinimized);
        }
        else if (m_QtWindow->windowStates() & Qt::WindowMinimized) {
            m_QtWindow->setWindowStates(m_QtWindow->windowStates() & ~Qt::WindowMinimized);
        }
#else
        if (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_MINIMIZED) {
            m_QtWindow->setWindowState(Qt::WindowMinimized);
        }
        else if (m_QtWindow->windowState() & Qt::WindowMinimized) {
            m_QtWindow->setWindowState(Qt::WindowNoState);
        }
#endif
    }

    // This must be called after the decoder is deleted, because
    // the renderer may want to interact with the window
    SDL_DestroyWindow(m_Window);

    if (iconSurface != nullptr) {
        SDL_FreeSurface(iconSurface);
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    // Cleanup can take a while, so dispatch it to a worker thread.
    // When it is complete, it will release our s_ActiveSessionSemaphore
    // reference.
    QThreadPool::globalInstance()->start(new DeferredSessionCleanupTask(this));
}
