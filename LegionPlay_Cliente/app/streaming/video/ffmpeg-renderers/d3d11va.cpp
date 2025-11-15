// For D3D11_DECODER_PROFILE values
#include <initguid.h>

#include "d3d11va.h"
#include "dxutil.h"
#include "path.h"

#include "streaming/streamutils.h"
#include "streaming/session.h"

#include <SDL_syswm.h>
#include <VersionHelpers.h>

#include <dwmapi.h>

using Microsoft::WRL::ComPtr;

// Standard DXVA GUIDs for HEVC RExt profiles (redefined for compatibility with pre-24H2 SDKs)
DEFINE_GUID(k_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN_444,   0x4008018f, 0xf537, 0x4b36, 0x98, 0xcf, 0x61, 0xaf, 0x8a, 0x2c, 0x1a, 0x33);
DEFINE_GUID(k_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10_444, 0x0dabeffa, 0x4458, 0x4602, 0xbc, 0x03, 0x07, 0x95, 0x65, 0x9d, 0x61, 0x7c);

typedef struct _VERTEX
{
    float x, y;
    float tu, tv;
} VERTEX, *PVERTEX;

#define CSC_MATRIX_RAW_ELEMENT_COUNT 9
#define CSC_MATRIX_PACKED_ELEMENT_COUNT 12
#define OFFSETS_ELEMENT_COUNT 3

typedef struct _CSC_CONST_BUF
{
    // CscMatrix value from above but packed and scaled
    float cscMatrix[CSC_MATRIX_PACKED_ELEMENT_COUNT];

    // YUV offset values
    float offsets[OFFSETS_ELEMENT_COUNT];

    // Padding float to end 16-byte boundary
    float padding;

    // Chroma offset values
    float chromaOffset[2];

    // Padding to final 16-byte boundary
    float padding2[2];
} CSC_CONST_BUF, *PCSC_CONST_BUF;
static_assert(sizeof(CSC_CONST_BUF) % 16 == 0, "Constant buffer sizes must be a multiple of 16");

static const std::array<const char*, D3D11VARenderer::PixelShaders::_COUNT> k_VideoShaderNames =
{
    "d3d11_yuv420_pixel.fxc",
    "d3d11_ayuv_pixel.fxc",
    "d3d11_y410_pixel.fxc",
};

D3D11VARenderer::D3D11VARenderer(int decoderSelectionPass)
    : IFFmpegRenderer(RendererType::D3D11VA),
      m_DecoderSelectionPass(decoderSelectionPass),
      m_DevicesWithFL11Support(0),
      m_DevicesWithCodecSupport(0),
      m_LastColorTrc(AVCOL_TRC_UNSPECIFIED),
      m_AllowTearing(false),
      m_OverlayLock(0),
      m_HwDeviceContext(nullptr),
      m_HwFramesContext(nullptr)
{
    m_ContextLock = SDL_CreateMutex();

    DwmEnableMMCSS(TRUE);
}

D3D11VARenderer::~D3D11VARenderer()
{
    DwmEnableMMCSS(FALSE);

    SDL_DestroyMutex(m_ContextLock);

    m_VideoVertexBuffer.Reset();
    for (auto& shader : m_VideoPixelShaders) {
        shader.Reset();
    }

    for (auto& textureSrvs : m_VideoTextureResourceViews) {
        for (auto& srv : textureSrvs) {
            srv.Reset();
        }
    }

    m_VideoTexture.Reset();

    for (auto& buffer : m_OverlayVertexBuffers) {
        buffer.Reset();
    }

    for (auto& srv : m_OverlayTextureResourceViews) {
        srv.Reset();
    }

    for (auto& texture : m_OverlayTextures) {
        texture.Reset();
    }

    m_OverlayPixelShader.Reset();

    m_RenderTargetView.Reset();
    m_SwapChain.Reset();

    av_buffer_unref(&m_HwFramesContext);
    av_buffer_unref(&m_HwDeviceContext);

    // Force destruction of the swapchain immediately
    if (m_DeviceContext != nullptr) {
        m_DeviceContext->ClearState();
        m_DeviceContext->Flush();
    }

    m_Device.Reset();
    m_DeviceContext.Reset();
    m_Factory.Reset();
}

