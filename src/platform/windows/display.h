//
// Created by loki on 4/23/20.
//

#ifndef SUNSHINE_DISPLAY_H
#define SUNSHINE_DISPLAY_H

#include <d3d11.h>
#include <d3d11_4.h>
#include <d3dcommon.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <dxgi1_6.h>

#include "src/platform/common.h"
#include "src/utility.h"

namespace platf::dxgi {
extern const char *format_str[];

// Add D3D11_CREATE_DEVICE_DEBUG here to enable the D3D11 debug runtime.
// You should have a debugger like WinDbg attached to receive debug messages.
auto constexpr D3D11_CREATE_DEVICE_FLAGS = D3D11_CREATE_DEVICE_VIDEO_SUPPORT;

template<class T>
void Release(T *dxgi) {
  dxgi->Release();
}

using factory1_t            = util::safe_ptr<IDXGIFactory1, Release<IDXGIFactory1>>;
using dxgi_t                = util::safe_ptr<IDXGIDevice, Release<IDXGIDevice>>;
using dxgi1_t               = util::safe_ptr<IDXGIDevice1, Release<IDXGIDevice1>>;
using device_t              = util::safe_ptr<ID3D11Device, Release<ID3D11Device>>;
using device1_t             = util::safe_ptr<ID3D11Device1, Release<ID3D11Device1>>;
using device_ctx_t          = util::safe_ptr<ID3D11DeviceContext, Release<ID3D11DeviceContext>>;
using adapter_t             = util::safe_ptr<IDXGIAdapter1, Release<IDXGIAdapter1>>;
using output_t              = util::safe_ptr<IDXGIOutput, Release<IDXGIOutput>>;
using output1_t             = util::safe_ptr<IDXGIOutput1, Release<IDXGIOutput1>>;
using output5_t             = util::safe_ptr<IDXGIOutput5, Release<IDXGIOutput5>>;
using output6_t             = util::safe_ptr<IDXGIOutput6, Release<IDXGIOutput6>>;
using dup_t                 = util::safe_ptr<IDXGIOutputDuplication, Release<IDXGIOutputDuplication>>;
using texture2d_t           = util::safe_ptr<ID3D11Texture2D, Release<ID3D11Texture2D>>;
using texture1d_t           = util::safe_ptr<ID3D11Texture1D, Release<ID3D11Texture1D>>;
using resource_t            = util::safe_ptr<IDXGIResource, Release<IDXGIResource>>;
using resource1_t           = util::safe_ptr<IDXGIResource1, Release<IDXGIResource1>>;
using multithread_t         = util::safe_ptr<ID3D11Multithread, Release<ID3D11Multithread>>;
using vs_t                  = util::safe_ptr<ID3D11VertexShader, Release<ID3D11VertexShader>>;
using ps_t                  = util::safe_ptr<ID3D11PixelShader, Release<ID3D11PixelShader>>;
using blend_t               = util::safe_ptr<ID3D11BlendState, Release<ID3D11BlendState>>;
using input_layout_t        = util::safe_ptr<ID3D11InputLayout, Release<ID3D11InputLayout>>;
using render_target_t       = util::safe_ptr<ID3D11RenderTargetView, Release<ID3D11RenderTargetView>>;
using shader_res_t          = util::safe_ptr<ID3D11ShaderResourceView, Release<ID3D11ShaderResourceView>>;
using buf_t                 = util::safe_ptr<ID3D11Buffer, Release<ID3D11Buffer>>;
using raster_state_t        = util::safe_ptr<ID3D11RasterizerState, Release<ID3D11RasterizerState>>;
using sampler_state_t       = util::safe_ptr<ID3D11SamplerState, Release<ID3D11SamplerState>>;
using blob_t                = util::safe_ptr<ID3DBlob, Release<ID3DBlob>>;
using depth_stencil_state_t = util::safe_ptr<ID3D11DepthStencilState, Release<ID3D11DepthStencilState>>;
using depth_stencil_view_t  = util::safe_ptr<ID3D11DepthStencilView, Release<ID3D11DepthStencilView>>;
using keyed_mutex_t         = util::safe_ptr<IDXGIKeyedMutex, Release<IDXGIKeyedMutex>>;

namespace video {
using device_t         = util::safe_ptr<ID3D11VideoDevice, Release<ID3D11VideoDevice>>;
using ctx_t            = util::safe_ptr<ID3D11VideoContext, Release<ID3D11VideoContext>>;
using processor_t      = util::safe_ptr<ID3D11VideoProcessor, Release<ID3D11VideoProcessor>>;
using processor_out_t  = util::safe_ptr<ID3D11VideoProcessorOutputView, Release<ID3D11VideoProcessorOutputView>>;
using processor_in_t   = util::safe_ptr<ID3D11VideoProcessorInputView, Release<ID3D11VideoProcessorInputView>>;
using processor_enum_t = util::safe_ptr<ID3D11VideoProcessorEnumerator, Release<ID3D11VideoProcessorEnumerator>>;
} // namespace video

class hwdevice_t;
struct cursor_t {
  std::vector<std::uint8_t> img_data;

  DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info;
  int x, y;
  bool visible;
};

class gpu_cursor_t {
public:
  gpu_cursor_t() : cursor_view { 0, 0, 0, 0, 0.0f, 1.0f } {};
  void set_pos(LONG rel_x, LONG rel_y, bool visible) {
    cursor_view.TopLeftX = rel_x;
    cursor_view.TopLeftY = rel_y;

    this->visible = visible;
  }

  void set_texture(LONG width, LONG height, texture2d_t &&texture) {
    cursor_view.Width  = width;
    cursor_view.Height = height;

    this->texture = std::move(texture);
  }

  texture2d_t texture;
  shader_res_t input_res;

  D3D11_VIEWPORT cursor_view;

  bool visible;
};

class duplication_t {
public:
  dup_t dup;
  bool has_frame {};
  bool use_dwmflush {};

  capture_e next_frame(DXGI_OUTDUPL_FRAME_INFO &frame_info, std::chrono::milliseconds timeout, resource_t::pointer *res_p);
  capture_e reset(dup_t::pointer dup_p = dup_t::pointer());
  capture_e release_frame();

  ~duplication_t();
};

class display_base_t : public display_t {
public:
  int init(const ::video::config_t &config, const std::string &display_name);
  capture_e capture(snapshot_cb_t &&snapshot_cb, std::shared_ptr<img_t> img, bool *cursor) override;

  std::chrono::nanoseconds delay;

  factory1_t factory;
  adapter_t adapter;
  output_t output;
  device_t device;
  device_ctx_t device_ctx;
  duplication_t dup;

  DXGI_FORMAT capture_format;
  D3D_FEATURE_LEVEL feature_level;

  typedef enum _D3DKMT_SCHEDULINGPRIORITYCLASS {
    D3DKMT_SCHEDULINGPRIORITYCLASS_IDLE,
    D3DKMT_SCHEDULINGPRIORITYCLASS_BELOW_NORMAL,
    D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL,
    D3DKMT_SCHEDULINGPRIORITYCLASS_ABOVE_NORMAL,
    D3DKMT_SCHEDULINGPRIORITYCLASS_HIGH,
    D3DKMT_SCHEDULINGPRIORITYCLASS_REALTIME
  } D3DKMT_SCHEDULINGPRIORITYCLASS;

  typedef NTSTATUS WINAPI (*PD3DKMTSetProcessSchedulingPriorityClass)(HANDLE, D3DKMT_SCHEDULINGPRIORITYCLASS);

  virtual bool is_hdr() override;
  virtual bool get_hdr_metadata(SS_HDR_METADATA &metadata) override;

protected:
  int get_pixel_pitch() {
    return (capture_format == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 8 : 4;
  }

  const char *dxgi_format_to_string(DXGI_FORMAT format);
  const char *colorspace_to_string(DXGI_COLOR_SPACE_TYPE type);

  virtual capture_e snapshot(img_t *img, std::chrono::milliseconds timeout, bool cursor_visible) = 0;
  virtual int complete_img(img_t *img, bool dummy)                                               = 0;
  virtual std::vector<DXGI_FORMAT> get_supported_sdr_capture_formats()                           = 0;
  virtual std::vector<DXGI_FORMAT> get_supported_hdr_capture_formats()                           = 0;
};

class display_ram_t : public display_base_t {
public:
  virtual capture_e snapshot(img_t *img, std::chrono::milliseconds timeout, bool cursor_visible) override;

  std::shared_ptr<img_t> alloc_img() override;
  int dummy_img(img_t *img) override;
  int complete_img(img_t *img, bool dummy) override;
  std::vector<DXGI_FORMAT> get_supported_sdr_capture_formats() override;
  std::vector<DXGI_FORMAT> get_supported_hdr_capture_formats() override;

  int init(const ::video::config_t &config, const std::string &display_name);

  cursor_t cursor;
  D3D11_MAPPED_SUBRESOURCE img_info;
  texture2d_t texture;
};

class display_vram_t : public display_base_t, public std::enable_shared_from_this<display_vram_t> {
public:
  virtual capture_e snapshot(img_t *img, std::chrono::milliseconds timeout, bool cursor_visible) override;

  std::shared_ptr<img_t> alloc_img() override;
  int dummy_img(img_t *img_base) override;
  int complete_img(img_t *img_base, bool dummy) override;
  std::vector<DXGI_FORMAT> get_supported_sdr_capture_formats() override;
  std::vector<DXGI_FORMAT> get_supported_hdr_capture_formats() override;

  int init(const ::video::config_t &config, const std::string &display_name);

  std::shared_ptr<platf::hwdevice_t> make_hwdevice(pix_fmt_e pix_fmt) override;

  sampler_state_t sampler_linear;

  blend_t blend_alpha;
  blend_t blend_invert;
  blend_t blend_disable;

  ps_t scene_ps;
  vs_t scene_vs;

  gpu_cursor_t cursor_alpha;
  gpu_cursor_t cursor_xor;

  texture2d_t last_frame_copy;

  std::atomic<uint32_t> next_image_id;
};
} // namespace platf::dxgi

#endif
