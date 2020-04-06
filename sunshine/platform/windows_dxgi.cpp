//
// Created by loki on 1/12/20.
//

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <dxgi.h>
#include <d3d11.h>
#include <d3dcommon.h>
#include <dxgi1_2.h>

#include <codecvt>

#include "sunshine/config.h"
#include "sunshine/main.h"
#include "common.h"

namespace platf {
using namespace std::literals;
}
namespace platf::dxgi {
template<class T>
void Release(T *dxgi) {
  dxgi->Release();
}

using factory1_t   = util::safe_ptr<IDXGIFactory1, Release<IDXGIFactory1>>;
using dxgi_t       = util::safe_ptr<IDXGIDevice, Release<IDXGIDevice>>;
using dxgi1_t      = util::safe_ptr<IDXGIDevice1, Release<IDXGIDevice1>>;
using device_t     = util::safe_ptr<ID3D11Device, Release<ID3D11Device>>;
using device_ctx_t = util::safe_ptr<ID3D11DeviceContext, Release<ID3D11DeviceContext>>;
using adapter_t    = util::safe_ptr<IDXGIAdapter1, Release<IDXGIAdapter1>>;
using output_t     = util::safe_ptr<IDXGIOutput, Release<IDXGIOutput>>;
using output1_t    = util::safe_ptr<IDXGIOutput1, Release<IDXGIOutput1>>;
using dup_t        = util::safe_ptr<IDXGIOutputDuplication, Release<IDXGIOutputDuplication>>;
using texture2d_t  = util::safe_ptr<ID3D11Texture2D, Release<ID3D11Texture2D>>;
using resource_t   = util::safe_ptr<IDXGIResource, Release<IDXGIResource>>;

namespace video {
using device_t         = util::safe_ptr<ID3D11VideoDevice, Release<ID3D11VideoDevice>>;
using ctx_t            = util::safe_ptr<ID3D11VideoContext, Release<ID3D11VideoContext>>;
using processor_t      = util::safe_ptr<ID3D11VideoProcessor, Release<ID3D11VideoProcessor>>;
using processor_out_t  = util::safe_ptr<ID3D11VideoProcessorOutputView, Release<ID3D11VideoProcessorOutputView>>;
using processor_in_t   = util::safe_ptr<ID3D11VideoProcessorInputView, Release<ID3D11VideoProcessorInputView>>;
using processor_enum_t = util::safe_ptr<ID3D11VideoProcessorEnumerator, Release<ID3D11VideoProcessorEnumerator>>;
}

extern const char *format_str[];

class duplication_t {
public:
  dup_t dup;
  bool has_frame {};

  capture_e next_frame(DXGI_OUTDUPL_FRAME_INFO &frame_info, resource_t::pointer *res_p) {
    auto capture_status = release_frame();
    if(capture_status != capture_e::ok) {
      return capture_status;
    }

    auto status = dup->AcquireNextFrame(1000, &frame_info, res_p);

    switch(status) {
      case S_OK:
        has_frame = true;
        return capture_e::ok;
      case DXGI_ERROR_WAIT_TIMEOUT:
        return capture_e::timeout;
      case WAIT_ABANDONED:
      case DXGI_ERROR_ACCESS_LOST:
      case DXGI_ERROR_ACCESS_DENIED:
        return capture_e::reinit;
      default:
        BOOST_LOG(error) << "Couldn't acquire next frame [0x"sv << util::hex(status).to_string_view();
        return capture_e::error;
    }
  }

  capture_e reset(dup_t::pointer dup_p = dup_t::pointer()) {
    auto capture_status = release_frame();

    dup.reset(dup_p);

    return capture_status;
  }

  capture_e release_frame() {
    if(!has_frame) {
      return capture_e::ok;
    }

    auto status = dup->ReleaseFrame();
    switch (status) {
      case S_OK:
        has_frame = false;
        return capture_e::ok;
      case DXGI_ERROR_WAIT_TIMEOUT:
        return capture_e::timeout;
      case WAIT_ABANDONED:
      case DXGI_ERROR_ACCESS_LOST:
      case DXGI_ERROR_ACCESS_DENIED:
        has_frame = false;
        return capture_e::reinit;
      default:
        BOOST_LOG(error) << "Couldn't release frame [0x"sv << util::hex(status).to_string_view();
        return capture_e::error;
    }
  }

  ~duplication_t() {
    release_frame();
  }
};

struct img_t : public ::platf::img_t  {
  ~img_t() override {
    delete[] data;
    data = nullptr;
  }
};

struct img_d3d_t : public ::platf::img_t {
  std::shared_ptr<platf::display_t> display;
  texture2d_t texture;