bool D3D11VARenderer::createDeviceByAdapterIndex(int adapterIndex, bool* adapterNotFound)
{
    const D3D_FEATURE_LEVEL supportedFeatureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    bool success = false;
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 adapterDesc;
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr;

    SDL_assert(!m_Device);
    SDL_assert(!m_DeviceContext);

    hr = m_Factory->EnumAdapters1(adapterIndex, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND) {
        // Expected at the end of enumeration
        goto Exit;
    }
    else if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGIFactory::EnumAdapters1() failed: %x",
                     hr);
        goto Exit;
    }

    hr = adapter->GetDesc1(&adapterDesc);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGIAdapter::GetDesc() failed: %x",
                     hr);
        goto Exit;
    }

    if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
        // Skip the WARP device. We know it will fail.
        goto Exit;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Detected GPU %d: %S (%x:%x)",
                adapterIndex,
                adapterDesc.Description,
                adapterDesc.VendorId,
                adapterDesc.DeviceId);

    hr = D3D11CreateDevice(adapter.Get(),
                           D3D_DRIVER_TYPE_UNKNOWN,
                           nullptr,
                           D3D11_CREATE_DEVICE_VIDEO_SUPPORT
                       #ifdef QT_DEBUG
                               | D3D11_CREATE_DEVICE_DEBUG
                       #endif
                           ,
                           supportedFeatureLevels,
                           ARRAYSIZE(supportedFeatureLevels),
                           D3D11_SDK_VERSION,
                           &m_Device,
                           &featureLevel,
                           &m_DeviceContext);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3D11CreateDevice() failed: %x",
                     hr);
        goto Exit;
    }
    else if (adapterDesc.VendorId == 0x8086 && featureLevel <= D3D_FEATURE_LEVEL_11_0 && !qEnvironmentVariableIntValue("D3D11VA_ENABLED")) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Avoiding D3D11VA on old pre-FL11.1 Intel GPU. Set D3D11VA_ENABLED=1 to override.");
        m_DeviceContext.Reset();
        m_Device.Reset();
        goto Exit;
    }
    else if (featureLevel >= D3D_FEATURE_LEVEL_11_0) {
        // Remember that we found a non-software D3D11 devices with support for
        // feature level 11.0 or later (Fermi, Terascale 2, or Ivy Bridge and later)
        m_DevicesWithFL11Support++;
    }

    bool ok;
    m_BindDecoderOutputTextures = !!qEnvironmentVariableIntValue("D3D11VA_FORCE_BIND", &ok);
    if (!ok) {
        // Skip copying to our own internal texture on Intel GPUs due to
        // significant performance impact of the extra copy. See:
        // https://github.com/moonlight-stream/moonlight-qt/issues/1304
        m_BindDecoderOutputTextures = adapterDesc.VendorId == 0x8086;
    }
    else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Using D3D11VA_FORCE_BIND to override default bind/copy logic");
    }

    m_UseFenceHack = !!qEnvironmentVariableIntValue("D3D11VA_FORCE_FENCE", &ok);
    if (!ok) {
        // Old Intel GPUs (HD 4000) require a fence to properly synchronize
        // the video engine with the 3D engine for texture sampling.
        m_UseFenceHack = adapterDesc.VendorId == 0x8086 && featureLevel < D3D_FEATURE_LEVEL_11_1;
    }
    else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Using D3D11VA_FORCE_FENCE to override default fence workaround logic");
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Decoder texture access: %s (fence: %s)",
                m_BindDecoderOutputTextures ? "bind" : "copy",
                (m_BindDecoderOutputTextures && m_UseFenceHack) ? "yes" : "no");

    // Check which fence types are supported by this GPU
    {
        m_FenceType = SupportedFenceType::None;

        ComPtr<IDXGIAdapter4> adapter4;
        if (SUCCEEDED(adapter.As(&adapter4))) {
            DXGI_ADAPTER_DESC3 desc3;
            if (SUCCEEDED(adapter4->GetDesc3(&desc3))) {
                if (desc3.Flags & DXGI_ADAPTER_FLAG3_SUPPORT_MONITORED_FENCES) {
                    // Monitored fences must be used when they are supported
                    m_FenceType = SupportedFenceType::Monitored;
                }
                else if (desc3.Flags & DXGI_ADAPTER_FLAG3_SUPPORT_NON_MONITORED_FENCES) {
                    // Non-monitored fences must only be used when monitored fences are unsupported
                    m_FenceType = SupportedFenceType::NonMonitored;
                }
            }
        }
    }

    if (!checkDecoderSupport(adapter.Get())) {
        m_DeviceContext.Reset();
        m_Device.Reset();
        goto Exit;
    }
    else {
        // Remember that we found a device with support for decoding this codec
        m_DevicesWithCodecSupport++;
    }

    success = true;

Exit:
    if (adapterNotFound != nullptr) {
        *adapterNotFound = !adapter;
    }
    return success;
}

