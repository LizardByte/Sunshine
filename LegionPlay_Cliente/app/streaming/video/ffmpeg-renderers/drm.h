#pragma once

#include "renderer.h"
#include "swframemapper.h"

#ifdef HAVE_EGL
#include "eglimagefactory.h"
#endif

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <set>

// Newer libdrm headers have these HDR structs, but some older ones don't.
namespace DrmDefs
{
    // HDR structs is copied from linux include/linux/hdmi.h
    struct hdr_metadata_infoframe
    {
        uint8_t eotf;
        uint8_t metadata_type;

        struct
        {
            uint16_t x, y;
        } display_primaries[3];

        struct
        {
            uint16_t x, y;
        } white_point;

        uint16_t max_display_mastering_luminance;
        uint16_t min_display_mastering_luminance;

        uint16_t max_cll;
        uint16_t max_fall;
    };

    struct hdr_output_metadata
    {
        uint32_t metadata_type;

        union {
            struct hdr_metadata_infoframe hdmi_metadata_type1;
        };
    };
}

class DrmRenderer : public IFFmpegRenderer {
public:
    DrmRenderer(AVHWDeviceType hwDeviceType = AV_HWDEVICE_TYPE_NONE, IFFmpegRenderer *backendRenderer = nullptr);
    virtual ~DrmRenderer() override;
    virtual bool initialize(PDECODER_PARAMETERS params) override;
    virtual bool prepareDecoderContext(AVCodecContext* context, AVDictionary** options) override;
    virtual void prepareToRender() override;
    virtual void renderFrame(AVFrame* frame) override;
    virtual enum AVPixelFormat getPreferredPixelFormat(int videoFormat) override;
    virtual bool isPixelFormatSupported(int videoFormat, AVPixelFormat pixelFormat) override;
    virtual int getRendererAttributes() override;
    virtual bool needsTestFrame() override;
    virtual bool testRenderFrame(AVFrame* frame) override;
    virtual bool isDirectRenderingSupported() override;
    virtual int getDecoderColorspace() override;
    virtual void setHdrMode(bool enabled) override;
#ifdef HAVE_EGL
    virtual bool canExportEGL() override;
    virtual AVPixelFormat getEGLImagePixelFormat() override;
    virtual bool initializeEGL(EGLDisplay dpy, const EGLExtensions &ext) override;
    virtual ssize_t exportEGLImages(AVFrame *frame, EGLDisplay dpy, EGLImage images[EGL_MAX_PLANES]) override;
    virtual void freeEGLImages(EGLDisplay dpy, EGLImage[EGL_MAX_PLANES]) override;
#endif

private:
    bool getPropertyByName(drmModeObjectPropertiesPtr props, const char* name, uint64_t *value);
    const char* getDrmColorEncodingValue(AVFrame* frame);
    const char* getDrmColorRangeValue(AVFrame* frame);
    bool mapSoftwareFrame(AVFrame* frame, AVDRMFrameDescriptor* mappedFrame);
    bool addFbForFrame(AVFrame* frame, uint32_t* newFbId, bool testMode);
    static bool drmFormatMatchesVideoFormat(uint32_t drmFormat, int videoFormat);

    IFFmpegRenderer* m_BackendRenderer;
    SDL_Window* m_Window;
    bool m_DrmPrimeBackend;
    AVHWDeviceType m_HwDeviceType;
    AVBufferRef* m_HwContext;
    int m_DrmFd;
    bool m_DrmIsMaster;
    bool m_MustCloseDrmFd;
    bool m_SupportsDirectRendering;
    int m_VideoFormat;
    uint32_t m_ConnectorId;
    uint32_t m_EncoderId;
    uint32_t m_CrtcId;
    uint32_t m_PlaneId;
    uint32_t m_CurrentFbId;
    drmModePlanePtr m_Plane;
    drmModePropertyPtr m_ColorEncodingProp;
    drmModePropertyPtr m_ColorRangeProp;
    drmModePropertyPtr m_HdrOutputMetadataProp;
    drmModePropertyPtr m_ColorspaceProp;
    drmVersionPtr m_Version;
    uint32_t m_HdrOutputMetadataBlobId;
    SDL_Rect m_OutputRect;
    std::set<uint32_t> m_SupportedPlaneFormats;

    static constexpr int k_SwFrameCount = 2;
    SwFrameMapper m_SwFrameMapper;
    int m_CurrentSwFrameIdx;
    struct {
        uint32_t handle;
        uint32_t pitch;
        uint64_t size;
        uint8_t* mapping;
        int primeFd;
    } m_SwFrame[k_SwFrameCount];

#ifdef HAVE_EGL
    EglImageFactory m_EglImageFactory;
#endif
};