  ~img_d3d_t() override = default;
};

struct cursor_t {
  std::vector<std::uint8_t> img_data;

  DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info;
  int x, y;
  bool visible;
};

void blend_cursor_monochrome(const cursor_t &cursor, img_t &img) {
  int height = cursor.shape_info.Height / 2;
  int width  = cursor.shape_info.Width;
  int pitch  = cursor.shape_info.Pitch;

  // img cursor.{x,y} < 0, skip parts of the cursor.img_data
  auto cursor_skip_y = -std::min(0, cursor.y);
  auto cursor_skip_x = -std::min(0, cursor.x);

  // img cursor.{x,y} > img.{x,y}, truncate parts of the cursor.img_data 
  auto cursor_truncate_y = std::max(0, cursor.y - img.height);
  auto cursor_truncate_x = std::max(0, cursor.x - img.width);

  auto cursor_width = width - cursor_skip_x - cursor_truncate_x;
  auto cursor_height = height - cursor_skip_y - cursor_truncate_y;

  if(cursor_height > height || cursor_width > width) {
    return;
  }

  auto img_skip_y    = std::max(0, cursor.y);
  auto img_skip_x    = std::max(0, cursor.x);

  auto cursor_img_data = cursor.img_data.data() + cursor_skip_y * pitch;

  int delta_height = std::min(cursor_height - cursor_truncate_y, std::max(0, img.height - img_skip_y));
  int delta_width = std::min(cursor_width - cursor_truncate_x, std::max(0, img.width - img_skip_x));

  auto pixels_per_byte = width / pitch;
  auto bytes_per_row = delta_width / pixels_per_byte;

  auto img_data = (int*)img.data;
  for(int i = 0; i < delta_height; ++i) {
    auto and_mask = &cursor_img_data[i * pitch];
    auto xor_mask = &cursor_img_data[(i + height) * pitch];

    auto img_pixel_p = &img_data[(i + img_skip_y) * (img.row_pitch / img.pixel_pitch) + img_skip_x];

    auto skip_x = cursor_skip_x;
    for(int x = 0; x < bytes_per_row; ++x) {
      for(auto bit = 0u; bit < 8; ++bit) {
        if(skip_x > 0) {
          --skip_x;

          continue;
        }

        int and_ = *and_mask & (1 << (7 - bit)) ? -1 : 0;
        int xor_ = *xor_mask & (1 << (7 - bit)) ? -1 : 0;

        *img_pixel_p &= and_;
        *img_pixel_p ^= xor_;

        ++img_pixel_p;
      }

      ++and_mask;
      ++xor_mask;
    }
  }
}

void apply_color_alpha(int *img_pixel_p, int cursor_pixel) {
  auto colors_out = (std::uint8_t*)&cursor_pixel;
  auto colors_in  = (std::uint8_t*)img_pixel_p;

  //TODO: When use of IDXGIOutput5 is implemented, support different color formats
  auto alpha = colors_out[3];
  if(alpha == 255) {
    *img_pixel_p = cursor_pixel;
  }
  else {
    colors_in[0] = colors_out[0] + (colors_in[0] * (255 - alpha) + 255/2) / 255;
    colors_in[1] = colors_out[1] + (colors_in[1] * (255 - alpha) + 255/2) / 255;
    colors_in[2] = colors_out[2] + (colors_in[2] * (255 - alpha) + 255/2) / 255;
  }
}

void apply_color_masked(int *img_pixel_p, int cursor_pixel) {
  //TODO: When use of IDXGIOutput5 is implemented, support different color formats
  auto alpha = ((std::uint8_t*)&cursor_pixel)[3];
  if(alpha == 0xFF) {
    *img_pixel_p ^= cursor_pixel;
  }
  else {
    *img_pixel_p = cursor_pixel;
  }
}

void blend_cursor_color(const cursor_t &cursor, img_t &img, const bool masked) {
  int height = cursor.shape_info.Height;
  int width  = cursor.shape_info.Width;
  int pitch  = cursor.shape_info.Pitch;

  // img cursor.y < 0, skip parts of the cursor.img_data
  auto cursor_skip_y = -std::min(0, cursor.y);
  auto cursor_skip_x = -std::min(0, cursor.x);

  // img cursor.{x,y} > img.{x,y}, truncate parts of the cursor.img_data 
  auto cursor_truncate_y = std::max(0, cursor.y - img.height);
  auto cursor_truncate_x = std::max(0, cursor.x - img.width);

  auto img_skip_y    = std::max(0, cursor.y);
  auto img_skip_x    = std::max(0, cursor.x);

  auto cursor_width = width - cursor_skip_x - cursor_truncate_x;
  auto cursor_height = height - cursor_skip_y - cursor_truncate_y;

  if(cursor_height > height || cursor_width > width) {
    return;
  }

  auto cursor_img_data = (int*)&cursor.img_data[cursor_skip_y * pitch];

  int delta_height = std::min(cursor_height - cursor_truncate_y, std::max(0, img.height - img_skip_y));
  int delta_width = std::min(cursor_width - cursor_truncate_x, std::max(0, img.width - img_skip_x));

  auto img_data = (int*)img.data;

  for(int i = 0; i < delta_height; ++i) {
    auto cursor_begin = &cursor_img_data[i * cursor.shape_info.Width + cursor_skip_x];
    auto cursor_end = &cursor_begin[delta_width];

    auto img_pixel_p = &img_data[(i + img_skip_y) * (img.row_pitch / img.pixel_pitch) + img_skip_x];
    std::for_each(cursor_begin, cursor_end, [&](int cursor_pixel) {
      if(masked) {
        apply_color_masked(img_pixel_p, cursor_pixel);
      }
      else {
        apply_color_alpha(img_pixel_p, cursor_pixel);
      }
      ++img_pixel_p;
    });
  }
}

void blend_cursor(const cursor_t &cursor, img_t &img) {
  switch(cursor.shape_info.Type) {
    case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
      blend_cursor_color(cursor, img, false);
      break;
    case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME:
      blend_cursor_monochrome(cursor, img);
      break;
    case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR:
      blend_cursor_color(cursor, img, true);
      break;
    default:
      BOOST_LOG(warning) << "Unsupported cursor format ["sv << cursor.shape_info.Type << ']';
  }
}

class hwdevice_ctx_t : public platf::hwdevice_ctx_t {
public:
  const platf::img_t*const convert(platf::img_t &img_base) override {
    auto &img = (img_d3d_t&)img_base;

    auto it = texture_to_processor_in.find(img.texture.get());
    if(it == std::end(texture_to_processor_in)) {
      D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc = { 0, (D3D11_VPIV_DIMENSION)D3D11_VPIV_DIMENSION_TEXTURE2D, { 0, 0 } };

      video::processor_in_t::pointer processor_in_p;
      auto status = device->CreateVideoProcessorInputView(img.texture.get(), processor_e.get(), &input_desc, &processor_in_p);
      if(FAILED(status)) {
        BOOST_LOG(error) << "Failed to create VideoProcessorInputView [0x"sv << util::hex(status).to_string_view() << ']';
        return nullptr;
      }
      it = texture_to_processor_in.emplace(img.texture.get(), processor_in_p).first;
    }
    auto &processor_in = it->second;

    D3D11_VIDEO_PROCESSOR_STREAM stream { TRUE, 0, 0, 0, 0, nullptr, processor_in.get(), nullptr };
    auto status = ctx->VideoProcessorBlt(processor.get(), processor_out.get(), 0, 1, &stream);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed size and color conversion [0x"sv << util::hex(status).to_string_view() << ']';
      return nullptr;
    }

    return &this->img;
  }