bool D3D11VARenderer::initialize(PDECODER_PARAMETERS params)
{
    int adapterIndex, outputIndex;
    HRESULT hr;

    m_DecoderParams = *params;

    if (qgetenv("D3D11VA_ENABLED") == "0") {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "D3D11VA is disabled by environment variable");
        return false;
    }
    else if (!IsWindows10OrGreater()) {
        // Use DXVA2 on anything older than Win10, so we don't have to handle a bunch
        // of legacy Win7/Win8 codepaths in here.
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3D11VA renderer is only supported on Windows 10 or later.");
        return false;
    }

    if (!SDL_DXGIGetOutputInfo(SDL_GetWindowDisplayIndex(params->window),
                               &adapterIndex, &outputIndex)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_DXGIGetOutputInfo() failed: %s",
                     SDL_GetError());
        return false;
    }

    hr = CreateDXGIFactory(__uuidof(IDXGIFactory5), (void**)&m_Factory);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "CreateDXGIFactory() failed: %x",
                     hr);
        return false;
    }

    // First try the adapter corresponding to the display where our window resides.
    // This will let us avoid a copy if the display GPU has the required decoder.
    if (!createDeviceByAdapterIndex(adapterIndex)) {
        // If that didn't work, we'll try all GPUs in order until we find one
        // or run out of GPUs (DXGI_ERROR_NOT_FOUND from EnumAdapters())
        bool adapterNotFound = false;
        for (int i = 0; !adapterNotFound; i++) {
            if (i == adapterIndex) {
                // Don't try the same GPU again
                continue;
            }

            if (createDeviceByAdapterIndex(i, &adapterNotFound)) {
                // This GPU worked! Continue initialization.
                break;
            }
        }

        if (adapterNotFound) {
            SDL_assert(!m_Device);
            SDL_assert(!m_DeviceContext);
            return false;
        }
    }

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags = 0;

    // 3 front buffers (default GetMaximumFrameLatency() count)
    // + 1 back buffer
    // + 1 extra for DWM to hold on to for DirectFlip
    //
    // Even though we allocate 3 front buffers for pre-rendered frames,
    // they won't actually increase presentation latency because we
    // always use SyncInterval 0 which replaces the last one.
    //
    // IDXGIDevice1 has a SetMaximumFrameLatency() function, but counter-
    // intuitively we must avoid it to reduce latency. If we set our max
    // frame latency to 1 on thedevice, our SyncInterval 0 Present() calls
    // will block on DWM (acting like SyncInterval 1) rather than doing
    // the non-blocking present we expect.
    //
    // NB: 3 total buffers seems sufficient on NVIDIA hardware but
    // causes performance issues (buffer starvation) on AMD GPUs.
    swapChainDesc.BufferCount = 3 + 1 + 1;

    // Use the current window size as the swapchain size
    SDL_GetWindowSize(params->window, (int*)&swapChainDesc.Width, (int*)&swapChainDesc.Height);

    m_DisplayWidth = swapChainDesc.Width;
    m_DisplayHeight = swapChainDesc.Height;

    if (params->videoFormat & VIDEO_FORMAT_MASK_10BIT) {
        swapChainDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    }
    else {
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    // Use DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING with flip mode for non-vsync case, if possible.
    // NOTE: This is only possible in windowed or borderless windowed mode.
    if (!params->enableVsync) {
        BOOL allowTearing = FALSE;
        hr = m_Factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                            &allowTearing,
                                            sizeof(allowTearing));
        if (SUCCEEDED(hr)) {
            if (allowTearing) {
                // Use flip discard with allow tearing mode if possible.
                swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
                m_AllowTearing = true;
            }
            else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "OS/GPU doesn't support DXGI_FEATURE_PRESENT_ALLOW_TEARING");
            }
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "IDXGIFactory::CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING) failed: %x",
                         hr);
            // Non-fatal
        }

        // DXVA2 may let us take over for FSE V-sync off cases. However, if we don't have DXGI_FEATURE_PRESENT_ALLOW_TEARING
        // then we should not attempt to do this unless there's no other option (HDR, DXVA2 failed in pass 1, etc).
        if (!m_AllowTearing && m_DecoderSelectionPass == 0 && !(params->videoFormat & VIDEO_FORMAT_MASK_10BIT) &&
                (SDL_GetWindowFlags(params->window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Defaulting to DXVA2 for FSE without DXGI_FEATURE_PRESENT_ALLOW_TEARING support");
            return false;
        }
    }

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    SDL_GetWindowWMInfo(params->window, &info);
    SDL_assert(info.subsystem == SDL_SYSWM_WINDOWS);

    // Always use windowed or borderless windowed mode.. SDL does mode-setting for us in
    // full-screen exclusive mode (SDL_WINDOW_FULLSCREEN), so this actually works out okay.
    ComPtr<IDXGISwapChain1> swapChain;
    hr = m_Factory->CreateSwapChainForHwnd(m_Device.Get(),
                                           info.info.win.window,
                                           &swapChainDesc,
                                           nullptr,
                                           nullptr,
                                           &swapChain);

    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGIFactory::CreateSwapChainForHwnd() failed: %x",
                     hr);
        return false;
    }

    hr = swapChain.As(&m_SwapChain);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGISwapChain::QueryInterface(IDXGISwapChain4) failed: %x",
                     hr);
        return false;
    }

    // Disable Alt+Enter, PrintScreen, and window message snooping. This makes
    // it safe to run the renderer on a separate rendering thread rather than
    // requiring the main (message loop) thread.
    hr = m_Factory->MakeWindowAssociation(info.info.win.window, DXGI_MWA_NO_WINDOW_CHANGES);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGIFactory::MakeWindowAssociation() failed: %x",
                     hr);
        return false;
    }

    // Surfaces must be 16 pixel aligned for H.264 and 128 pixel aligned for everything else
    // https://github.com/FFmpeg/FFmpeg/blob/a234e5cd80224c95a205c1f3e297d8c04a1374c3/libavcodec/dxva2.c#L609-L616
    m_TextureAlignment = (params->videoFormat & VIDEO_FORMAT_MASK_H264) ? 16 : 128;

    if (!setupRenderingResources()) {
        return false;
    }

    {
        m_HwDeviceContext = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        if (!m_HwDeviceContext) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to allocate D3D11VA device context");
            return false;
        }

        AVHWDeviceContext* deviceContext = (AVHWDeviceContext*)m_HwDeviceContext->data;
        AVD3D11VADeviceContext* d3d11vaDeviceContext = (AVD3D11VADeviceContext*)deviceContext->hwctx;

        // FFmpeg will take ownership of these pointers, so we use CopyTo() to bump the ref count
        m_Device.CopyTo(&d3d11vaDeviceContext->device);
        m_DeviceContext.CopyTo(&d3d11vaDeviceContext->device_context);

        // Set lock functions that we will use to synchronize with FFmpeg's usage of our device context
        d3d11vaDeviceContext->lock = lockContext;
        d3d11vaDeviceContext->unlock = unlockContext;
        d3d11vaDeviceContext->lock_ctx = this;

        int err = av_hwdevice_ctx_init(m_HwDeviceContext);
        if (err < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to initialize D3D11VA device context: %d",
                         err);
            return false;
        }
    }

    {
        m_HwFramesContext = av_hwframe_ctx_alloc(m_HwDeviceContext);
        if (!m_HwFramesContext) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to allocate D3D11VA frame context");
            return false;
        }

        AVHWFramesContext* framesContext = (AVHWFramesContext*)m_HwFramesContext->data;

        framesContext->format = AV_PIX_FMT_D3D11;
        if (params->videoFormat & VIDEO_FORMAT_MASK_10BIT) {
            framesContext->sw_format = (params->videoFormat & VIDEO_FORMAT_MASK_YUV444) ?
                                           AV_PIX_FMT_XV30 : AV_PIX_FMT_P010;
        }
        else {
            framesContext->sw_format = (params->videoFormat & VIDEO_FORMAT_MASK_YUV444) ?
                                           AV_PIX_FMT_VUYX : AV_PIX_FMT_NV12;
        }

        framesContext->width = FFALIGN(params->width, m_TextureAlignment);
        framesContext->height = FFALIGN(params->height, m_TextureAlignment);

        // We can have up to 16 reference frames plus a working surface
        framesContext->initial_pool_size = DECODER_BUFFER_POOL_SIZE;

        AVD3D11VAFramesContext* d3d11vaFramesContext = (AVD3D11VAFramesContext*)framesContext->hwctx;

        d3d11vaFramesContext->BindFlags = D3D11_BIND_DECODER;
        if (m_BindDecoderOutputTextures) {
            // We need to override the default D3D11VA bind flags to bind the textures as a shader resources
            d3d11vaFramesContext->BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        }

        int err = av_hwframe_ctx_init(m_HwFramesContext);
        if (err < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to initialize D3D11VA frame context: %d",
                         err);
            return false;
        }

        D3D11_TEXTURE2D_DESC textureDesc;
        d3d11vaFramesContext->texture_infos->texture->GetDesc(&textureDesc);
        m_TextureFormat = textureDesc.Format;
        m_TextureWidth = textureDesc.Width;
        m_TextureHeight = textureDesc.Height;

        if (m_BindDecoderOutputTextures) {
            // Create SRVs for all textures in the decoder pool
            if (!setupTexturePoolViews(d3d11vaFramesContext)) {
                return false;
            }
        }
        else {
            // Create our internal texture to copy and render
            if (!setupVideoTexture()) {
                return false;
            }
        }
    }

    return true;
}

bool D3D11VARenderer::prepareDecoderContext(AVCodecContext* context, AVDictionary**)
{
    context->hw_device_ctx = av_buffer_ref(m_HwDeviceContext);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Using D3D11VA accelerated renderer");

    return true;
}

bool D3D11VARenderer::prepareDecoderContextInGetFormat(AVCodecContext *context, AVPixelFormat)
{
    // hw_frames_ctx must be initialized in ffGetFormat().
    context->hw_frames_ctx = av_buffer_ref(m_HwFramesContext);

    return true;
}

