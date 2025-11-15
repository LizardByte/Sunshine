// Nasty hack to avoid conflict between AVFoundation and
// libavutil both defining AVMediaType
#define AVMediaType AVMediaType_FFmpeg
#include "vt.h"
#include "pacer/pacer.h"
#undef AVMediaType

#include <SDL_syswm.h>
#include <Limelight.h>
#include "streaming/session.h"
#include "streaming/streamutils.h"
#include "path.h"

#import <Cocoa/Cocoa.h>
#import <VideoToolbox/VideoToolbox.h>
#import <AVFoundation/AVFoundation.h>
#import <dispatch/dispatch.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

extern "C" {
    #include <libavutil/pixdesc.h>
}

struct CscParams
{
    simd_float3 matrix[3];
    simd_float3 offsets;
};

struct ParamBuffer
{
    CscParams cscParams;
    simd_float2 chromaOffset;
    float bitnessScaleFactor;
};

struct Vertex
{
    simd_float4 position;
    simd_float2 texCoord;
};

#define MAX_VIDEO_PLANES 3

class VTMetalRenderer;

@interface DisplayLinkDelegate : NSObject <CAMetalDisplayLinkDelegate>

- (id)initWithRenderer:(VTMetalRenderer *)renderer;

@end

class VTMetalRenderer : public VTBaseRenderer
{
public:
    VTMetalRenderer(bool hwAccel)
        : VTBaseRenderer(RendererType::VTMetal),
          m_HwAccel(hwAccel),
          m_Window(nullptr),
          m_HwContext(nullptr),
          m_MetalLayer(nullptr),
          m_MetalDisplayLink(nullptr),
          m_LatestUnrenderedFrame(nullptr),
          m_FrameLock(SDL_CreateMutex()),
          m_FrameReady(SDL_CreateCond()),
          m_TextureCache(nullptr),
          m_CscParamsBuffer(nullptr),
          m_VideoVertexBuffer(nullptr),
          m_OverlayTextures{},
          m_OverlayLock(0),
          m_VideoPipelineState(nullptr),
          m_OverlayPipelineState(nullptr),
          m_ShaderLibrary(nullptr),
          m_CommandQueue(nullptr),
          m_SwMappingTextures{},
          m_MetalView(nullptr),
          m_LastFrameWidth(-1),
          m_LastFrameHeight(-1),
          m_LastDrawableWidth(-1),
          m_LastDrawableHeight(-1)
    {
    }

    virtual ~VTMetalRenderer() override
    { @autoreleasepool {
        // Stop the display link and free associated state
        stopDisplayLink();
        av_frame_free(&m_LatestUnrenderedFrame);
        SDL_DestroyCond(m_FrameReady);
        SDL_DestroyMutex(m_FrameLock);

        if (m_HwContext != nullptr) {
            av_buffer_unref(&m_HwContext);
        }

        if (m_CscParamsBuffer != nullptr) {
            [m_CscParamsBuffer release];
        }

        if (m_VideoVertexBuffer != nullptr) {
            [m_VideoVertexBuffer release];
        }

        if (m_VideoPipelineState != nullptr) {
            [m_VideoPipelineState release];
        }

        for (int i = 0; i < Overlay::OverlayMax; i++) {
            if (m_OverlayTextures[i] != nullptr) {
                [m_OverlayTextures[i] release];
            }
        }

        for (int i = 0; i < MAX_VIDEO_PLANES; i++) {
            if (m_SwMappingTextures[i] != nullptr) {
                [m_SwMappingTextures[i] release];
            }
        }

        if (m_OverlayPipelineState != nullptr) {
            [m_OverlayPipelineState release];
        }

        if (m_ShaderLibrary != nullptr) {
            [m_ShaderLibrary release];
        }

        if (m_CommandQueue != nullptr) {
            [m_CommandQueue release];
        }

        if (m_TextureCache != nullptr) {
            CFRelease(m_TextureCache);
        }

        if (m_MetalView != nullptr) {
            SDL_Metal_DestroyView(m_MetalView);
        }
    }}

