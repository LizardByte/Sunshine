#pragma once

#include "renderer.h"

#ifdef __OBJC__
#import <Metal/Metal.h>
class VTBaseRenderer : public IFFmpegRenderer {
public:
    VTBaseRenderer(IFFmpegRenderer::RendererType type);
    virtual ~VTBaseRenderer();
    bool checkDecoderCapabilities(id<MTLDevice> device, PDECODER_PARAMETERS params);
    void setHdrMode(bool enabled) override;

protected:
    bool isAppleSilicon();

    bool m_HdrMetadataChanged; // Manual reset
    CFDataRef m_MasteringDisplayColorVolume;
    CFDataRef m_ContentLightLevelInfo;
};
#endif

// A factory is required to avoid pulling in
// incompatible Objective-C headers.

class VTMetalRendererFactory {
public:
    static
    IFFmpegRenderer* createRenderer(bool hwAccel);
};

class VTRendererFactory {
public:
    static
    IFFmpegRenderer* createRenderer();
};