void D3D11VARenderer::renderFrame(AVFrame* frame)
{
    // Acquire the context lock for rendering to prevent concurrent
    // access from inside FFmpeg's decoding code
    lockContext(this);

    // Clear the back buffer
    const float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    m_DeviceContext->ClearRenderTargetView(m_RenderTargetView.Get(), clearColor);

    // Bind the back buffer. This needs to be done each time,
    // because the render target view will be unbound by Present().
    m_DeviceContext->OMSetRenderTargets(1, m_RenderTargetView.GetAddressOf(), nullptr);

    // Render our video frame with the aspect-ratio adjusted viewport
    renderVideo(frame);

    // Render overlays on top of the video stream
    for (int i = 0; i < Overlay::OverlayMax; i++) {
        renderOverlay((Overlay::OverlayType)i);
    }

    UINT flags;

    if (m_AllowTearing) {
        SDL_assert(!m_DecoderParams.enableVsync);

        // If tearing is allowed, use DXGI_PRESENT_ALLOW_TEARING with syncInterval 0.
        // It is not valid to use any other syncInterval values in tearing mode.
        flags = DXGI_PRESENT_ALLOW_TEARING;
    }
    else {
        // Otherwise, we'll submit as fast as possible and DWM will discard excess
        // frames for us. If frame pacing is also enabled or we're in full-screen,
        // our Vsync source will keep us in sync with VBlank.
        flags = 0;
    }

    HRESULT hr;

    if (frame->color_trc != m_LastColorTrc) {
        if (frame->color_trc == AVCOL_TRC_SMPTE2084) {
            // Switch to Rec 2020 PQ (SMPTE ST 2084) colorspace for HDR10 rendering
            hr = m_SwapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
            if (FAILED(hr)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "IDXGISwapChain::SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) failed: %x",
                             hr);
            }
        }
        else {
            // Restore default sRGB colorspace
            hr = m_SwapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
            if (FAILED(hr)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "IDXGISwapChain::SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) failed: %x",
                             hr);
            }
        }

        m_LastColorTrc = frame->color_trc;
    }

    // Present according to the decoder parameters
    hr = m_SwapChain->Present(0, flags);

    // Release the context lock
    unlockContext(this);

    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGISwapChain::Present() failed: %x",
                     hr);

        // The card may have been removed or crashed. Reset the decoder.
        SDL_Event event;
        event.type = SDL_RENDER_TARGETS_RESET;
        SDL_PushEvent(&event);
        return;
    }
}

void D3D11VARenderer::renderOverlay(Overlay::OverlayType type)
{
    if (!Session::get()->getOverlayManager().isOverlayEnabled(type)) {
        return;
    }

    // If the overlay is being updated, just skip rendering it this frame
    if (!SDL_AtomicTryLock(&m_OverlayLock)) {
        return;
    }

    // Reference these objects so they don't immediately go away if the
    // overlay update thread tries to release them.
    ComPtr<ID3D11Texture2D> overlayTexture = m_OverlayTextures[type];
    ComPtr<ID3D11Buffer> overlayVertexBuffer = m_OverlayVertexBuffers[type];
    ComPtr<ID3D11ShaderResourceView> overlayTextureResourceView = m_OverlayTextureResourceViews[type];
    SDL_AtomicUnlock(&m_OverlayLock);

    if (!overlayTexture) {
        return;
    }

    // If there was a texture, there must also be a vertex buffer and SRV
    SDL_assert(overlayVertexBuffer);
    SDL_assert(overlayTextureResourceView);

    // Bind vertex buffer
    UINT stride = sizeof(VERTEX);
    UINT offset = 0;
    m_DeviceContext->IASetVertexBuffers(0, 1, overlayVertexBuffer.GetAddressOf(), &stride, &offset);

    // Bind pixel shader and resources
    m_DeviceContext->PSSetShader(m_OverlayPixelShader.Get(), nullptr, 0);
    m_DeviceContext->PSSetShaderResources(0, 1, overlayTextureResourceView.GetAddressOf());

    // Draw the overlay
    m_DeviceContext->DrawIndexed(6, 0, 0);
}

void D3D11VARenderer::bindColorConversion(AVFrame* frame)
{
    bool yuv444 = (m_DecoderParams.videoFormat & VIDEO_FORMAT_MASK_YUV444);

    if (yuv444) {
        // We'll need to use one of the 4:4:4 shaders for this pixel format
        switch (m_TextureFormat)
        {
        case DXGI_FORMAT_AYUV:
            m_DeviceContext->PSSetShader(m_VideoPixelShaders[PixelShaders::GENERIC_AYUV].Get(), nullptr, 0);
            break;
        case DXGI_FORMAT_Y410:
            m_DeviceContext->PSSetShader(m_VideoPixelShaders[PixelShaders::GENERIC_Y410].Get(), nullptr, 0);
            break;
        default:
            SDL_assert(false);
        }
    }
    else {
        // We'll need to use the generic 4:2:0 shader for this colorspace and color range combo
        m_DeviceContext->PSSetShader(m_VideoPixelShaders[PixelShaders::GENERIC_YUV_420].Get(), nullptr, 0);
    }

    // If nothing has changed since last frame, we're done
    if (!hasFrameFormatChanged(frame)) {
        return;
    }

    D3D11_BUFFER_DESC constDesc = {};
    constDesc.ByteWidth = sizeof(CSC_CONST_BUF);
    constDesc.Usage = D3D11_USAGE_IMMUTABLE;
    constDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constDesc.CPUAccessFlags = 0;
    constDesc.MiscFlags = 0;

    CSC_CONST_BUF constBuf = {};
    std::array<float, 9> cscMatrix;
    std::array<float, 3> yuvOffsets;
    getFramePremultipliedCscConstants(frame, cscMatrix, yuvOffsets);

    std::copy(yuvOffsets.cbegin(), yuvOffsets.cend(), constBuf.offsets);

    // We need to adjust our CSC matrix to be column-major and with float3 vectors
    // padded with a float in between each of them to adhere to HLSL requirements.
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            constBuf.cscMatrix[i * 4 + j] = cscMatrix[j * 3 + i];
        }
    }

    std::array<float, 2> chromaOffset;
    getFrameChromaCositingOffsets(frame, chromaOffset);
    constBuf.chromaOffset[0] = chromaOffset[0] / m_TextureWidth;
    constBuf.chromaOffset[1] = chromaOffset[1] / m_TextureHeight;

    D3D11_SUBRESOURCE_DATA constData = {};
    constData.pSysMem = &constBuf;

    ComPtr<ID3D11Buffer> constantBuffer;
    HRESULT hr = m_Device->CreateBuffer(&constDesc, &constData, &constantBuffer);
    if (SUCCEEDED(hr)) {
        m_DeviceContext->PSSetConstantBuffers(1, 1, constantBuffer.GetAddressOf());
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateBuffer() failed: %x",
                     hr);
        return;
    }
}

