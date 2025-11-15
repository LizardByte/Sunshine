#pragma once

#include "decoder.h"
#include "overlaymanager.h"

#include <SLVideo.h>

class SLVideoDecoder : public IVideoDecoder, public Overlay::IOverlayRenderer
{
public:
    SLVideoDecoder(bool testOnly);
    virtual ~SLVideoDecoder();
    virtual bool initialize(PDECODER_PARAMETERS params) override;
    virtual bool isHardwareAccelerated() override;
    virtual bool isAlwaysFullScreen() override;
    virtual int getDecoderCapabilities() override;
    virtual int getDecoderColorspace() override;
    virtual int getDecoderColorRange() override;
    virtual QSize getDecoderMaxResolution() override;
    virtual int submitDecodeUnit(PDECODE_UNIT du) override;
    virtual void notifyOverlayUpdated(Overlay::OverlayType) override;

    // Unused since rendering is done directly from the decode thread
    virtual void renderFrameOnMainThread() override {}

    // HDR is not supported by SLVideo
    virtual void setHdrMode(bool) override {}
    virtual bool isHdrSupported() override {
        return false;
    }

    // Window state changes are not supported by SLVideo
    virtual bool notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO) {
        return false;
    }

private:
    static void slLogCallback(void* context, ESLVideoLog logLevel, const char* message);

    CSLVideoContext* m_VideoContext;
    CSLVideoStream* m_VideoStream;
    CSLVideoOverlay* m_Overlay;

    int m_ViewportWidth;
    int m_ViewportHeight;
};
