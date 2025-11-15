#pragma once

#include "renderer.h"

#include <d3d11_4.h>
#include <dxgi1_6.h>

extern "C" {
#include <libavutil/hwcontext_d3d11va.h>
}

#include <wrl/client.h>

class D3D11VARenderer : public IFFmpegRenderer
{
public:
    D3D11VARenderer(int decoderSelectionPass);
    virtual ~D3D11VARenderer() override;
    virtual bool initialize(PDECODER_PARAMETERS params) override;
    virtual bool prepareDecoderContext(AVCodecContext* context, AVDictionary**) override;
    virtual bool prepareDecoderContextInGetFormat(AVCodecContext* context, AVPixelFormat pixelFormat) override;
    virtual void renderFrame(AVFrame* frame) override;
    virtual void notifyOverlayUpdated(Overlay::OverlayType) override;
    virtual int getRendererAttributes() override;
    virtual int getDecoderCapabilities() override;
    virtual bool needsTestFrame() override;
    virtual InitFailureReason getInitFailureReason() override;

    enum PixelShaders {
        GENERIC_YUV_420,
        GENERIC_AYUV,
        GENERIC_Y410,
        _COUNT
    };

private:
    static void lockContext(void* lock_ctx);
    static void unlockContext(void* lock_ctx);

    bool setupRenderingResources();
    std::vector<DXGI_FORMAT> getVideoTextureSRVFormats();
    bool setupVideoTexture(); // for !m_BindDecoderOutputTextures
    bool setupTexturePoolViews(AVD3D11VAFramesContext* frameContext); // for m_BindDecoderOutputTextures
    void renderOverlay(Overlay::OverlayType type);
    void bindColorConversion(AVFrame* frame);
    void renderVideo(AVFrame* frame);
    bool checkDecoderSupport(IDXGIAdapter* adapter);
    bool createDeviceByAdapterIndex(int adapterIndex, bool* adapterNotFound = nullptr);

    int m_DecoderSelectionPass;
    int m_DevicesWithFL11Support;
    int m_DevicesWithCodecSupport;

    enum class SupportedFenceType {
        None,
        NonMonitored,
        Monitored,
    };

    Microsoft::WRL::ComPtr<IDXGIFactory5> m_Factory;
    Microsoft::WRL::ComPtr<ID3D11Device> m_Device;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_SwapChain;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_DeviceContext;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RenderTargetView;
    SupportedFenceType m_FenceType;
    SDL_mutex* m_ContextLock;
    bool m_BindDecoderOutputTextures;
    bool m_UseFenceHack;

    DECODER_PARAMETERS m_DecoderParams;
    int m_TextureAlignment;
    DXGI_FORMAT m_TextureFormat;
    UINT m_TextureWidth;
    UINT m_TextureHeight;
    int m_DisplayWidth;
    int m_DisplayHeight;
    AVColorTransferCharacteristic m_LastColorTrc;

    bool m_AllowTearing;

    std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>, PixelShaders::_COUNT> m_VideoPixelShaders;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_VideoVertexBuffer;

    // Only valid if !m_BindDecoderOutputTextures
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_VideoTexture;

    // Only index 0 is valid if !m_BindDecoderOutputTextures
#define DECODER_BUFFER_POOL_SIZE 17
    std::array<std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2>, DECODER_BUFFER_POOL_SIZE> m_VideoTextureResourceViews;

    SDL_SpinLock m_OverlayLock;
    std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, Overlay::OverlayMax> m_OverlayVertexBuffers;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, Overlay::OverlayMax> m_OverlayTextures;
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, Overlay::OverlayMax> m_OverlayTextureResourceViews;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_OverlayPixelShader;

    AVBufferRef* m_HwDeviceContext;
    AVBufferRef* m_HwFramesContext;
};