void D3D11VARenderer::renderVideo(AVFrame* frame)
{
    // Bind video rendering vertex buffer
    UINT stride = sizeof(VERTEX);
    UINT offset = 0;
    m_DeviceContext->IASetVertexBuffers(0, 1, m_VideoVertexBuffer.GetAddressOf(), &stride, &offset);

    UINT srvIndex;
    if (m_BindDecoderOutputTextures) {
        // Our indexing logic depends on a direct mapping into m_VideoTextureResourceViews
        // based on the texture index provided by FFmpeg.
        srvIndex = (uintptr_t)frame->data[1];
        SDL_assert(srvIndex < m_VideoTextureResourceViews.size());
        if (srvIndex >= m_VideoTextureResourceViews.size()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Unexpected texture index: %u",
                         srvIndex);
            return;
        }


        // Ensure decoding operations have completed using a dummy fence.
        // This is not necessary on modern GPU drivers, but it is required
        // on some older Intel GPU drivers that don't properly synchronize
        // the video engine with 3D operations.
        if (m_UseFenceHack && m_FenceType != SupportedFenceType::None) {
            ComPtr<ID3D11Device5> device5;
            ComPtr<ID3D11DeviceContext4> deviceContext4;
            if (SUCCEEDED(m_Device.As(&device5)) && SUCCEEDED(m_DeviceContext.As(&deviceContext4))) {
                ComPtr<ID3D11Fence> fence;
                if (SUCCEEDED(device5->CreateFence(0,
                                                   m_FenceType == SupportedFenceType::Monitored ?
                                                       D3D11_FENCE_FLAG_NONE : D3D11_FENCE_FLAG_NON_MONITORED,
                                                   IID_PPV_ARGS(&fence)))) {
                    if (SUCCEEDED(deviceContext4->Signal(fence.Get(), 1))) {
                        deviceContext4->Wait(fence.Get(), 1);
                    }
                }
            }
        }
    }
    else {
        // Copy this frame (minus alignment padding) into our video texture
        D3D11_BOX srcBox;
        srcBox.left = 0;
        srcBox.top = 0;
        srcBox.right = m_DecoderParams.width;
        srcBox.bottom = m_DecoderParams.height;
        srcBox.front = 0;
        srcBox.back = 1;
        m_DeviceContext->CopySubresourceRegion(m_VideoTexture.Get(), 0, 0, 0, 0, (ID3D11Resource*)frame->data[0], (int)(intptr_t)frame->data[1], &srcBox);

        // SRV 0 is always mapped to the video texture
        srvIndex = 0;
    }

    // Bind our CSC shader (and constant buffer, if required)
    bindColorConversion(frame);

    // Bind SRVs for this frame
    ID3D11ShaderResourceView* frameSrvs[] = { m_VideoTextureResourceViews[srvIndex][0].Get(), m_VideoTextureResourceViews[srvIndex][1].Get() };
    m_DeviceContext->PSSetShaderResources(0, 2, frameSrvs);

    // Draw the video
    m_DeviceContext->DrawIndexed(6, 0, 0);

    // Unbind SRVs for this frame
    ID3D11ShaderResourceView* nullSrvs[2] = {};
    m_DeviceContext->PSSetShaderResources(0, 2, nullSrvs);
}

// This function must NOT use any DXGI or ID3D11DeviceContext methods
// since it can be called on an arbitrary thread!
void D3D11VARenderer::notifyOverlayUpdated(Overlay::OverlayType type)
{
    HRESULT hr;

    SDL_Surface* newSurface = Session::get()->getOverlayManager().getUpdatedOverlaySurface(type);
    bool overlayEnabled = Session::get()->getOverlayManager().isOverlayEnabled(type);
    if (newSurface == nullptr && overlayEnabled) {
        // The overlay is enabled and there is no new surface. Leave the old texture alone.
        return;
    }

    SDL_AtomicLock(&m_OverlayLock);
    ComPtr<ID3D11Texture2D> oldTexture = std::move(m_OverlayTextures[type]);
    ComPtr<ID3D11Buffer> oldVertexBuffer = std::move(m_OverlayVertexBuffers[type]);
    ComPtr<ID3D11ShaderResourceView> oldTextureResourceView = std::move(m_OverlayTextureResourceViews[type]);
    SDL_AtomicUnlock(&m_OverlayLock);

    // If the overlay is disabled, we're done
    if (!overlayEnabled) {
        SDL_FreeSurface(newSurface);
        return;
    }

    // Create a texture with our pixel data
    SDL_assert(!SDL_MUSTLOCK(newSurface));
    SDL_assert(newSurface->format->format == SDL_PIXELFORMAT_ARGB8888);

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = newSurface->w;
    texDesc.Height = newSurface->h;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA texData = {};
    texData.pSysMem = newSurface->pixels;
    texData.SysMemPitch = newSurface->pitch;

    ComPtr<ID3D11Texture2D> newTexture;
    hr = m_Device->CreateTexture2D(&texDesc, &texData, &newTexture);
    if (FAILED(hr)) {
        SDL_FreeSurface(newSurface);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateTexture2D() failed: %x",
                     hr);
        return;
    }

    ComPtr<ID3D11ShaderResourceView> newTextureResourceView;
    hr = m_Device->CreateShaderResourceView((ID3D11Resource*)newTexture.Get(), nullptr, &newTextureResourceView);
    if (FAILED(hr)) {
        SDL_FreeSurface(newSurface);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateShaderResourceView() failed: %x",
                     hr);
        return;
    }

    SDL_FRect renderRect = {};

    if (type == Overlay::OverlayStatusUpdate) {
        // Bottom Left
        renderRect.x = 0;
        renderRect.y = 0;
    }
    else if (type == Overlay::OverlayDebug) {
        // Top left
        renderRect.x = 0;
        renderRect.y = m_DisplayHeight - newSurface->h;
    }

    renderRect.w = newSurface->w;
    renderRect.h = newSurface->h;

    // Convert screen space to normalized device coordinates
    StreamUtils::screenSpaceToNormalizedDeviceCoords(&renderRect, m_DisplayWidth, m_DisplayHeight);

    // The surface is no longer required
    SDL_FreeSurface(newSurface);
    newSurface = nullptr;

    VERTEX verts[] =
    {
        {renderRect.x, renderRect.y, 0, 1},
        {renderRect.x, renderRect.y+renderRect.h, 0, 0},
        {renderRect.x+renderRect.w, renderRect.y, 1, 1},
        {renderRect.x+renderRect.w, renderRect.y+renderRect.h, 1, 0},
    };

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(verts);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = 0;
    vbDesc.MiscFlags = 0;
    vbDesc.StructureByteStride = sizeof(VERTEX);

    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = verts;

    ComPtr<ID3D11Buffer> newVertexBuffer;
    hr = m_Device->CreateBuffer(&vbDesc, &vbData, &newVertexBuffer);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateBuffer() failed: %x",
                     hr);
        return;
    }

    SDL_AtomicLock(&m_OverlayLock);
    m_OverlayVertexBuffers[type] = std::move(newVertexBuffer);
    m_OverlayTextures[type] = std::move(newTexture);
    m_OverlayTextureResourceViews[type] = std::move(newTextureResourceView);
    SDL_AtomicUnlock(&m_OverlayLock);
}