  int init(std::shared_ptr<platf::display_t> display, device_t::pointer device_p, device_ctx_t::pointer device_ctx_p, int in_width, int in_height, int out_width, int out_height) {
    HRESULT status;

    video::device_t::pointer vdevice_p;
    status = device_p->QueryInterface(IID_ID3D11VideoDevice, (void**)&vdevice_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to query ID3D11VideoDevice interface [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    device.reset(vdevice_p);

    video::ctx_t::pointer ctx_p;
    status = device_ctx_p->QueryInterface(IID_ID3D11VideoContext, (void**)&ctx_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to query ID3D11VideoContext interface [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    ctx.reset(ctx_p);

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc {
      D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE,
      { 1, 1 }, (UINT)in_width, (UINT)in_height,
      { 1, 1 }, (UINT)out_width, (UINT)out_height,
      D3D11_VIDEO_USAGE_PLAYBACK_NORMAL
    };

    video::processor_enum_t::pointer vp_e_p;
    status = device->CreateVideoProcessorEnumerator(&contentDesc, &vp_e_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create video processor enumerator [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    processor_e.reset(vp_e_p);

    video::processor_t::pointer processor_p;
    status = device->CreateVideoProcessor(processor_e.get(), 0, &processor_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create video processor [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    processor.reset(processor_p);

    D3D11_TEXTURE2D_DESC t {};
    t.Width  = out_width;
    t.Height = out_height;
    t.MipLevels = 1;
    t.ArraySize = 1;
    t.SampleDesc.Count = 1;
    t.Usage = D3D11_USAGE_DEFAULT;
    t.Format = DXGI_FORMAT_NV12;
    t.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_VIDEO_ENCODER;

    dxgi::texture2d_t::pointer tex_p {};
    status = device_p->CreateTexture2D(&t, nullptr, &tex_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create texture [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    img.texture.reset(tex_p);
    img.display = std::move(display);
    img.width = out_width;
    img.height = out_height;
    img.data = (std::uint8_t*)tex_p;
    img.row_pitch = out_width;
    img.pixel_pitch = 1;

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc { D3D11_VPOV_DIMENSION_TEXTURE2D, 0 };
    video::processor_out_t::pointer processor_out_p;
    status = device->CreateVideoProcessorOutputView(tex_p, processor_e.get(), &output_desc, &processor_out_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create VideoProcessorOutputView [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    processor_out.reset(processor_out_p);

    device_p->AddRef();
    hwdevice = device_p;
    return 0;
  }

  ~hwdevice_ctx_t() override {
    if(hwdevice) {
      ((ID3D11Device*)hwdevice)->Release();
    }
  }

  img_d3d_t img;
  video::device_t device;
  video::ctx_t ctx;
  video::processor_enum_t processor_e;
  video::processor_t processor;
  video::processor_out_t processor_out;
  std::unordered_map<texture2d_t::pointer, video::processor_in_t> texture_to_processor_in;
};

class display_base_t : public ::platf::display_t {
public:
  int init() {
/* Uncomment when use of IDXGIOutput5 is implemented
  std::call_once(windows_cpp_once_flag, []() {
    DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
    const auto DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = ((DPI_AWARENESS_CONTEXT)-4);

    typedef BOOL (*User32_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT value);

    auto user32 = LoadLibraryA("user32.dll");
    auto f = (User32_SetProcessDpiAwarenessContext)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if(f) {
      f(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    FreeLibrary(user32);
  });
*/
    dxgi::factory1_t::pointer   factory_p {};
    dxgi::adapter_t::pointer    adapter_p {};
    dxgi::output_t::pointer     output_p {};
    dxgi::device_t::pointer     device_p {};
    dxgi::device_ctx_t::pointer device_ctx_p {};

    HRESULT status;

    status = CreateDXGIFactory1(IID_IDXGIFactory1, (void**)&factory_p);
    factory.reset(factory_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create DXGIFactory1 [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;

    auto adapter_name = converter.from_bytes(config::video.adapter_name);
    auto output_name = converter.from_bytes(config::video.output_name);

    for(int x = 0; factory_p->EnumAdapters1(x, &adapter_p) != DXGI_ERROR_NOT_FOUND; ++x) {
      dxgi::adapter_t adapter_tmp { adapter_p };

      DXGI_ADAPTER_DESC1 adapter_desc;
      adapter_tmp->GetDesc1(&adapter_desc);

      if(!adapter_name.empty() && adapter_desc.Description != adapter_name) {
        continue;
      }

      for(int y = 0; adapter_tmp->EnumOutputs(y, &output_p) != DXGI_ERROR_NOT_FOUND; ++y) {
        dxgi::output_t output_tmp {output_p };

        DXGI_OUTPUT_DESC desc;
        output_tmp->GetDesc(&desc);

        if(!output_name.empty() && desc.DeviceName != output_name) {
          continue;
        }

        if(desc.AttachedToDesktop) {
          output = std::move(output_tmp);

          width  = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
          height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
        }
      }

      if(output) {
        adapter = std::move(adapter_tmp);
        break;
      }
    }

    if(!output) {
      BOOST_LOG(error) << "Failed to locate an output device"sv;
      return -1;
    }

    D3D_FEATURE_LEVEL featureLevels[] {
      D3D_FEATURE_LEVEL_12_1,
      D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,
      D3D_FEATURE_LEVEL_9_2,
      D3D_FEATURE_LEVEL_9_1
    };

    status = adapter->QueryInterface(IID_IDXGIAdapter, (void**)&adapter_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to query IDXGIAdapter interface"sv;

      return -1;
    }

    status = D3D11CreateDevice(
      adapter_p,
      D3D_DRIVER_TYPE_UNKNOWN,
      nullptr,
      D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
      featureLevels, sizeof(featureLevels) / sizeof(D3D_FEATURE_LEVEL),
      D3D11_SDK_VERSION,
      &device_p,
      &feature_level,
      &device_ctx_p);

    adapter_p->Release();

    device.reset(device_p);
    device_ctx.reset(device_ctx_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create D3D11 device [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }


    DXGI_ADAPTER_DESC adapter_desc;
    adapter->GetDesc(&adapter_desc);

    auto description = converter.to_bytes(adapter_desc.Description);
    BOOST_LOG(info)
      << std::endl
      << "Device Description : " << description << std::endl
      << "Device Vendor ID   : 0x"sv << util::hex(adapter_desc.VendorId).to_string_view() << std::endl
      << "Device Device ID   : 0x"sv << util::hex(adapter_desc.DeviceId).to_string_view() << std::endl
      << "Device Video Mem   : "sv << adapter_desc.DedicatedVideoMemory / 1048576 << " MiB"sv << std::endl
      << "Device Sys Mem     : "sv << adapter_desc.DedicatedSystemMemory / 1048576 << " MiB"sv << std::endl
      << "Share Sys Mem      : "sv << adapter_desc.SharedSystemMemory / 1048576 << " MiB"sv << std::endl
      << "Feature Level      : 0x"sv << util::hex(feature_level).to_string_view() << std::endl
      << "Capture size       : "sv << width << 'x'  << height;

    // Bump up thread priority
    {
      dxgi::dxgi_t::pointer dxgi_p {};
      status = device->QueryInterface(IID_IDXGIDevice, (void**)&dxgi_p);
      dxgi::dxgi_t dxgi { dxgi_p };

      if(FAILED(status)) {
        BOOST_LOG(error) << "Failed to query DXGI interface from device [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }

      dxgi->SetGPUThreadPriority(7);
    }

    // Try to reduce latency
    {
      dxgi::dxgi1_t::pointer dxgi_p {};
      status = device->QueryInterface(IID_IDXGIDevice, (void**)&dxgi_p);
      dxgi::dxgi1_t dxgi { dxgi_p };

      if(FAILED(status)) {
        BOOST_LOG(error) << "Failed to query DXGI interface from device [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }

      dxgi->SetMaximumFrameLatency(1);
    }

    //FIXME: Duplicate output on RX580 in combination with DOOM (2016) --> BSOD
    //TODO: Use IDXGIOutput5 for improved performance
    {
      dxgi::output1_t::pointer output1_p {};
      status = output->QueryInterface(IID_IDXGIOutput1, (void**)&output1_p);
      dxgi::output1_t output1 {output1_p };

      if(FAILED(status)) {
        BOOST_LOG(error) << "Failed to query IDXGIOutput1 from the output"sv;
        return -1;
      }

      // We try this twice, in case we still get an error on reinitialization
      for(int x = 0; x < 2; ++x) {
        dxgi::dup_t::pointer dup_p {};
        status = output1->DuplicateOutput((IUnknown*)device.get(), &dup_p);
        if(SUCCEEDED(status)) {
          dup.reset(dup_p);
          break;
        }
        std::this_thread::sleep_for(200ms);
      }

      if(FAILED(status)) {
        BOOST_LOG(error) << "DuplicateOutput Failed [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }
    }

    DXGI_OUTDUPL_DESC dup_desc;
    dup.dup->GetDesc(&dup_desc);

    format = dup_desc.ModeDesc.Format;

    BOOST_LOG(debug) << "Source format ["sv << format_str[dup_desc.ModeDesc.Format] << ']';

    return 0;
  }

  factory1_t factory;
  adapter_t adapter;
  output_t output;
  device_t device;
  device_ctx_t device_ctx;
  duplication_t dup;

  int width, height;

  DXGI_FORMAT format;
  D3D_FEATURE_LEVEL feature_level;
};

class display_cpu_t : public display_base_t {
public:
  capture_e snapshot(::platf::img_t *img_base, bool cursor_visible) override {
    auto img = (img_t*)img_base;

    HRESULT status;

    DXGI_OUTDUPL_FRAME_INFO frame_info;

    resource_t::pointer res_p {};
    auto capture_status = dup.next_frame(frame_info, &res_p);
    resource_t res{res_p};

    if (capture_status != capture_e::ok) {
      return capture_status;
    }

    if(frame_info.PointerShapeBufferSize > 0) {
      auto &img_data = cursor.img_data;

      img_data.resize(frame_info.PointerShapeBufferSize);

      UINT dummy;
      status = dup.dup->GetFramePointerShape(img_data.size(), img_data.data(), &dummy, &cursor.shape_info);
      if (FAILED(status)) {
        BOOST_LOG(error) << "Failed to get new pointer shape [0x"sv << util::hex(status).to_string_view() << ']';

        return capture_e::error;
      }
    }

    if(frame_info.LastMouseUpdateTime.QuadPart) {
      cursor.x = frame_info.PointerPosition.Position.x;
      cursor.y = frame_info.PointerPosition.Position.y;
      cursor.visible = frame_info.PointerPosition.Visible;
    }

    // If frame has been updated
    if (frame_info.LastPresentTime.QuadPart != 0) {
      {
        texture2d_t::pointer src_p {};
        status = res->QueryInterface(IID_ID3D11Texture2D, (void **)&src_p);
        texture2d_t src{src_p};

        if (FAILED(status)) {
          BOOST_LOG(error) << "Couldn't query interface [0x"sv << util::hex(status).to_string_view() << ']';
          return capture_e::error;
        }

        //Copy from GPU to CPU
        device_ctx->CopyResource(texture.get(), src.get());
      }

      if(img_info.pData) {
        device_ctx->Unmap(texture.get(), 0);
        img_info.pData = nullptr;
      }

      status = device_ctx->Map(texture.get(), 0, D3D11_MAP_READ, 0, &img_info);
      if (FAILED(status)) {
        BOOST_LOG(error) << "Failed to map texture [0x"sv << util::hex(status).to_string_view() << ']';

        return capture_e::error;
      }
    }

    const bool mouse_update = 
      (frame_info.LastMouseUpdateTime.QuadPart || frame_info.PointerShapeBufferSize > 0) &&
      (cursor_visible && cursor.visible);

    const bool update_flag = frame_info.LastPresentTime.QuadPart != 0 || mouse_update;

    if(!update_flag) {
      return capture_e::timeout;
    }

    if(img->width != width || img->height != height) {
      delete[] img->data;
      img->data = new std::uint8_t[height * img_info.RowPitch];

      img->width = width;
      img->height = height;
      img->row_pitch = img_info.RowPitch;
    }

    std::copy_n((std::uint8_t*)img_info.pData, height * img_info.RowPitch, (std::uint8_t*)img->data);

    if(cursor_visible && cursor.visible) {
      blend_cursor(cursor, *img);
    }

    return capture_e::ok;
  }

  std::shared_ptr<platf::img_t> alloc_img() override {
    auto img = std::make_shared<img_t>();

    img->data         = nullptr;
    img->row_pitch    = 0;
    img->pixel_pitch  = 4;
    img->width        = 0;
    img->height       = 0;

    return img;
  }

  int dummy_img(platf::img_t *img, int &) override {
    auto dummy_data_p = new int[1];

    return platf::display_t::dummy_img(img, *dummy_data_p);
  }

  int init() {
    if(display_base_t::init()) {
      return -1;
    }

    D3D11_TEXTURE2D_DESC t {};
    t.Width  = width;
    t.Height = height;
    t.MipLevels = 1;
    t.ArraySize = 1;
    t.SampleDesc.Count = 1;
    t.Usage = D3D11_USAGE_STAGING;
    t.Format = format;
    t.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    dxgi::texture2d_t::pointer tex_p {};
    auto status = device->CreateTexture2D(&t, nullptr, &tex_p);

    texture.reset(tex_p);

    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create texture [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    // map the texture simply to get the pitch and stride
    status = device_ctx->Map(texture.get(), 0, D3D11_MAP_READ, 0, &img_info);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to map the texture [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    return 0;
  }

  cursor_t cursor;
  D3D11_MAPPED_SUBRESOURCE img_info;
  texture2d_t texture;
};

class display_gpu_t : public display_base_t, public std::enable_shared_from_this<display_gpu_t> {
  capture_e snapshot(::platf::img_t *img_base, bool cursor_visible) override {
    auto img = (img_d3d_t*)img_base;

    HRESULT status;

    DXGI_OUTDUPL_FRAME_INFO frame_info;

    resource_t::pointer res_p {};
    auto capture_status = dup.next_frame(frame_info, &res_p);
    resource_t res{res_p};

    if (capture_status != capture_e::ok) {
      return capture_status;
    }

    const bool update_flag = frame_info.LastPresentTime.QuadPart != 0;
    if(!update_flag) {
      return capture_e::timeout;
    }

    texture2d_t::pointer src_p{};
    status = res->QueryInterface(IID_ID3D11Texture2D, (void **)&src_p);

    if (FAILED(status)) {
      BOOST_LOG(error) << "Couldn't query interface [0x"sv << util::hex(status).to_string_view() << ']';
      return capture_e::error;
    }

    img->row_pitch = 0;
    img->width     = width;
    img->height    = height;
    img->data      = (std::uint8_t*)src_p;
    img->texture.reset(src_p);

    return capture_e::ok;
  }

  std::shared_ptr<platf::img_t> alloc_img() override {
    auto img = std::make_shared<img_d3d_t>();

    img->data        = nullptr;
    img->row_pitch   = 0;
    img->pixel_pitch = 4;
    img->width       = 0;
    img->height      = 0;
    img->display     = shared_from_this();

    return img;
  }

  int dummy_img(platf::img_t *img_base, int &dummy_data_p) override {
    auto img = (img_d3d_t*)img_base;

    D3D11_TEXTURE2D_DESC t {};
    t.Width  = 1;
    t.Height = 1;
    t.MipLevels = 1;
    t.ArraySize = 1;
    t.SampleDesc.Count = 1;
    t.Usage = D3D11_USAGE_DEFAULT;
    t.Format = format;

    D3D11_SUBRESOURCE_DATA data {
      &dummy_data_p,
      (UINT)img->row_pitch,
      0
    };

    dxgi::texture2d_t::pointer tex_p {};
    auto status = device->CreateTexture2D(&t, &data, &tex_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create dummy texture [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    img->texture.reset(tex_p);

    img->height      = 1;
    img->width       = 1;
    img->data        = (std::uint8_t*)tex_p;
    img->row_pitch   = 4;
    img->pixel_pitch = 4;

    return 0;
  }

  std::shared_ptr<platf::hwdevice_ctx_t> make_hwdevice_ctx(int width, int height, pix_fmt_e pix_fmt) override {
    auto hwdevice = std::make_shared<hwdevice_ctx_t>();

    auto ret = hwdevice->init(
      shared_from_this(),
      device.get(),
      device_ctx.get(),
      this->width, this->height,
      width, height);

    if(ret) {
      return nullptr;
    }

    return hwdevice;
  }
};

const char *format_str[] = {
  "DXGI_FORMAT_UNKNOWN",
  "DXGI_FORMAT_R32G32B32A32_TYPELESS",
  "DXGI_FORMAT_R32G32B32A32_FLOAT",
  "DXGI_FORMAT_R32G32B32A32_UINT",
  "DXGI_FORMAT_R32G32B32A32_SINT",
  "DXGI_FORMAT_R32G32B32_TYPELESS",
  "DXGI_FORMAT_R32G32B32_FLOAT",
  "DXGI_FORMAT_R32G32B32_UINT",
  "DXGI_FORMAT_R32G32B32_SINT",
  "DXGI_FORMAT_R16G16B16A16_TYPELESS",
  "DXGI_FORMAT_R16G16B16A16_FLOAT",
  "DXGI_FORMAT_R16G16B16A16_UNORM",
  "DXGI_FORMAT_R16G16B16A16_UINT",
  "DXGI_FORMAT_R16G16B16A16_SNORM",
  "DXGI_FORMAT_R16G16B16A16_SINT",
  "DXGI_FORMAT_R32G32_TYPELESS",
  "DXGI_FORMAT_R32G32_FLOAT",
  "DXGI_FORMAT_R32G32_UINT",
  "DXGI_FORMAT_R32G32_SINT",
  "DXGI_FORMAT_R32G8X24_TYPELESS",
  "DXGI_FORMAT_D32_FLOAT_S8X24_UINT",
  "DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS",
  "DXGI_FORMAT_X32_TYPELESS_G8X24_UINT",
  "DXGI_FORMAT_R10G10B10A2_TYPELESS",
  "DXGI_FORMAT_R10G10B10A2_UNORM",
  "DXGI_FORMAT_R10G10B10A2_UINT",
  "DXGI_FORMAT_R11G11B10_FLOAT",
  "DXGI_FORMAT_R8G8B8A8_TYPELESS",
  "DXGI_FORMAT_R8G8B8A8_UNORM",
  "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB",
  "DXGI_FORMAT_R8G8B8A8_UINT",
  "DXGI_FORMAT_R8G8B8A8_SNORM",
  "DXGI_FORMAT_R8G8B8A8_SINT",
  "DXGI_FORMAT_R16G16_TYPELESS",
  "DXGI_FORMAT_R16G16_FLOAT",
  "DXGI_FORMAT_R16G16_UNORM",
  "DXGI_FORMAT_R16G16_UINT",
  "DXGI_FORMAT_R16G16_SNORM",
  "DXGI_FORMAT_R16G16_SINT",
  "DXGI_FORMAT_R32_TYPELESS",
  "DXGI_FORMAT_D32_FLOAT",
  "DXGI_FORMAT_R32_FLOAT",
  "DXGI_FORMAT_R32_UINT",
  "DXGI_FORMAT_R32_SINT",
  "DXGI_FORMAT_R24G8_TYPELESS",
  "DXGI_FORMAT_D24_UNORM_S8_UINT",
  "DXGI_FORMAT_R24_UNORM_X8_TYPELESS",
  "DXGI_FORMAT_X24_TYPELESS_G8_UINT",
  "DXGI_FORMAT_R8G8_TYPELESS",
  "DXGI_FORMAT_R8G8_UNORM",
  "DXGI_FORMAT_R8G8_UINT",
  "DXGI_FORMAT_R8G8_SNORM",
  "DXGI_FORMAT_R8G8_SINT",
  "DXGI_FORMAT_R16_TYPELESS",
  "DXGI_FORMAT_R16_FLOAT",
  "DXGI_FORMAT_D16_UNORM",
  "DXGI_FORMAT_R16_UNORM",
  "DXGI_FORMAT_R16_UINT",
  "DXGI_FORMAT_R16_SNORM",
  "DXGI_FORMAT_R16_SINT",
  "DXGI_FORMAT_R8_TYPELESS",
  "DXGI_FORMAT_R8_UNORM",
  "DXGI_FORMAT_R8_UINT",
  "DXGI_FORMAT_R8_SNORM",
  "DXGI_FORMAT_R8_SINT",
  "DXGI_FORMAT_A8_UNORM",
  "DXGI_FORMAT_R1_UNORM",
  "DXGI_FORMAT_R9G9B9E5_SHAREDEXP",
  "DXGI_FORMAT_R8G8_B8G8_UNORM",
  "DXGI_FORMAT_G8R8_G8B8_UNORM",
  "DXGI_FORMAT_BC1_TYPELESS",
  "DXGI_FORMAT_BC1_UNORM",
  "DXGI_FORMAT_BC1_UNORM_SRGB",
  "DXGI_FORMAT_BC2_TYPELESS",
  "DXGI_FORMAT_BC2_UNORM",
  "DXGI_FORMAT_BC2_UNORM_SRGB",
  "DXGI_FORMAT_BC3_TYPELESS",
  "DXGI_FORMAT_BC3_UNORM",
  "DXGI_FORMAT_BC3_UNORM_SRGB",
  "DXGI_FORMAT_BC4_TYPELESS",
  "DXGI_FORMAT_BC4_UNORM",
  "DXGI_FORMAT_BC4_SNORM",
  "DXGI_FORMAT_BC5_TYPELESS",
  "DXGI_FORMAT_BC5_UNORM",
  "DXGI_FORMAT_BC5_SNORM",
  "DXGI_FORMAT_B5G6R5_UNORM",
  "DXGI_FORMAT_B5G5R5A1_UNORM",
  "DXGI_FORMAT_B8G8R8A8_UNORM",
  "DXGI_FORMAT_B8G8R8X8_UNORM",
  "DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM",
  "DXGI_FORMAT_B8G8R8A8_TYPELESS",
  "DXGI_FORMAT_B8G8R8A8_UNORM_SRGB",
  "DXGI_FORMAT_B8G8R8X8_TYPELESS",
  "DXGI_FORMAT_B8G8R8X8_UNORM_SRGB",
  "DXGI_FORMAT_BC6H_TYPELESS",
  "DXGI_FORMAT_BC6H_UF16",
  "DXGI_FORMAT_BC6H_SF16",
  "DXGI_FORMAT_BC7_TYPELESS",
  "DXGI_FORMAT_BC7_UNORM",
  "DXGI_FORMAT_BC7_UNORM_SRGB",
  "DXGI_FORMAT_AYUV",
  "DXGI_FORMAT_Y410",
  "DXGI_FORMAT_Y416",
  "DXGI_FORMAT_NV12",
  "DXGI_FORMAT_P010",
  "DXGI_FORMAT_P016",
  "DXGI_FORMAT_420_OPAQUE",
  "DXGI_FORMAT_YUY2",
  "DXGI_FORMAT_Y210",
  "DXGI_FORMAT_Y216",
  "DXGI_FORMAT_NV11",
  "DXGI_FORMAT_AI44",
  "DXGI_FORMAT_IA44",
  "DXGI_FORMAT_P8",
  "DXGI_FORMAT_A8P8",
  "DXGI_FORMAT_B4G4R4A4_UNORM",

  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

  "DXGI_FORMAT_P208",
  "DXGI_FORMAT_V208",
  "DXGI_FORMAT_V408"
};

}

namespace platf {
std::shared_ptr<display_t> display(int hwdevice_type) {
  if(hwdevice_type == AV_HWDEVICE_TYPE_D3D11VA) {
    auto disp = std::make_shared<dxgi::display_gpu_t>();

    if(!disp->init()) {
      return disp;
    }
  }
  else {
    auto disp = std::make_shared<dxgi::display_cpu_t>();

    if(!disp->init()) {
      return disp;
    }
  }

  return nullptr;
}
}
