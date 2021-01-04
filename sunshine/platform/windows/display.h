//
// Created by loki on 4/23/20.
//

#ifndef SUNSHINE_DISPLAY_H
#define SUNSHINE_DISPLAY_H

#include <dxgi.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3dcommon.h>
#include <dxgi1_2.h>

#include "sunshine/utility.h"
#include "sunshine/platform/common.h"

namespace platf::dxgi {
extern const char *format_str[];

template<class T>
void Release(T *dxgi) {
  dxgi->Release();
}

using factory1_t    = util::safe_ptr<IDXGIFactory1, Release<IDXGIFactory1>>;
using dxgi_t        = util::safe_ptr<IDXGIDevice, Release<IDXGIDevice>>;
using dxgi1_t       = util::safe_ptr<IDXGIDevice1, Release<IDXGIDevice1>>;
using device_t      = util::safe_ptr<ID3D11Device, Release<ID3D11Device>>;
using device_ctx_t  = util::safe_ptr<ID3D11DeviceContext, Release<ID3D11DeviceContext>>;
using adapter_t     = util::safe_ptr<IDXGIAdapter1, Release<IDXGIAdapter1>>;
using output_t      = util::safe_ptr<IDXGIOutput, Release<IDXGIOutput>>;
using output1_t     = util::safe_ptr<IDXGIOutput1, Release<IDXGIOutput1>>;
using dup_t         = util::safe_ptr<IDXGIOutputDuplication, Release<IDXGIOutputDuplication>>;
using texture2d_t   = util::safe_ptr<ID3D11Texture2D, Release<ID3D11Texture2D>>;
using resource_t    = util::safe_ptr<IDXGIResource, Release<IDXGIResource>>;
using multithread_t = util::safe_ptr<ID3D11Multithread, Release<ID3D11Multithread>>;

namespace video {
using device_t         = util::safe_ptr<ID3D11VideoDevice, Release<ID3D11VideoDevice>>;
using ctx_t            = util::safe_ptr<ID3D11VideoContext, Release<ID3D11VideoContext>>;
using processor_t      = util::safe_ptr<ID3D11VideoProcessor, Release<ID3D11VideoProcessor>>;
using processor_out_t  = util::safe_ptr<ID3D11VideoProcessorOutputView, Release<ID3D11VideoProcessorOutputView>>;
using processor_in_t   = util::safe_ptr<ID3D11VideoProcessorInputView, Release<ID3D11VideoProcessorInputView>>;
using processor_enum_t = util::safe_ptr<ID3D11VideoProcessorEnumerator, Release<ID3D11VideoProcessorEnumerator>>;
}

class hwdevice_t;
struct cursor_t {
  std::vector<std::uint8_t> img_data;

  DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info;
  int x, y;
  bool visible;
};

struct gpu_cursor_t {
  texture2d_t texture;

  LONG width, height;
};

class duplication_t {
public:
  dup_t dup;
  bool has_frame {};

  capture_e next_frame(DXGI_OUTDUPL_FRAME_INFO &frame_info, std::chrono::milliseconds timeout, resource_t::pointer *res_p);
  capture_e reset(dup_t::pointer dup_p = dup_t::pointer());
  capture_e release_frame();

  ~duplication_t();
};

class display_base_t : public display_t {
public:
  int init();

  factory1_t factory;
  adapter_t adapter;
  output_t output;
  device_t device;
  device_ctx_t device_ctx;
  duplication_t dup;

  DXGI_FORMAT format;
  D3D_FEATURE_LEVEL feature_level;

  typedef enum _D3DKMT_SCHEDULINGPRIORITYCLASS
  {
    D3DKMT_SCHEDULINGPRIORITYCLASS_IDLE,
    D3DKMT_SCHEDULINGPRIORITYCLASS_BELOW_NORMAL,
    D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL,
    D3DKMT_SCHEDULINGPRIORITYCLASS_ABOVE_NORMAL,
    D3DKMT_SCHEDULINGPRIORITYCLASS_HIGH,
    D3DKMT_SCHEDULINGPRIORITYCLASS_REALTIME
  }
  D3DKMT_SCHEDULINGPRIORITYCLASS;

  typedef NTSTATUS WINAPI (*PD3DKMTSetProcessSchedulingPriorityClass)(HANDLE, D3DKMT_SCHEDULINGPRIORITYCLASS);
};

class display_ram_t : public display_base_t {
public:
  capture_e snapshot(img_t *img, std::chrono::milliseconds timeout, bool cursor_visible) override;
  std::shared_ptr<img_t> alloc_img() override;
  int dummy_img(img_t *img) override;

  int init();

  cursor_t cursor;
  D3D11_MAPPED_SUBRESOURCE img_info;
  texture2d_t texture;
};

class display_vram_t : public display_base_t, public std::enable_shared_from_this<display_vram_t> {
public:
  capture_e snapshot(img_t *img, std::chrono::milliseconds timeout, bool cursor_visible) override;

  std::shared_ptr<img_t> alloc_img() override;
  int dummy_img(img_t *img_base) override;

  std::shared_ptr<platf::hwdevice_t> make_hwdevice(int width, int height, pix_fmt_e pix_fmt) override;

  gpu_cursor_t cursor;
  std::vector<hwdevice_t*> hwdevices;
};
}

#endif