bool D3D11VARenderer::checkDecoderSupport(IDXGIAdapter* adapter)
{
    HRESULT hr;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice;

    DXGI_ADAPTER_DESC adapterDesc;
    hr = adapter->GetDesc(&adapterDesc);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IDXGIAdapter::GetDesc() failed: %x",
                     hr);
        return false;
    }

    // Derive a ID3D11VideoDevice from our ID3D11Device.
    hr = m_Device.As(&videoDevice);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::QueryInterface(ID3D11VideoDevice) failed: %x",
                     hr);
        return false;
    }

    // Check if the format is supported by this decoder
    BOOL supported;
    switch (m_DecoderParams.videoFormat)
    {
    case VIDEO_FORMAT_H264:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_H264_VLD_NOFGT, DXGI_FORMAT_NV12, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support H.264 decoding");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support H.264 decoding to NV12 format");
            return false;
        }
        break;

    case VIDEO_FORMAT_H264_HIGH8_444:
        // Unsupported by DXVA
        return false;

    case VIDEO_FORMAT_H265:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_HEVC_VLD_MAIN, DXGI_FORMAT_NV12, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC decoding");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC decoding to NV12 format");
            return false;
        }
        break;

    case VIDEO_FORMAT_H265_MAIN10:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10, DXGI_FORMAT_P010, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC Main10 decoding");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC Main10 decoding to P010 format");
            return false;
        }
        break;

    case VIDEO_FORMAT_H265_REXT8_444:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&k_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN_444, DXGI_FORMAT_AYUV, &supported)))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC Main 444 8-bit decoding via D3D11VA");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC Main 444 8-bit decoding to AYUV format");
            return false;
        }
        break;

    case VIDEO_FORMAT_H265_REXT10_444:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&k_D3D11_DECODER_PROFILE_HEVC_VLD_MAIN10_444, DXGI_FORMAT_Y410, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC Main 444 10-bit decoding via D3D11VA");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support HEVC Main 444 10-bit decoding to Y410 format");
            return false;
        }
        break;

    case VIDEO_FORMAT_AV1_MAIN8:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_AV1_VLD_PROFILE0, DXGI_FORMAT_NV12, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 decoding");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 decoding to NV12 format");
            return false;
        }
        break;

    case VIDEO_FORMAT_AV1_MAIN10:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_AV1_VLD_PROFILE0, DXGI_FORMAT_P010, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 Main 10-bit decoding");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 Main 10-bit decoding to P010 format");
            return false;
        }
        break;

    case VIDEO_FORMAT_AV1_HIGH8_444:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_AV1_VLD_PROFILE1, DXGI_FORMAT_AYUV, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 High 444 8-bit decoding");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 High 444 8-bit decoding to AYUV format");
            return false;
        }
        break;

    case VIDEO_FORMAT_AV1_HIGH10_444:
        if (FAILED(videoDevice->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_AV1_VLD_PROFILE1, DXGI_FORMAT_Y410, &supported))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 High 444 10-bit decoding");
            return false;
        }
        else if (!supported) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GPU doesn't support AV1 High 444 10-bit decoding to Y410 format");
            return false;
        }
        break;

    default:
        SDL_assert(false);
        return false;
    }

    if (DXUtil::isFormatHybridDecodedByHardware(m_DecoderParams.videoFormat, adapterDesc.VendorId, adapterDesc.DeviceId)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "GPU decoding for format %x is blocked due to hardware limitations",
                    m_DecoderParams.videoFormat);
        return false;
    }

    return true;
}

