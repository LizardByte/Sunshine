#include "cuda.h"

#include <SDL_opengl.h>

CUDARenderer::CUDARenderer()
    : IFFmpegRenderer(RendererType::CUDA),
      m_HwContext(nullptr)
{

}

CUDARenderer::~CUDARenderer()
{
    if (m_HwContext != nullptr) {
        av_buffer_unref(&m_HwContext);
    }
}

bool CUDARenderer::initialize(PDECODER_PARAMETERS)
{
    int err;

    err = av_hwdevice_ctx_create(&m_HwContext, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
    if (err != 0) {
        m_InitFailureReason = InitFailureReason::NoSoftwareSupport;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "av_hwdevice_ctx_create(CUDA) failed: %d",
                     err);
        return false;
    }

    return true;
}

bool CUDARenderer::prepareDecoderContext(AVCodecContext* context, AVDictionary**)
{
    context->hw_device_ctx = av_buffer_ref(m_HwContext);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Using CUDA accelerated decoder");

    return true;
}

void CUDARenderer::renderFrame(AVFrame*)
{
    // We only support indirect rendering
    SDL_assert(false);
}

bool CUDARenderer::needsTestFrame()
{
    return true;
}

bool CUDARenderer::isDirectRenderingSupported()
{
    // We only support rendering via SDL read-back
    return false;
}

int CUDARenderer::getDecoderCapabilities()
{
    return CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC |
           CAPABILITY_REFERENCE_FRAME_INVALIDATION_AV1;
}

CUDAGLInteropHelper::CUDAGLInteropHelper(AVHWDeviceContext* context)
    : m_Funcs(nullptr),
      m_Context((AVCUDADeviceContext*)context->hwctx)
{
    memset(m_Resources, 0, sizeof(m_Resources));

    // One-time init of CUDA library
    cuda_load_functions(&m_Funcs, nullptr);
    if (m_Funcs == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize CUDA library");
        return;
    }
}

CUDAGLInteropHelper::~CUDAGLInteropHelper()
{
    unregisterTextures();

    if (m_Funcs != nullptr) {
        cuda_free_functions(&m_Funcs);
    }
}

bool CUDAGLInteropHelper::registerBoundTextures()
{
    int err;

    if (m_Funcs == nullptr) {
        // Already logged in constructor
        return false;
    }

    // Push FFmpeg's CUDA context to use for our CUDA operations
    err = m_Funcs->cuCtxPushCurrent(m_Context->cuda_ctx);
    if (err != CUDA_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cuCtxPushCurrent() failed: %d", err);
        return false;
    }

    // Register each plane as a separate resource
    for (int i = 0; i < NV12_PLANES; i++) {
        GLint tex;

        // Get the ID of this plane's texture
        glActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);

        // Register it with CUDA
        err = m_Funcs->cuGraphicsGLRegisterImage(&m_Resources[i], tex, GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD);
        if (err != CUDA_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cuGraphicsGLRegisterImage() failed: %d", err);
            m_Resources[i] = 0;
            unregisterTextures();
            goto Exit;
        }
    }

Exit:
    {
        CUcontext dummy;
        m_Funcs->cuCtxPopCurrent(&dummy);
    }
    return err == CUDA_SUCCESS;
}

void CUDAGLInteropHelper::unregisterTextures()
{
    int err;

    if (m_Funcs == nullptr) {
        // Already logged in constructor
        return;
    }

    // Push FFmpeg's CUDA context to use for our CUDA operations
    err = m_Funcs->cuCtxPushCurrent(m_Context->cuda_ctx);
    if (err != CUDA_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cuCtxPushCurrent() failed: %d", err);
        return;
    }

    for (int i = 0; i < NV12_PLANES; i++) {
        if (m_Resources[i] != 0) {
            m_Funcs->cuGraphicsUnregisterResource(m_Resources[i]);
            m_Resources[i] = 0;
        }
    }

    {
        CUcontext dummy;
        m_Funcs->cuCtxPopCurrent(&dummy);
    }
}

bool CUDAGLInteropHelper::copyCudaFrameToTextures(AVFrame* frame)
{
    int err;

    if (m_Funcs == nullptr) {
        // Already logged in constructor
        return false;
    }

    // Push FFmpeg's CUDA context to use for our CUDA operations
    err = m_Funcs->cuCtxPushCurrent(m_Context->cuda_ctx);
    if (err != CUDA_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cuCtxPushCurrent() failed: %d", err);
        return false;
    }

    // Map our resources
    err = m_Funcs->cuGraphicsMapResources(NV12_PLANES, m_Resources, m_Context->stream);
    if (err != CUDA_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cuGraphicsMapResources() failed: %d", err);
        goto PopCtxExit;
    }

    for (int i = 0; i < NV12_PLANES; i++) {
        CUarray cudaArray;

        // Get a pointer to the mapped array for this plane
        err = m_Funcs->cuGraphicsSubResourceGetMappedArray(&cudaArray, m_Resources[i], 0, 0);
        if (err != CUDA_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cuGraphicsSubResourceGetMappedArray() failed: %d", err);
            goto UnmapExit;
        }

        // Do the copy
        CUDA_MEMCPY2D cu2d = {
            .srcMemoryType = CU_MEMORYTYPE_DEVICE,
            .srcDevice = (CUdeviceptr)frame->data[i],
            .srcPitch = (size_t)frame->linesize[i],
            .dstMemoryType = CU_MEMORYTYPE_ARRAY,
            .dstArray = cudaArray,
            .dstPitch = (size_t)frame->width >> i,
            .WidthInBytes = (size_t)frame->width,
            .Height = (size_t)frame->height >> i
        };
        err = m_Funcs->cuMemcpy2D(&cu2d);
        if (err != CUDA_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cuMemcpy2D() failed: %d", err);
            goto UnmapExit;
        }
    }

UnmapExit:
    m_Funcs->cuGraphicsUnmapResources(NV12_PLANES, m_Resources, m_Context->stream);
PopCtxExit:
    {
        CUcontext dummy;
        m_Funcs->cuCtxPopCurrent(&dummy);
    }
    return err == CUDA_SUCCESS;
}
