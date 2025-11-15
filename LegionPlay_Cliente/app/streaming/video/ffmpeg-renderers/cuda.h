#pragma once

#include "renderer.h"

#include <ffnvcodec/dynlink_loader.h>

extern "C" {
    #include <libavutil/hwcontext_cuda.h>
}

class CUDARenderer : public IFFmpegRenderer {
public:
    CUDARenderer();
    virtual ~CUDARenderer() override;
    virtual bool initialize(PDECODER_PARAMETERS) override;
    virtual bool prepareDecoderContext(AVCodecContext* context, AVDictionary** options) override;
    virtual void renderFrame(AVFrame* frame) override;
    virtual bool needsTestFrame() override;
    virtual bool isDirectRenderingSupported() override;
    virtual int getDecoderCapabilities() override;

private:
    AVBufferRef* m_HwContext;
};

#define NV12_PLANES 2

// Helper class used by SDLRenderer to read our CUDA frame
class CUDAGLInteropHelper {
public:
    CUDAGLInteropHelper(AVHWDeviceContext* context);
    ~CUDAGLInteropHelper();

    bool registerBoundTextures();
    void unregisterTextures();

    bool copyCudaFrameToTextures(AVFrame* frame);

private:
    CudaFunctions* m_Funcs;
    AVCUDADeviceContext* m_Context;
    CUgraphicsResource m_Resources[NV12_PLANES];
};