int D3D11VARenderer::getRendererAttributes()
{
    int attributes = 0;

    // This renderer supports HDR
    attributes |= RENDERER_ATTRIBUTE_HDR_SUPPORT;

    // This renderer requires frame pacing to synchronize with VBlank when we're in full-screen.
    // In windowed mode, we will render as fast we can and DWM will grab whatever is latest at the
    // time unless the user opts for pacing. We will use pacing in full-screen mode and normal DWM
    // sequencing in full-screen desktop mode to behave similarly to the DXVA2 renderer.
    if ((SDL_GetWindowFlags(m_DecoderParams.window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN) {
        attributes |= RENDERER_ATTRIBUTE_FORCE_PACING;
    }

    return attributes;
}

int D3D11VARenderer::getDecoderCapabilities()
{
    return CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC |
           CAPABILITY_REFERENCE_FRAME_INVALIDATION_AV1;
}

bool D3D11VARenderer::needsTestFrame()
{
    // We can usually determine when D3D11VA will work based on which decoder GUIDs are supported,
    // however there are some strange cases (Quadro P400 + Radeon HD 5570) where something goes
    // horribly wrong and D3D11VideoDevice::CreateVideoDecoder() fails inside FFmpeg. We need to
    // catch that case before we commit to using D3D11VA.
    return true;
}

IFFmpegRenderer::InitFailureReason D3D11VARenderer::getInitFailureReason()
{
    // In the specific case where we found at least one D3D11 hardware device but none of the
    // enumerated devices have support for the specified codec, tell the FFmpeg decoder not to
    // bother trying other hwaccels. We don't want to try loading D3D9 if the device doesn't
    // even have hardware support for the codec.
    //
    // NB: We use feature level 11.0 support as a gate here because we want to avoid returning
    // this failure reason in cases where we might have an extremely old GPU with support for
    // DXVA2 on D3D9 but not D3D11VA on D3D11. I'm unsure if any such drivers/hardware exists,
    // but better be safe than sorry.
    //
    // NB2: We're also assuming that no GPU exists which lacks any D3D11 driver but has drivers
    // for non-DX APIs like Vulkan. I believe this is a Windows Logo requirement so it should be
    // safe to assume.
    //
    // NB3: Sigh, there *are* GPUs drivers with greater codec support available via Vulkan than
    // D3D11VA even when both D3D11 and Vulkan APIs are supported. This is the case for HEVC RExt
    // profiles that were not supported by Microsoft until the Windows 11 24H2 SDK. Don't report
    // that hardware support is missing for YUV444 profiles since the Vulkan driver may support it.
    if (m_DevicesWithFL11Support != 0 && m_DevicesWithCodecSupport == 0 && !(m_DecoderParams.videoFormat & VIDEO_FORMAT_MASK_YUV444)) {
        return InitFailureReason::NoHardwareSupport;
    }
    else {
        return InitFailureReason::Unknown;
    }
}

void D3D11VARenderer::lockContext(void *lock_ctx)
{
    auto me = (D3D11VARenderer*)lock_ctx;

    SDL_LockMutex(me->m_ContextLock);
}

void D3D11VARenderer::unlockContext(void *lock_ctx)
{
    auto me = (D3D11VARenderer*)lock_ctx;

    SDL_UnlockMutex(me->m_ContextLock);
}

bool D3D11VARenderer::setupRenderingResources()
{
    HRESULT hr;

    m_DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // We use a common vertex shader for all pixel shaders
    {
        QByteArray vertexShaderBytecode = Path::readDataFile("d3d11_vertex.fxc");

        ComPtr<ID3D11VertexShader> vertexShader;
        hr = m_Device->CreateVertexShader(vertexShaderBytecode.constData(), vertexShaderBytecode.length(), nullptr, &vertexShader);
        if (SUCCEEDED(hr)) {
            m_DeviceContext->VSSetShader(vertexShader.Get(), nullptr, 0);
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateVertexShader() failed: %x",
                         hr);
            return false;
        }

        const D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        ComPtr<ID3D11InputLayout> inputLayout;
        hr = m_Device->CreateInputLayout(vertexDesc, ARRAYSIZE(vertexDesc), vertexShaderBytecode.constData(), vertexShaderBytecode.length(), &inputLayout);
        if (SUCCEEDED(hr)) {
            m_DeviceContext->IASetInputLayout(inputLayout.Get());
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateInputLayout() failed: %x",
                         hr);
            return false;
        }
    }

    {
        QByteArray overlayPixelShaderBytecode = Path::readDataFile("d3d11_overlay_pixel.fxc");

        hr = m_Device->CreatePixelShader(overlayPixelShaderBytecode.constData(), overlayPixelShaderBytecode.length(), nullptr, &m_OverlayPixelShader);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreatePixelShader() failed: %x",
                         hr);
            return false;
        }
    }

    for (int i = 0; i < PixelShaders::_COUNT; i++)
    {
        QByteArray videoPixelShaderBytecode = Path::readDataFile(k_VideoShaderNames[i]);

        hr = m_Device->CreatePixelShader(videoPixelShaderBytecode.constData(), videoPixelShaderBytecode.length(), nullptr, &m_VideoPixelShaders[i]);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreatePixelShader() failed: %x",
                         hr);
            return false;
        }
    }

    // We use a common sampler for all pixel shaders
    {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        samplerDesc.MinLOD = 0.0f;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

        ComPtr<ID3D11SamplerState> sampler;
        hr = m_Device->CreateSamplerState(&samplerDesc,  &sampler);
        if (SUCCEEDED(hr)) {
            m_DeviceContext->PSSetSamplers(0, 1, sampler.GetAddressOf());
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateSamplerState() failed: %x",
                         hr);
            return false;
        }
    }

    // Create our render target view
    {
        ComPtr<ID3D11Resource> backBufferResource;
        hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Resource), (void**)&backBufferResource);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "IDXGISwapChain::GetBuffer() failed: %x",
                         hr);
            return false;
        }

        hr = m_Device->CreateRenderTargetView(backBufferResource.Get(), nullptr, &m_RenderTargetView);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateRenderTargetView() failed: %x",
                         hr);
            return false;
        }
    }

    // We use a common index buffer for all geometry
    {
        const int indexes[] = {0, 1, 2, 3, 2, 1};
        D3D11_BUFFER_DESC indexBufferDesc = {};
        indexBufferDesc.ByteWidth = sizeof(indexes);
        indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        indexBufferDesc.CPUAccessFlags = 0;
        indexBufferDesc.MiscFlags = 0;
        indexBufferDesc.StructureByteStride = sizeof(int);

        D3D11_SUBRESOURCE_DATA indexBufferData = {};
        indexBufferData.pSysMem = indexes;
        indexBufferData.SysMemPitch = sizeof(int);

        ComPtr<ID3D11Buffer> indexBuffer;
        hr = m_Device->CreateBuffer(&indexBufferDesc, &indexBufferData, &indexBuffer);
        if (SUCCEEDED(hr)) {
            m_DeviceContext->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateBuffer() failed: %x",
                         hr);
            return false;
        }
    }

    // Create our fixed vertex buffer for video rendering
    {
        // Scale video to the window size while preserving aspect ratio
        SDL_Rect src, dst;
        src.x = src.y = 0;
        src.w = m_DecoderParams.width;
        src.h = m_DecoderParams.height;
        dst.x = dst.y = 0;
        dst.w = m_DisplayWidth;
        dst.h = m_DisplayHeight;
        StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

        // Convert screen space to normalized device coordinates
        SDL_FRect renderRect;
        StreamUtils::screenSpaceToNormalizedDeviceCoords(&dst, &renderRect, m_DisplayWidth, m_DisplayHeight);

        // If we're binding the decoder output textures directly, don't sample from the alignment padding area
        SDL_assert(m_TextureAlignment != 0);
        float uMax = m_BindDecoderOutputTextures ? ((float)m_DecoderParams.width / FFALIGN(m_DecoderParams.width, m_TextureAlignment)) : 1.0f;
        float vMax = m_BindDecoderOutputTextures ? ((float)m_DecoderParams.height / FFALIGN(m_DecoderParams.height, m_TextureAlignment)) : 1.0f;

        VERTEX verts[] =
        {
            {renderRect.x, renderRect.y, 0, vMax},
            {renderRect.x, renderRect.y+renderRect.h, 0, 0},
            {renderRect.x+renderRect.w, renderRect.y, uMax, vMax},
            {renderRect.x+renderRect.w, renderRect.y+renderRect.h, uMax, 0},
        };

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = sizeof(verts);
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = 0;
        vbDesc.MiscFlags = 0;
        vbDesc.StructureByteStride = sizeof(VERTEX);

        D3D11_SUBRESOURCE_DATA vbData = {};
        vbData.pSysMem = verts;

        hr = m_Device->CreateBuffer(&vbDesc, &vbData, &m_VideoVertexBuffer);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateBuffer() failed: %x",
                         hr);
            return false;
        }
    }

    // Create our fixed constant buffer to limit chroma texcoords and avoid sampling from alignment texels.
    {
        D3D11_BUFFER_DESC constDesc = {};
        constDesc.ByteWidth = sizeof(CSC_CONST_BUF);
        constDesc.Usage = D3D11_USAGE_IMMUTABLE;
        constDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constDesc.CPUAccessFlags = 0;
        constDesc.MiscFlags = 0;

        int textureWidth = m_BindDecoderOutputTextures ? FFALIGN(m_DecoderParams.width, m_TextureAlignment) : m_DecoderParams.width;
        int textureHeight = m_BindDecoderOutputTextures ? FFALIGN(m_DecoderParams.height, m_TextureAlignment) : m_DecoderParams.height;

        float chromaUVMax[3] = {};
        chromaUVMax[0] = m_DecoderParams.width != textureWidth ? ((float)(m_DecoderParams.width - 1) / textureWidth) : 1.0f;
        chromaUVMax[1] = m_DecoderParams.height != textureHeight ? ((float)(m_DecoderParams.height - 1) / textureHeight) : 1.0f;

        D3D11_SUBRESOURCE_DATA constData = {};
        constData.pSysMem = chromaUVMax;

        ComPtr<ID3D11Buffer> constantBuffer;
        HRESULT hr = m_Device->CreateBuffer(&constDesc, &constData, &constantBuffer);
        if (SUCCEEDED(hr)) {
            m_DeviceContext->PSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateBuffer() failed: %x",
                         hr);
            return false;
        }
    }

    // Create our blend state
    {
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        ComPtr<ID3D11BlendState> blendState;
        hr = m_Device->CreateBlendState(&blendDesc, &blendState);
        if (SUCCEEDED(hr)) {
            m_DeviceContext->OMSetBlendState(blendState.Get(), nullptr, 0xffffffff);
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateBlendState() failed: %x",
                         hr);
            return false;
        }
    }

    // Set a viewport that fills the window
    {
        D3D11_VIEWPORT viewport;

        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width = m_DisplayWidth;
        viewport.Height = m_DisplayHeight;
        viewport.MinDepth = 0;
        viewport.MaxDepth = 1;

        m_DeviceContext->RSSetViewports(1, &viewport);
    }

    return true;
}

