#pragma once

#include "renderer.h"

class SwFrameMapper
{
public:
    explicit SwFrameMapper(IFFmpegRenderer* renderer);
    void setVideoFormat(int videoFormat);
    AVFrame* getSwFrameFromHwFrame(AVFrame* hwFrame);

private:
    bool initializeReadBackFormat(AVBufferRef* hwFrameCtxRef, AVFrame* testFrame);

    IFFmpegRenderer* m_Renderer;
    int m_VideoFormat;
    enum AVPixelFormat m_SwPixelFormat;
    bool m_MapFrame;
};
