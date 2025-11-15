#pragma once

#include "renderer.h"

#include <interface/mmal/mmal.h>
#include <interface/mmal/util/mmal_util.h>
#include <interface/mmal/util/mmal_util_params.h>
#include <interface/mmal/util/mmal_default_components.h>

class MmalRenderer : public IFFmpegRenderer {
public:
    MmalRenderer();
    virtual ~MmalRenderer() override;
    virtual bool initialize(PDECODER_PARAMETERS params) override;
    virtual bool prepareDecoderContext(AVCodecContext* context, AVDictionary** options) override;
    virtual void prepareToRender() override;
    virtual void renderFrame(AVFrame* frame) override;
    virtual enum AVPixelFormat getPreferredPixelFormat(int videoFormat) override;
    virtual bool needsTestFrame() override;
    virtual int getRendererAttributes() override;
    virtual int getDecoderColorspace() override;

private:
    static void InputPortCallback(MMAL_PORT_T* port, MMAL_BUFFER_HEADER_T* buffer);
    bool getDtDeviceStatus(QString name, bool ifUnknown);
    bool isMmalOverlaySupported();
    void updateDisplayRegion();

    MMAL_COMPONENT_T* m_Renderer;
    MMAL_PORT_T* m_InputPort;

    SDL_Renderer* m_BackgroundRenderer;
    SDL_Window* m_Window;
    int m_VideoWidth, m_VideoHeight;
    int m_LastWindowPosX, m_LastWindowPosY;
};