    bool updateVideoRegionSizeForFrame(AVFrame* frame)
    {
        int drawableWidth, drawableHeight;
        SDL_Metal_GetDrawableSize(m_Window, &drawableWidth, &drawableHeight);

        // Check if anything has changed since the last vertex buffer upload
        if (m_VideoVertexBuffer &&
                frame->width == m_LastFrameWidth && frame->height == m_LastFrameHeight &&
                drawableWidth == m_LastDrawableWidth && drawableHeight == m_LastDrawableHeight) {
            // Nothing to do
            return true;
        }

        // Determine the correct scaled size for the video region
        SDL_Rect src, dst;
        src.x = src.y = 0;
        src.w = frame->width;
        src.h = frame->height;
        dst.x = dst.y = 0;
        dst.w = drawableWidth;
        dst.h = drawableHeight;
        StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

        // Convert screen space to normalized device coordinates
        SDL_FRect renderRect;
        StreamUtils::screenSpaceToNormalizedDeviceCoords(&dst, &renderRect, drawableWidth, drawableHeight);

        Vertex verts[] =
        {
            { { renderRect.x, renderRect.y, 0.0f, 1.0f }, { 0.0f, 1.0f } },
            { { renderRect.x, renderRect.y+renderRect.h, 0.0f, 1.0f }, { 0.0f, 0} },
            { { renderRect.x+renderRect.w, renderRect.y, 0.0f, 1.0f }, { 1.0f, 1.0f} },
            { { renderRect.x+renderRect.w, renderRect.y+renderRect.h, 0.0f, 1.0f }, { 1.0f, 0} },
        };

        [m_VideoVertexBuffer release];
        auto bufferOptions = MTLCPUCacheModeWriteCombined | MTLResourceStorageModeManaged;
        m_VideoVertexBuffer = [m_MetalLayer.device newBufferWithBytes:verts length:sizeof(verts) options:bufferOptions];
        if (!m_VideoVertexBuffer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to create video vertex buffer");
            return false;
        }

        m_LastFrameWidth = frame->width;
        m_LastFrameHeight = frame->height;
        m_LastDrawableWidth = drawableWidth;
        m_LastDrawableHeight = drawableHeight;

        return true;
    }

