#pragma once

#include <Limelight.h>
#include "SDL_compat.h"
#include "settings/streamingpreferences.h"

#define SDL_CODE_FRAME_READY 0

#define MAX_SLICES 4

typedef struct _VIDEO_STATS {
    uint32_t receivedFrames;
    uint32_t decodedFrames;
    uint32_t renderedFrames;
    uint32_t totalFrames;
    uint32_t networkDroppedFrames;
    uint32_t pacerDroppedFrames;
    uint16_t minHostProcessingLatency;         // low-res from RTP
    uint16_t maxHostProcessingLatency;         // low-res from RTP
    uint32_t totalHostProcessingLatency;       // low-res from RTP
    uint32_t framesWithHostProcessingLatency;  // low-res from RTP
    uint64_t totalReassemblyTimeUs;            // high-res (1us)
    uint64_t totalDecodeTimeUs;                // high-res (1us)
    uint64_t totalPacerTimeUs;                 // high-res (1us)
    uint64_t totalRenderTimeUs;                // high-res (1us)
    uint32_t lastRtt;                          // low-res from enet (1ms)
    uint32_t lastRttVariance;                  // low-res from enet (1ms)
    double totalFps;                           // high-res
    double receivedFps;                        // high-res
    double decodedFps;                         // high-res
    double renderedFps;                        // high-res
    double videoMegabitsPerSec;                // current video bitrate in Mbps, not including FEC overhead
    uint64_t measurementStartUs;               // microseconds
} VIDEO_STATS, *PVIDEO_STATS;

typedef struct _DECODER_PARAMETERS {
    SDL_Window* window;
    StreamingPreferences::VideoDecoderSelection vds;

    int videoFormat;
    int width;
    int height;
    int frameRate;
    bool enableVsync;
    bool enableFramePacing;
    bool testOnly;
} DECODER_PARAMETERS, *PDECODER_PARAMETERS;

#define WINDOW_STATE_CHANGE_SIZE 0x01
#define WINDOW_STATE_CHANGE_DISPLAY 0x02

typedef struct _WINDOW_STATE_CHANGE_INFO {
    SDL_Window* window;
    uint32_t stateChangeFlags;

    // Populated if WINDOW_STATE_CHANGE_SIZE is set
    int width;
    int height;

    // Populated if WINDOW_STATE_CHANGE_DISPLAY is set
    int displayIndex;
} WINDOW_STATE_CHANGE_INFO, *PWINDOW_STATE_CHANGE_INFO;

class IVideoDecoder {
public:
    virtual ~IVideoDecoder() {}
    virtual bool initialize(PDECODER_PARAMETERS params) = 0;
    virtual bool isHardwareAccelerated() = 0;
    virtual bool isAlwaysFullScreen() = 0;
    virtual bool isHdrSupported() = 0;
    virtual int getDecoderCapabilities() = 0;
    virtual int getDecoderColorspace() = 0;
    virtual int getDecoderColorRange() = 0;
    virtual QSize getDecoderMaxResolution() = 0;
    virtual int submitDecodeUnit(PDECODE_UNIT du) = 0;
    virtual void renderFrameOnMainThread() = 0;
    virtual void setHdrMode(bool enabled) = 0;
    virtual bool notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO info) = 0;
};
