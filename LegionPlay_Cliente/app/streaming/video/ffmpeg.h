#pragma once

#include <functional>
#include <QQueue>
#include <set>

#include "../bandwidth.h"
#include "decoder.h"
#include "ffmpeg-renderers/renderer.h"
#include "ffmpeg-renderers/pacer/pacer.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

class FFmpegVideoDecoder : public IVideoDecoder {
public:
    FFmpegVideoDecoder(bool testOnly);
    virtual ~FFmpegVideoDecoder() override;
    virtual bool initialize(PDECODER_PARAMETERS params) override;
    virtual bool isHardwareAccelerated() override;
    virtual bool isAlwaysFullScreen() override;
    virtual bool isHdrSupported() override;
    virtual int getDecoderCapabilities() override;
    virtual int getDecoderColorspace() override;
    virtual int getDecoderColorRange() override;
    virtual QSize getDecoderMaxResolution() override;
    virtual int submitDecodeUnit(PDECODE_UNIT du) override;
    virtual void renderFrameOnMainThread() override;
    virtual void setHdrMode(bool enabled) override;
    virtual bool notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO info) override;

    virtual IFFmpegRenderer* getBackendRenderer();

private:
    bool completeInitialization(const AVCodec* decoder,
                                enum AVPixelFormat requiredFormat,
                                PDECODER_PARAMETERS params,
                                bool testFrame,
                                bool useAlternateFrontend);

    void stringifyVideoStats(VIDEO_STATS& stats, char* output, int length);

    void logVideoStats(VIDEO_STATS& stats, const char* title);

    void addVideoStats(VIDEO_STATS& src, VIDEO_STATS& dst);

    bool createFrontendRenderer(PDECODER_PARAMETERS params, bool useAlternateFrontend);

    static
    bool isDecoderMatchForParams(const AVCodec *decoder, PDECODER_PARAMETERS params);

    static
    bool isZeroCopyFormat(AVPixelFormat format);

    static
    int getAVCodecCapabilities(const AVCodec *codec);

    bool tryInitializeHwAccelDecoder(PDECODER_PARAMETERS params,
                                     int pass,
                                     QSet<const AVCodec*>& terminallyFailedHardwareDecoders);

    bool tryInitializeNonHwAccelDecoder(PDECODER_PARAMETERS params,
                                        bool requireZeroCopyFormat,
                                        QSet<const AVCodec*>& terminallyFailedHardwareDecoders);

    bool tryInitializeRendererForUnknownDecoder(const AVCodec* decoder,
                                                PDECODER_PARAMETERS params,
                                                bool tryHwAccel);

    bool tryInitializeRenderer(const AVCodec* decoder,
                               enum AVPixelFormat requiredFormat,
                               PDECODER_PARAMETERS params,
                               const AVCodecHWConfig* hwConfig,
                               IFFmpegRenderer::InitFailureReason* failureReason,
                               std::function<IFFmpegRenderer*()> createRendererFunc);

    static IFFmpegRenderer* createHwAccelRenderer(const AVCodecHWConfig* hwDecodeCfg, int pass);

    bool initializeRendererInternal(IFFmpegRenderer* renderer, PDECODER_PARAMETERS params);

    void reset();

    void writeBuffer(PLENTRY entry, int& offset);

    static
    enum AVPixelFormat ffGetFormat(AVCodecContext* context,
                                   const enum AVPixelFormat* pixFmts);

    void decoderThreadProc();

    static int decoderThreadProcThunk(void* context);

    AVPacket* m_Pkt;
    AVCodecContext* m_VideoDecoderCtx;
    enum AVPixelFormat m_RequiredPixelFormat;
    QByteArray m_DecodeBuffer;
    const AVCodecHWConfig* m_HwDecodeCfg;
    IFFmpegRenderer* m_BackendRenderer;
    IFFmpegRenderer* m_FrontendRenderer;
    int m_ConsecutiveFailedDecodes;
    Pacer* m_Pacer;
    BandwidthTracker m_BwTracker;
    VIDEO_STATS m_ActiveWndVideoStats;
    VIDEO_STATS m_LastWndVideoStats;
    VIDEO_STATS m_GlobalVideoStats;
    std::set<IFFmpegRenderer::RendererType> m_FailedRenderers;

    int m_FramesIn;
    int m_FramesOut;

    int m_LastFrameNumber;
    int m_StreamFps;
    int m_VideoFormat;
    bool m_NeedsSpsFixup;
    bool m_TestOnly;
    SDL_Thread* m_DecoderThread;
    SDL_atomic_t m_DecoderThreadShouldQuit;

    // Data buffers in the queued DU are not valid
    QQueue<DECODE_UNIT> m_FrameInfoQueue;

    static const uint8_t k_H264TestFrame[];
    static const uint8_t k_HEVCMainTestFrame[];
    static const uint8_t k_HEVCMain10TestFrame[];
    static const uint8_t k_AV1Main8TestFrame[];
    static const uint8_t k_AV1Main10TestFrame[];
    static const uint8_t k_h264High_444TestFrame[];
    static const uint8_t k_HEVCRExt8_444TestFrame[];
    static const uint8_t k_HEVCRExt10_444TestFrame[];
    static const uint8_t k_AV1High8_444TestFrame[];
    static const uint8_t k_AV1High10_444TestFrame[];

};