    int getFramePlaneCount(AVFrame* frame)
    {
        if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
            return CVPixelBufferGetPlaneCount((CVPixelBufferRef)frame->data[3]);
        }
        else {
            return av_pix_fmt_count_planes((AVPixelFormat)frame->format);
        }
    }

    int getBitnessScaleFactor(AVFrame* frame)
    {
        if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
            // VideoToolbox frames never require scaling
            return 1;
        }
        else {
            const AVPixFmtDescriptor* formatDesc = av_pix_fmt_desc_get((AVPixelFormat)frame->format);
            if (!formatDesc) {
                // This shouldn't be possible but handle it anyway
                SDL_assert(formatDesc);
                return 1;
            }

            // This assumes plane 0 is exclusively the Y component
            SDL_assert(formatDesc->comp[0].step == 1 || formatDesc->comp[0].step == 2);
            return pow(2, (formatDesc->comp[0].step * 8) - formatDesc->comp[0].depth);
        }
    }

    bool updateColorSpaceForFrame(AVFrame* frame)
    {
        if (!hasFrameFormatChanged(frame) && !m_HdrMetadataChanged) {
            return true;
        }

        int colorspace = getFrameColorspace(frame);
        CGColorSpaceRef newColorSpace;
        ParamBuffer paramBuffer;

        // Stop the display link before changing the Metal layer
        stopDisplayLink();

        switch (colorspace) {
        case COLORSPACE_REC_709:
            m_MetalLayer.colorspace = newColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceITUR_709);
            m_MetalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            break;
        case COLORSPACE_REC_2020:
            m_MetalLayer.pixelFormat = MTLPixelFormatBGR10A2Unorm;
            if (frame->color_trc == AVCOL_TRC_SMPTE2084) {
                // https://developer.apple.com/documentation/metal/hdr_content/using_color_spaces_to_display_hdr_content
                m_MetalLayer.colorspace = newColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceITUR_2100_PQ);
            }
            else {
                m_MetalLayer.colorspace = newColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceITUR_2020);
            }
            break;
        default:
        case COLORSPACE_REC_601:
            m_MetalLayer.colorspace = newColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            m_MetalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            break;
        }

        std::array<float, 9> cscMatrix;
        std::array<float, 3> yuvOffsets;
        std::array<float, 2> chromaOffset;
        getFramePremultipliedCscConstants(frame, cscMatrix, yuvOffsets);
        getFrameChromaCositingOffsets(frame, chromaOffset);

        // Copy the row-major CSC matrix into column-major for Metal
        for (int i = 0; i < 3; i++) {
            paramBuffer.cscParams.matrix[i] = simd_make_float3(cscMatrix[0 + i],
                                                               cscMatrix[3 + i],
                                                               cscMatrix[6 + i]);
        }

        paramBuffer.cscParams.offsets = simd_make_float3(yuvOffsets[0],
                                                         yuvOffsets[1],
                                                         yuvOffsets[2]);
        paramBuffer.chromaOffset = simd_make_float2(chromaOffset[0],
                                                    chromaOffset[1]);

        // Set the EDR metadata for HDR10 to enable OS tonemapping
        if (frame->color_trc == AVCOL_TRC_SMPTE2084 && m_MasteringDisplayColorVolume != nullptr) {
            m_MetalLayer.EDRMetadata = [CAEDRMetadata HDR10MetadataWithDisplayInfo:(__bridge NSData*)m_MasteringDisplayColorVolume
                                                                       contentInfo:(__bridge NSData*)m_ContentLightLevelInfo
                                                                opticalOutputScale:203.0];
        }
        else {
            m_MetalLayer.EDRMetadata = nullptr;
        }

        paramBuffer.bitnessScaleFactor = getBitnessScaleFactor(frame);

        // The CAMetalLayer retains the CGColorSpace
        CGColorSpaceRelease(newColorSpace);

        // Create the new colorspace parameter buffer for our fragment shader
        [m_CscParamsBuffer release];
        auto bufferOptions = MTLCPUCacheModeWriteCombined | MTLResourceStorageModeManaged;
        m_CscParamsBuffer = [m_MetalLayer.device newBufferWithBytes:(void*)&paramBuffer length:sizeof(paramBuffer) options:bufferOptions];
        if (!m_CscParamsBuffer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to create CSC parameters buffer");
            return false;
        }

        int planes = getFramePlaneCount(frame);
        SDL_assert(planes == 2 || planes == 3);

        MTLRenderPipelineDescriptor *pipelineDesc = [[MTLRenderPipelineDescriptor new] autorelease];
        pipelineDesc.vertexFunction = [[m_ShaderLibrary newFunctionWithName:@"vs_draw"] autorelease];
        pipelineDesc.fragmentFunction = [[m_ShaderLibrary newFunctionWithName:planes == 2 ? @"ps_draw_biplanar" : @"ps_draw_triplanar"] autorelease];
        pipelineDesc.colorAttachments[0].pixelFormat = m_MetalLayer.pixelFormat;
        [m_VideoPipelineState release];
        m_VideoPipelineState = [m_MetalLayer.device newRenderPipelineStateWithDescriptor:pipelineDesc error:nullptr];
        if (!m_VideoPipelineState) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to create video pipeline state");
            return false;
        }

        pipelineDesc = [[MTLRenderPipelineDescriptor new] autorelease];
        pipelineDesc.vertexFunction = [[m_ShaderLibrary newFunctionWithName:@"vs_draw"] autorelease];
        pipelineDesc.fragmentFunction = [[m_ShaderLibrary newFunctionWithName:@"ps_draw_rgb"] autorelease];
        pipelineDesc.colorAttachments[0].pixelFormat = m_MetalLayer.pixelFormat;
        pipelineDesc.colorAttachments[0].blendingEnabled = YES;
        pipelineDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        pipelineDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        pipelineDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        pipelineDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
        pipelineDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pipelineDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        [m_OverlayPipelineState release];
        m_OverlayPipelineState = [m_MetalLayer.device newRenderPipelineStateWithDescriptor:pipelineDesc error:nullptr];
        if (!m_VideoPipelineState) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to create overlay pipeline state");
            return false;
        }

        m_HdrMetadataChanged = false;
        return true;
    }

    id<MTLTexture> mapPlaneForSoftwareFrame(AVFrame* frame, int planeIndex)
    {
        const AVPixFmtDescriptor* formatDesc = av_pix_fmt_desc_get((AVPixelFormat)frame->format);
        if (!formatDesc) {
            // This shouldn't be possible but handle it anyway
            SDL_assert(formatDesc);
            return nil;
        }

        SDL_assert(planeIndex < MAX_VIDEO_PLANES);

        NSUInteger planeWidth = planeIndex ? AV_CEIL_RSHIFT(frame->width, formatDesc->log2_chroma_w) : frame->width;
        NSUInteger planeHeight = planeIndex ? AV_CEIL_RSHIFT(frame->height, formatDesc->log2_chroma_h) : frame->height;

        // Recreate the texture if the plane size changes
        if (m_SwMappingTextures[planeIndex] && (m_SwMappingTextures[planeIndex].width != planeWidth ||
                                                m_SwMappingTextures[planeIndex].height != planeHeight)) {
            [m_SwMappingTextures[planeIndex] release];
            m_SwMappingTextures[planeIndex] = nil;
        }

        if (!m_SwMappingTextures[planeIndex]) {
            MTLPixelFormat metalFormat;

            switch (formatDesc->comp[planeIndex].step) {
            case 1:
                metalFormat = MTLPixelFormatR8Unorm;
                break;
            case 2:
                metalFormat = MTLPixelFormatR16Unorm;
                break;
            default:
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Unhandled plane step: %d (plane: %d)",
                             formatDesc->comp[planeIndex].step,
                             planeIndex);
                SDL_assert(false);
                return nil;
            }

            auto texDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:metalFormat
                                                                              width:planeWidth
                                                                             height:planeHeight
                                                                          mipmapped:NO];
            texDesc.cpuCacheMode = MTLCPUCacheModeWriteCombined;
            texDesc.storageMode = MTLStorageModeManaged;
            texDesc.usage = MTLTextureUsageShaderRead;

            m_SwMappingTextures[planeIndex] = [m_MetalLayer.device newTextureWithDescriptor:texDesc];
            if (!m_SwMappingTextures[planeIndex]) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Failed to allocate software frame texture");
                return nil;
            }
        }

        [m_SwMappingTextures[planeIndex] replaceRegion:MTLRegionMake2D(0, 0, planeWidth, planeHeight)
                                           mipmapLevel:0
                                             withBytes:frame->data[planeIndex]
                                           bytesPerRow:frame->linesize[planeIndex]];

        return m_SwMappingTextures[planeIndex];
    }

    // Caller frees frame after we return
    virtual void renderFrameIntoDrawable(AVFrame* frame, id<CAMetalDrawable> drawable)
    { @autoreleasepool {
        std::array<CVMetalTextureRef, MAX_VIDEO_PLANES> cvMetalTextures;
        size_t planes = getFramePlaneCount(frame);
        SDL_assert(planes <= MAX_VIDEO_PLANES);

        if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
            CVPixelBufferRef pixBuf = reinterpret_cast<CVPixelBufferRef>(frame->data[3]);

            // Create Metal textures for the planes of the CVPixelBuffer
            for (size_t i = 0; i < planes; i++) {
                MTLPixelFormat fmt;

                switch (CVPixelBufferGetPixelFormatType(pixBuf)) {
                case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
                case kCVPixelFormatType_444YpCbCr8BiPlanarVideoRange:
                case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
                case kCVPixelFormatType_444YpCbCr8BiPlanarFullRange:
                    fmt = (i == 0) ? MTLPixelFormatR8Unorm : MTLPixelFormatRG8Unorm;
                    break;

                case kCVPixelFormatType_420YpCbCr10BiPlanarFullRange:
                case kCVPixelFormatType_444YpCbCr10BiPlanarFullRange:
                case kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange:
                case kCVPixelFormatType_444YpCbCr10BiPlanarVideoRange:
                    fmt = (i == 0) ? MTLPixelFormatR16Unorm : MTLPixelFormatRG16Unorm;
                    break;

                default:
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "Unknown pixel format: %x",
                                 CVPixelBufferGetPixelFormatType(pixBuf));
                    return;
                }

                CVReturn err = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, m_TextureCache, pixBuf, nullptr, fmt,
                                                                         CVPixelBufferGetWidthOfPlane(pixBuf, i),
                                                                         CVPixelBufferGetHeightOfPlane(pixBuf, i),
                                                                         i,
                                                                         &cvMetalTextures[i]);
                if (err != kCVReturnSuccess) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "CVMetalTextureCacheCreateTextureFromImage() failed: %d",
                                 err);
                    return;
                }
            }
        }

        // Prepare a render pass to render into the next drawable
        auto renderPassDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        renderPassDescriptor.colorAttachments[0].texture = drawable.texture;
        renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        renderPassDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
        renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        auto commandBuffer = [m_CommandQueue commandBuffer];
        auto renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];

        // Bind textures and buffers then draw the video region
        [renderEncoder setRenderPipelineState:m_VideoPipelineState];
        if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
            for (size_t i = 0; i < planes; i++) {
                [renderEncoder setFragmentTexture:CVMetalTextureGetTexture(cvMetalTextures[i]) atIndex:i];
            }
            [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
                // Free textures after completion of rendering per CVMetalTextureCache requirements
                for (size_t i = 0; i < planes; i++) {
                    CFRelease(cvMetalTextures[i]);
                }
            }];
        }
        else {
            for (size_t i = 0; i < planes; i++) {
                [renderEncoder setFragmentTexture:mapPlaneForSoftwareFrame(frame, i) atIndex:i];
            }
        }
        [renderEncoder setFragmentBuffer:m_CscParamsBuffer offset:0 atIndex:0];
        [renderEncoder setVertexBuffer:m_VideoVertexBuffer offset:0 atIndex:0];
        [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];

        // Now draw any overlays that are enabled
        for (int i = 0; i < Overlay::OverlayMax; i++) {
            id<MTLTexture> overlayTexture = nullptr;

            // Try to acquire a reference on the overlay texture
            SDL_AtomicLock(&m_OverlayLock);
            overlayTexture = [m_OverlayTextures[i] retain];
            SDL_AtomicUnlock(&m_OverlayLock);

            if (overlayTexture) {
                SDL_FRect renderRect = {};
                if (i == Overlay::OverlayStatusUpdate) {
                    // Bottom Left
                    renderRect.x = 0;
                    renderRect.y = 0;
                }
                else if (i == Overlay::OverlayDebug) {
                    // Top left
                    renderRect.x = 0;
                    renderRect.y = m_LastDrawableHeight - overlayTexture.height;
                }

                renderRect.w = overlayTexture.width;
                renderRect.h = overlayTexture.height;

                // Convert screen space to normalized device coordinates
                StreamUtils::screenSpaceToNormalizedDeviceCoords(&renderRect, m_LastDrawableWidth, m_LastDrawableHeight);

                Vertex verts[] =
                {
                    { { renderRect.x, renderRect.y, 0.0f, 1.0f }, { 0.0f, 1.0f } },
                    { { renderRect.x, renderRect.y+renderRect.h, 0.0f, 1.0f }, { 0.0f, 0} },
                    { { renderRect.x+renderRect.w, renderRect.y, 0.0f, 1.0f }, { 1.0f, 1.0f} },
                    { { renderRect.x+renderRect.w, renderRect.y+renderRect.h, 0.0f, 1.0f }, { 1.0f, 0} },
                };

                [renderEncoder setRenderPipelineState:m_OverlayPipelineState];
                [renderEncoder setFragmentTexture:overlayTexture atIndex:0];
                [renderEncoder setVertexBytes:verts length:sizeof(verts) atIndex:0];
                [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:SDL_arraysize(verts)];

                [overlayTexture release];
            }
        }

        [renderEncoder endEncoding];

        // Flip to the newly rendered buffer
        [commandBuffer presentDrawable:drawable];
        [commandBuffer commit];

        // Wait for the command buffer to complete and free our CVMetalTextureCache references
        [commandBuffer waitUntilCompleted];
    }}

    // Caller frees frame after we return
    virtual void renderFrame(AVFrame* frame) override
    { @autoreleasepool {
        // Handle changes to the frame's colorspace from last time we rendered
        if (!updateColorSpaceForFrame(frame)) {
            // Trigger the main thread to recreate the decoder
            SDL_Event event;
            event.type = SDL_RENDER_DEVICE_RESET;
            SDL_PushEvent(&event);
            return;
        }

        // Handle changes to the video size or drawable size
        if (!updateVideoRegionSizeForFrame(frame)) {
            // Trigger the main thread to recreate the decoder
            SDL_Event event;
            event.type = SDL_RENDER_DEVICE_RESET;
            SDL_PushEvent(&event);
            return;
        }

        // Start the display link if necessary
        startDisplayLink();

        if (hasDisplayLink()) {
            // Move the buffers into a new AVFrame
            AVFrame* newFrame = av_frame_alloc();
            av_frame_move_ref(newFrame, frame);

            // Replace any existing unrendered frame with this new one
            // and signal the CAMetalDisplayLink callback
            AVFrame* oldFrame = nullptr;
            SDL_LockMutex(m_FrameLock);
            if (m_LatestUnrenderedFrame != nullptr) {
                oldFrame = m_LatestUnrenderedFrame;
            }
            m_LatestUnrenderedFrame = newFrame;
            SDL_UnlockMutex(m_FrameLock);
            SDL_CondSignal(m_FrameReady);

            av_frame_free(&oldFrame);
        }
        else {
            // Render to the next drawable right now when CAMetalDisplayLink is not in use
            id<CAMetalDrawable> drawable = [m_MetalLayer nextDrawable];
            if (drawable == nullptr) {
                return;
            }

            renderFrameIntoDrawable(frame, drawable);
        }
    }}

    id<MTLDevice> getMetalDevice() {
        if (qgetenv("VT_FORCE_METAL") == "0") {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Avoiding Metal renderer due to VT_FORCE_METAL=0 override.");
            return nullptr;
        }

        NSArray<id<MTLDevice>> *devices = [MTLCopyAllDevices() autorelease];
        if (devices.count == 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "No Metal device found!");
            return nullptr;
        }

        for (id<MTLDevice> device in devices) {
            if (device.isLowPower || device.hasUnifiedMemory) {
                return device;
            }
        }

        if (!m_HwAccel) {
            // Metal software decoding is always available
            return [MTLCreateSystemDefaultDevice() autorelease];
        }
        else if (qgetenv("VT_FORCE_METAL") == "1") {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Using Metal renderer due to VT_FORCE_METAL=1 override.");
            return [MTLCreateSystemDefaultDevice() autorelease];
        }
        else {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Avoiding Metal renderer due to use of dGPU/eGPU. Use VT_FORCE_METAL=1 to override.");
        }

        return nullptr;
    }

    virtual bool initialize(PDECODER_PARAMETERS params) override
    { @autoreleasepool {
        int err;

        m_Window = params->window;
        m_FrameRateRange = CAFrameRateRangeMake(params->frameRate, params->frameRate, params->frameRate);

        id<MTLDevice> device = getMetalDevice();
        if (!device) {
            m_InitFailureReason = InitFailureReason::NoSoftwareSupport;
            return false;
        }

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Selected Metal device: %s",
                    device.name.UTF8String);

        if (m_HwAccel && !checkDecoderCapabilities(device, params)) {
            return false;
        }

        err = av_hwdevice_ctx_create(&m_HwContext,
                                     AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
                                     nullptr,
                                     nullptr,
                                     0);
        if (err < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "av_hwdevice_ctx_create() failed for VT decoder: %d",
                        err);
            m_InitFailureReason = InitFailureReason::NoSoftwareSupport;
            return false;
        }

        m_MetalView = SDL_Metal_CreateView(m_Window);
        if (!m_MetalView) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL_Metal_CreateView() failed: %s",
                         SDL_GetError());
            return false;
        }

        m_MetalLayer = (CAMetalLayer*)SDL_Metal_GetLayer(m_MetalView);

        // Choose a device
        m_MetalLayer.device = device;

        // Allow EDR content if we're streaming in a 10-bit format
        m_MetalLayer.wantsExtendedDynamicRangeContent = !!(params->videoFormat & VIDEO_FORMAT_MASK_10BIT);

        // Allow tearing if V-Sync is off (also requires direct display path)
        m_MetalLayer.displaySyncEnabled = params->enableVsync;

        // Create the Metal texture cache for our CVPixelBuffers
        CFStringRef keys[1] = { kCVMetalTextureUsage };
        NSUInteger values[1] = { MTLTextureUsageShaderRead };
        auto cacheAttributes = CFDictionaryCreate(kCFAllocatorDefault, (const void**)keys, (const void**)values, 1, nullptr, nullptr);
        err = CVMetalTextureCacheCreate(kCFAllocatorDefault, cacheAttributes, m_MetalLayer.device, nullptr, &m_TextureCache);
        CFRelease(cacheAttributes);

        if (err != kCVReturnSuccess) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "CVMetalTextureCacheCreate() failed: %d",
                         err);
            return false;
        }

        // Compile our shaders
        QString shaderSource = QString::fromUtf8(Path::readDataFile("vt_renderer.metal"));
        m_ShaderLibrary = [m_MetalLayer.device newLibraryWithSource:shaderSource.toNSString() options:nullptr error:nullptr];
        if (!m_ShaderLibrary) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to compile shaders");
            return false;
        }

        // Create a command queue for submission
        m_CommandQueue = [m_MetalLayer.device newCommandQueue];
        return true;
    }}

    virtual void notifyOverlayUpdated(Overlay::OverlayType type) override
    { @autoreleasepool {
        SDL_Surface* newSurface = Session::get()->getOverlayManager().getUpdatedOverlaySurface(type);
        bool overlayEnabled = Session::get()->getOverlayManager().isOverlayEnabled(type);
        if (newSurface == nullptr && overlayEnabled) {
            // The overlay is enabled and there is no new surface. Leave the old texture alone.
            return;
        }

        SDL_AtomicLock(&m_OverlayLock);
        auto oldTexture = m_OverlayTextures[type];
        m_OverlayTextures[type] = nullptr;
        SDL_AtomicUnlock(&m_OverlayLock);

        [oldTexture release];

        // If the overlay is disabled, we're done
        if (!overlayEnabled) {
            SDL_FreeSurface(newSurface);
            return;
        }

        // Create a texture to hold our pixel data
        SDL_assert(!SDL_MUSTLOCK(newSurface));
        SDL_assert(newSurface->format->format == SDL_PIXELFORMAT_ARGB8888);
        auto texDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                          width:newSurface->w
                                                                         height:newSurface->h
                                                                      mipmapped:NO];
        texDesc.cpuCacheMode = MTLCPUCacheModeWriteCombined;
        texDesc.storageMode = MTLStorageModeManaged;
        texDesc.usage = MTLTextureUsageShaderRead;
        auto newTexture = [m_MetalLayer.device newTextureWithDescriptor:texDesc];

        // Load the pixel data into the new texture
        [newTexture replaceRegion:MTLRegionMake2D(0, 0, newSurface->w, newSurface->h)
                      mipmapLevel:0
                        withBytes:newSurface->pixels
                      bytesPerRow:newSurface->pitch];

        // The surface is no longer required
        SDL_FreeSurface(newSurface);
        newSurface = nullptr;

        SDL_AtomicLock(&m_OverlayLock);
        m_OverlayTextures[type] = newTexture;
        SDL_AtomicUnlock(&m_OverlayLock);
    }}

    virtual bool prepareDecoderContext(AVCodecContext* context, AVDictionary**) override
    {
        if (m_HwAccel) {
            context->hw_device_ctx = av_buffer_ref(m_HwContext);
        }

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Using Metal renderer with %s decoding",
                    m_HwAccel ? "hardware" : "software");

        return true;
    }

    void startDisplayLink()
    {
        if (@available(macOS 14, *)) {
            if (m_MetalDisplayLink != nullptr || !m_MetalLayer.displaySyncEnabled || !isAppleSilicon()) {
                return;
            }

            m_MetalDisplayLink = [[CAMetalDisplayLink alloc] initWithMetalLayer:m_MetalLayer];
            m_MetalDisplayLink.preferredFrameLatency = 1.0f;
            m_MetalDisplayLink.preferredFrameRateRange = m_FrameRateRange;
            m_MetalDisplayLink.delegate = [[DisplayLinkDelegate alloc] initWithRenderer:this];
            [m_MetalDisplayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
        }
    }

    void stopDisplayLink()
    {
        if (@available(macOS 14, *)) {
            if (m_MetalDisplayLink == nullptr) {
                return;
            }

            [m_MetalDisplayLink invalidate];
            m_MetalDisplayLink = nullptr;
        }
    }

    bool hasDisplayLink()
    {
        if (@available(macOS 14, *)) {
            if (m_MetalDisplayLink != nullptr) {
                return true;
            }
        }

        return false;
    }

    virtual bool needsTestFrame() override
    {
        // We used to trust VT to tell us whether decode will work, but
        // there are cases where it can lie because the hardware technically
        // can decode the format but VT is unserviceable for some other reason.
        // Decoding the test frame will tell us for sure whether it will work.
        return true;
    }

    int getDecoderColorspace() override
    {
        // macOS seems to handle Rec 601 best
        return COLORSPACE_REC_601;
    }

    int getDecoderCapabilities() override
    {
        return CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC |
               CAPABILITY_REFERENCE_FRAME_INVALIDATION_AV1;
    }

    int getRendererAttributes() override
    {
        // Metal supports HDR output
        return RENDERER_ATTRIBUTE_HDR_SUPPORT;
    }

    bool isPixelFormatSupported(int videoFormat, AVPixelFormat pixelFormat) override
    {
        if (m_HwAccel) {
            return pixelFormat == AV_PIX_FMT_VIDEOTOOLBOX;
        }
        else {
            if (pixelFormat == AV_PIX_FMT_VIDEOTOOLBOX) {
                // VideoToolbox frames are always supported
                return true;
            }
            else {
                // Otherwise it's supported if we can map it
                const int expectedPixelDepth = (videoFormat & VIDEO_FORMAT_MASK_10BIT) ? 10 : 8;
                const int expectedLog2ChromaW = (videoFormat & VIDEO_FORMAT_MASK_YUV444) ? 0 : 1;
                const int expectedLog2ChromaH = (videoFormat & VIDEO_FORMAT_MASK_YUV444) ? 0 : 1;

                const AVPixFmtDescriptor* formatDesc = av_pix_fmt_desc_get(pixelFormat);
                if (!formatDesc) {
                    // This shouldn't be possible but handle it anyway
                    SDL_assert(formatDesc);
                    return false;
                }

                int planes = av_pix_fmt_count_planes(pixelFormat);
                return (planes == 2 || planes == 3) &&
                       formatDesc->comp[0].depth == expectedPixelDepth &&
                       formatDesc->log2_chroma_w == expectedLog2ChromaW &&
                       formatDesc->log2_chroma_h == expectedLog2ChromaH;
            }
        }
    }

    bool notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO info) override
    {
        auto unhandledStateFlags = info->stateChangeFlags;

        // We can always handle size changes
        unhandledStateFlags &= ~WINDOW_STATE_CHANGE_SIZE;

        // We can handle monitor changes
        unhandledStateFlags &= ~WINDOW_STATE_CHANGE_DISPLAY;

        // If nothing is left, we handled everything
        return unhandledStateFlags == 0;
    }

    void renderLatestFrameOnDrawable(id<CAMetalDrawable> drawable, CFTimeInterval targetTimestamp)
    {
        AVFrame* frame = nullptr;

        // Determine how long we can wait depending on how long our CAMetalDisplayLink
        // says we have until the next frame needs to be rendered. We will wait up to
        // half the per-frame interval for a new frame to become available.
        int waitTimeMs = ((targetTimestamp - CACurrentMediaTime()) * 1000) / 2;
        if (waitTimeMs < 0) {
            return;
        }

        // Wait for a new frame to be ready
        SDL_LockMutex(m_FrameLock);
        if (m_LatestUnrenderedFrame != nullptr || SDL_CondWaitTimeout(m_FrameReady, m_FrameLock, waitTimeMs) == 0) {
            frame = m_LatestUnrenderedFrame;
            m_LatestUnrenderedFrame = nullptr;
        }
        SDL_UnlockMutex(m_FrameLock);

        // Render a frame if we got one in time
        if (frame != nullptr) {
            renderFrameIntoDrawable(frame, drawable);
            av_frame_free(&frame);
        }
    }