std::vector<DXGI_FORMAT> D3D11VARenderer::getVideoTextureSRVFormats()
{
    if (m_DecoderParams.videoFormat & VIDEO_FORMAT_MASK_YUV444) {
        // YUV 4:4:4 formats don't use a second SRV
        return { (m_DecoderParams.videoFormat & VIDEO_FORMAT_MASK_10BIT) ?
                    DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM };
    }
    else if (m_DecoderParams.videoFormat & VIDEO_FORMAT_MASK_10BIT) {
        return { DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16G16_UNORM };
    }
    else {
        return { DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM };
    }
}

bool D3D11VARenderer::setupVideoTexture()
{
    SDL_assert(!m_BindDecoderOutputTextures);

    HRESULT hr;
    D3D11_TEXTURE2D_DESC texDesc = {};

    texDesc.Width = m_DecoderParams.width;
    texDesc.Height = m_DecoderParams.height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = m_TextureFormat;
    texDesc.SampleDesc.Quality = 0;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    hr = m_Device->CreateTexture2D(&texDesc, nullptr, &m_VideoTexture);
    if (FAILED(hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateTexture2D() failed: %x",
                     hr);
        return false;
    }

    // Create SRVs for the texture
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    size_t srvIndex = 0;
    for (DXGI_FORMAT srvFormat : getVideoTextureSRVFormats()) {
        SDL_assert(srvIndex < m_VideoTextureResourceViews[0].size());

        srvDesc.Format = srvFormat;
        hr = m_Device->CreateShaderResourceView(m_VideoTexture.Get(), &srvDesc, &m_VideoTextureResourceViews[0][srvIndex]);
        if (FAILED(hr)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ID3D11Device::CreateShaderResourceView() failed: %x",
                         hr);
            return false;
        }

        srvIndex++;
    }

    return true;
}

bool D3D11VARenderer::setupTexturePoolViews(AVD3D11VAFramesContext* frameContext)
{
    SDL_assert(m_BindDecoderOutputTextures);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.ArraySize = 1;

    // Create luminance and chrominance SRVs for each texture in the pool
    for (size_t i = 0; i < m_VideoTextureResourceViews.size(); i++) {
        HRESULT hr;

        // Our rendering logic depends on the texture index working to map into our SRV array
        SDL_assert(i == (size_t)frameContext->texture_infos[i].index);

        srvDesc.Texture2DArray.FirstArraySlice = frameContext->texture_infos[i].index;

        size_t srvIndex = 0;
        for (DXGI_FORMAT srvFormat : getVideoTextureSRVFormats()) {
            SDL_assert(srvIndex < m_VideoTextureResourceViews[i].size());

            srvDesc.Format = srvFormat;
            hr = m_Device->CreateShaderResourceView(frameContext->texture_infos[i].texture,
                                                    &srvDesc,
                                                    &m_VideoTextureResourceViews[i][srvIndex]);
            if (FAILED(hr)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "ID3D11Device::CreateShaderResourceView() failed: %x",
                             hr);
                return false;
            }

            srvIndex++;
        }
    }

    return true;
}
