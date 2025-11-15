#pragma once

#include "renderer.h"

class GenericHwAccelRenderer : public IFFmpegRenderer
{
public:
    GenericHwAccelRenderer(AVHWDeviceType hwDeviceType);
    virtual ~GenericHwAccelRenderer() override;
    virtual bool initialize(PDECODER_PARAMETERS) override;
    virtual bool prepareDecoderContext(AVCodecContext* context, AVDictionary** options) override;
    virtual void renderFrame(AVFrame* frame) override;
    virtual bool needsTestFrame() override;
    virtual bool isDirectRenderingSupported() override;
    virtual int getDecoderCapabilities() override;

private:
    AVHWDeviceType m_HwDeviceType;
    AVBufferRef* m_HwContext;
};

