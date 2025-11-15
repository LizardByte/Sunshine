#pragma once

#include "renderer.h"

extern "C" {
#include <vdpau/vdpau.h>
#include <vdpau/vdpau_x11.h>
#include <libavutil/hwcontext_vdpau.h>
}

class VDPAURenderer : public IFFmpegRenderer
{
public:
    VDPAURenderer(int decoderSelectionPass);
    virtual ~VDPAURenderer() override;
    virtual bool initialize(PDECODER_PARAMETERS params) override;
    virtual bool prepareDecoderContext(AVCodecContext* context, AVDictionary** options) override;
    virtual void notifyOverlayUpdated(Overlay::OverlayType type) override;
    virtual void waitToRender() override;
    virtual void renderFrame(AVFrame* frame) override;
    virtual bool needsTestFrame() override;
    virtual int getDecoderColorspace() override;
    virtual int getDecoderCapabilities() override;

private:
    void renderOverlay(VdpOutputSurface destination, Overlay::OverlayType type);

    int m_DecoderSelectionPass;
    uint32_t m_VideoWidth, m_VideoHeight;
    uint32_t m_DisplayWidth, m_DisplayHeight;
    AVBufferRef* m_HwContext;
    VdpPresentationQueueTarget m_PresentationQueueTarget;
    VdpPresentationQueue m_PresentationQueue;
    VdpVideoMixer m_VideoMixer;
    VdpRGBAFormat m_OutputSurfaceFormat;
    VdpDevice m_Device;

    // We just have a single mutex to protect all overlay slots.
    // This is fine because the majority of time spent in the mutex
    // is by the render thread, which cannot contend with itself
    // because overlays are rendered sequentially.
    SDL_mutex* m_OverlayMutex;
    VdpBitmapSurface m_OverlaySurface[Overlay::OverlayMax];
    VdpRect m_OverlayRect[Overlay::OverlayMax];
    VdpOutputSurfaceRenderBlendState m_OverlayBlendState;

#define OUTPUT_SURFACE_COUNT 3
    VdpOutputSurface m_OutputSurface[OUTPUT_SURFACE_COUNT];
    int m_NextSurfaceIndex;

#define OUTPUT_SURFACE_FORMAT_COUNT 2
    static const VdpRGBAFormat k_OutputFormats8Bit[OUTPUT_SURFACE_FORMAT_COUNT];
    static const VdpRGBAFormat k_OutputFormats10Bit[OUTPUT_SURFACE_FORMAT_COUNT];

    VdpGetErrorString* m_VdpGetErrorString;
    VdpPresentationQueueTargetDestroy* m_VdpPresentationQueueTargetDestroy;
    VdpVideoMixerCreate* m_VdpVideoMixerCreate;
    VdpVideoMixerDestroy* m_VdpVideoMixerDestroy;
    VdpVideoMixerRender* m_VdpVideoMixerRender;
    VdpPresentationQueueCreate* m_VdpPresentationQueueCreate;
    VdpPresentationQueueDestroy* m_VdpPresentationQueueDestroy;
    VdpPresentationQueueDisplay* m_VdpPresentationQueueDisplay;
    VdpPresentationQueueSetBackgroundColor* m_VdpPresentationQueueSetBackgroundColor;
    VdpPresentationQueueBlockUntilSurfaceIdle* m_VdpPresentationQueueBlockUntilSurfaceIdle;
    VdpOutputSurfaceCreate* m_VdpOutputSurfaceCreate;
    VdpOutputSurfaceDestroy* m_VdpOutputSurfaceDestroy;
    VdpOutputSurfaceQueryCapabilities* m_VdpOutputSurfaceQueryCapabilities;
    VdpBitmapSurfaceCreate* m_VdpBitmapSurfaceCreate;
    VdpBitmapSurfaceDestroy* m_VdpBitmapSurfaceDestroy;
    VdpBitmapSurfacePutBitsNative* m_VdpBitmapSurfacePutBitsNative;
    VdpOutputSurfaceRenderBitmapSurface* m_VdpOutputSurfaceRenderBitmapSurface;
    VdpVideoSurfaceGetParameters* m_VdpVideoSurfaceGetParameters;
    VdpGetInformationString* m_VdpGetInformationString;

    // X11 stuff
    VdpPresentationQueueTargetCreateX11* m_VdpPresentationQueueTargetCreateX11;
};