private:
    bool m_HwAccel;
    SDL_Window* m_Window;
    AVBufferRef* m_HwContext;
    CAMetalLayer* m_MetalLayer;
    CAMetalDisplayLink* m_MetalDisplayLink API_AVAILABLE(macos(14.0));
    CAFrameRateRange m_FrameRateRange;
    AVFrame* m_LatestUnrenderedFrame;
    SDL_mutex* m_FrameLock;
    SDL_cond* m_FrameReady;
    CVMetalTextureCacheRef m_TextureCache;
    id<MTLBuffer> m_CscParamsBuffer;
    id<MTLBuffer> m_VideoVertexBuffer;
    id<MTLTexture> m_OverlayTextures[Overlay::OverlayMax];
    SDL_SpinLock m_OverlayLock;
    id<MTLRenderPipelineState> m_VideoPipelineState;
    id<MTLRenderPipelineState> m_OverlayPipelineState;
    id<MTLLibrary> m_ShaderLibrary;
    id<MTLCommandQueue> m_CommandQueue;
    id<MTLTexture> m_SwMappingTextures[MAX_VIDEO_PLANES];
    SDL_MetalView m_MetalView;
    int m_LastFrameWidth;
    int m_LastFrameHeight;
    int m_LastDrawableWidth;
    int m_LastDrawableHeight;
};

@implementation DisplayLinkDelegate {
    VTMetalRenderer* _renderer;
}

- (id)initWithRenderer:(VTMetalRenderer *)renderer {
    _renderer = renderer;
    return self;
}

- (void)metalDisplayLink:(CAMetalDisplayLink *)link
             needsUpdate:(CAMetalDisplayLinkUpdate *)update API_AVAILABLE(macos(14.0)) {
    _renderer->renderLatestFrameOnDrawable(update.drawable, update.targetTimestamp);
}

@end

IFFmpegRenderer* VTMetalRendererFactory::createRenderer(bool hwAccel) {
    return new VTMetalRenderer(hwAccel);
}
