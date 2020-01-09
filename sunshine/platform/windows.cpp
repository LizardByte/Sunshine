#include <iostream>
#include <thread>

#include <dxgi.h>
#include <d3d11.h>
#include <d3dcommon.h>
#include <dxgi1_2.h>

#include "common.h"

namespace platf {
using namespace std::literals;
std::string get_local_ip() { return "192.168.0.119"s; }

namespace dxgi {
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

extern const char *format_str[];

class duplication_t {
public:
  dup_t dup;
  bool has_frame {};

  capture_e next_frame(DXGI_OUTDUPL_FRAME_INFO &frame_info, resource_t::pointer *res_p) {
    HRESULT status;
    if(has_frame) {
      status = dup->ReleaseFrame();

      switch(status) {
        case S_OK:
          has_frame = false;
          break;
        case DXGI_ERROR_WAIT_TIMEOUT:
          return capture_e::timeout;
        case WAIT_ABANDONED:
        case DXGI_ERROR_ACCESS_DENIED:
          return capture_e::reinit;
        default:
          std::cout << "Error: Couldn't release frame [0x"sv << util::hex(status).to_string_view() << std::endl;
          return capture_e::error;
      }
    }

    status = dup->AcquireNextFrame(1000, &frame_info, res_p);

    switch(status) {
      case S_OK:
        has_frame = true;
        return capture_e::ok;
      case DXGI_ERROR_WAIT_TIMEOUT:
        return capture_e::timeout;
      case WAIT_ABANDONED:
      case DXGI_ERROR_ACCESS_DENIED:
        return capture_e::reinit;
      default:
        std::cout << "Error: Couldn't acquire next frame [0x"sv << util::hex(status).to_string_view() << std::endl;
        return capture_e::error;
    }
  }

  capture_e reset(dup_t::pointer dup_p = dup_t::pointer()) {
    auto capture_status = capture_e::ok;
    if(has_frame) {
      capture_status = release_frame();
    }

    dup.reset(dup_p);

    return capture_status;
  }

  capture_e release_frame() {
    auto status = dup->ReleaseFrame();
    switch (status) {
      case S_OK:
        return capture_e::ok;
      case DXGI_ERROR_WAIT_TIMEOUT:
        return capture_e::timeout;
      case WAIT_ABANDONED:
      case DXGI_ERROR_ACCESS_DENIED:
        return capture_e::reinit;
      default:
        std::cout << "Error: Couldn't release frame [0x"sv << util::hex(status).to_string_view() << std::endl;
        return capture_e::error;
    }
  }

  ~duplication_t() {
    if(has_frame) {
      release_frame();
    }
  }
};

class display_t;
struct img_t : public ::platf::img_t  {
  std::shared_ptr<display_t> owner; //Ensure texture remains valid while img is still needed

  texture2d_t texture;
  D3D11_MAPPED_SUBRESOURCE map {};

  ~img_t() override;
};

struct cursor_t {
  int x, y;
  int width, height;
  int pitch;

  std::vector<std::uint8_t> img_data;
  bool visible;

};

class display_t : public ::platf::display_t, public std::enable_shared_from_this<display_t> {
public:
  capture_e snapshot(std::unique_ptr<::platf::img_t> &img_base, bool cursor_visible) override {
    auto img = (img_t *) img_base.get();
    HRESULT status;

    DXGI_OUTDUPL_FRAME_INFO frame_info;

    resource_t::pointer res_p {};
    auto capture_status = dup.next_frame(frame_info, &res_p);
    resource_t res{res_p};

    if (capture_status != capture_e::ok) {
      return capture_status;
    }

    if (frame_info.PointerShapeBufferSize > 0) {
      cursor.img_data.resize(frame_info.PointerShapeBufferSize);
      DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info;

      auto &img_data = cursor.img_data;

      UINT dummy;
      status = dup.dup->GetFramePointerShape(img_data.size(), img_data.data(), &dummy, &shape_info);
      if (FAILED(status)) {
        std::cout << "Error: Failed to get new pointer shape [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;

        return capture_e::error;
      }

      if (shape_info.Type != DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
        std::cout << "Warning: Unsupported cursor format ["sv << shape_info.Type << ']' << std::endl;
      }

      cursor.width = shape_info.Width;
      cursor.height = shape_info.Height;
      cursor.pitch = shape_info.Pitch;
    }

    if (frame_info.LastPresentTime.QuadPart == 0) {
      return capture_e::timeout;
    }

    {
      texture2d_t::pointer src_p {};
      status = res->QueryInterface(IID_ID3D11Texture2D, (void **)&src_p);
      texture2d_t src{src_p};

      if (FAILED(status)) {
        std::cout << "Error: Couldn't query interface [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
        return capture_e::error;
      }

      //Copy from GPU to CPU
      device_ctx->CopyResource((ID3D11Resource *) img->texture.get(), (ID3D11Resource *) src.get());
    }

    cursor.x = frame_info.PointerPosition.Position.x;
    cursor.y = frame_info.PointerPosition.Position.y;
    cursor.visible = frame_info.PointerPosition.Visible;

    status = device_ctx->Map((ID3D11Resource *) img->texture.get(), 0, D3D11_MAP_READ, 0, &img->map);
    if (FAILED(status)) {
      if (status == DXGI_ERROR_WAS_STILL_DRAWING) {
        return capture_e::timeout;
      }

      std::cout << "Error: Failed to map texture [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;

      return capture_e::error;
    }

    img->data = (std::uint8_t *) img->map.pData;
    img->width = width;
    img->height = height;

    return capture_e::ok;
  }

  /*
   * Called when access is lost. Dup must be reinitialized
   */
  int reinit() override {
    HRESULT status;

    dup.reset();
    //TODO: Use IDXGIOutput5 for improved performance
    {
      dxgi::output1_t::pointer output1_p {};
      status = output->QueryInterface(IID_IDXGIOutput1, (void**)&output1_p);
      dxgi::output1_t output1 {output1_p };

      if(FAILED(status)) {
        std::cout << "Error: Failed to query IDXGIOutput1 from the output"sv << std::endl;
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
        std::cout << "Error: DuplicateOutput Failed [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
        return -1;
      }
    }

    DXGI_OUTDUPL_DESC dup_desc;
    dup.dup->GetDesc(&dup_desc);

    format = dup_desc.ModeDesc.Format;

    std::cout << "Source format ["sv << format_str[dup_desc.ModeDesc.Format] << ']' << std::endl;

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
    status = device->CreateTexture2D(&t, nullptr, &tex_p);
    dxgi::texture2d_t tex { tex_p };

    if(FAILED(status)) {
      std::cout << "Error: Failed to create texture [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
      return -1;
    }

    // map the texture simply to get the pitch and stride
    D3D11_MAPPED_SUBRESOURCE mapping;

    status = device_ctx->Map((ID3D11Resource *)tex.get(), 0, D3D11_MAP_READ, 0, &mapping);
    if(FAILED(status)) {
      std::cout << "Error: Failed to map the texture [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
      return -1;
    }

    pitch = (int)mapping.RowPitch;
    stride = (int)mapping.RowPitch / 4;

    device_ctx->Unmap((ID3D11Resource *)tex.get(), 0);

    return 0;
  }

  std::unique_ptr<::platf::img_t> alloc_img() override {
    auto img = std::make_unique<img_t>();

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
    HRESULT status = device->CreateTexture2D(&t, nullptr, &tex_p);
    dxgi::texture2d_t tex { tex_p };

    if(FAILED(status)) {
      std::cout << "Error: Failed to create texture [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
      return nullptr;
    }

    img->texture = std::move(tex);
    img->owner = shared_from_this();
    return img;
  }

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
      std::cout << "Error: Failed to create DXGIFactory1 [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
      return -1;
    }


    for(int x = 0; factory_p->EnumAdapters1(x, &adapter_p) != DXGI_ERROR_NOT_FOUND; ++x) {
      dxgi::adapter_t adapter_tmp { adapter_p };

      for(int y = 0; adapter_tmp->EnumOutputs(y, &output_p) != DXGI_ERROR_NOT_FOUND; ++y) {
        dxgi::output_t output_tmp {output_p };

        DXGI_OUTPUT_DESC desc;
        output_tmp->GetDesc(&desc);

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
      std::cout << "Error: Failed to locate an output device"sv << std::endl;
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
      std::cout << "Error: Failed to query IDXGIAdapter interface"sv <<std::endl;

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
      std::cout << "Error: Failed to create D3D11 device [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;

      return -1;
    }

    DXGI_ADAPTER_DESC adapter_desc;
    adapter->GetDesc(&adapter_desc);
    std::cout
      << "Device Description : "sv << adapter_desc.Description << std::endl
      << "Device Vendor ID   : 0x"sv << util::hex(adapter_desc.VendorId).to_string_view() << std::endl
      << "Device Device ID   : 0x"sv << util::hex(adapter_desc.DeviceId).to_string_view() << std::endl
      << "Device Video Mem   : "sv << adapter_desc.DedicatedVideoMemory / 1048576 << " MiB"sv << std::endl
      << "Device Sys Mem     : "sv << adapter_desc.DedicatedSystemMemory / 1048576 << " MiB"sv << std::endl
      << "Share Sys Mem      : "sv << adapter_desc.SharedSystemMemory / 1048576 << " MiB"sv << std::endl
      << "Feature Level      : 0x"sv << util::hex(feature_level).to_string_view() << std::endl
      << "Capture size       : "sv << width << 'x'  << height << std::endl;

    // Bump up thread priority
    {
      dxgi::dxgi_t::pointer dxgi_p {};
      status = device->QueryInterface(IID_IDXGIDevice, (void**)&dxgi_p);
      dxgi::dxgi_t dxgi { dxgi_p };

      if(FAILED(status)) {
        std::cout << "Error: Failed to query DXGI interface from device [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
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
        std::cout << "Error: Failed to query DXGI interface from device [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
        return -1;
      }

      dxgi->SetMaximumFrameLatency(1);
    }

    return reinit();
  }

  int deinit() {
    dup.reset();
    device_ctx.reset();
    device.reset();
    output.reset();
    adapter.reset();
    factory.reset();

    return 0;
  }

  factory1_t factory;
  adapter_t adapter;
  output_t output;
  device_t device;
  device_ctx_t device_ctx;
  duplication_t dup;
  cursor_t cursor;

  int width, height;
  int pitch, stride;

  DXGI_FORMAT format;
  D3D_FEATURE_LEVEL feature_level;
};

img_t::~img_t() {
  if(map.pData) {
    owner->device_ctx->Unmap((ID3D11Resource *)texture.get(), 0);
    map.pData = nullptr;
  }
}
}

class dummy_mic_t : public mic_t {
public:
  std::vector<std::int16_t> sample(std::size_t sample_size) override {
    std::vector<std::int16_t> sample_buf;
    sample_buf.resize(sample_size);

    return sample_buf;
  }
};

std::unique_ptr<mic_t> microphone() {
  return std::unique_ptr<mic_t> { new dummy_mic_t {} };
}

//std::once_flag windows_cpp_once_flag;
std::shared_ptr<display_t> display() {
  auto disp = std::make_unique<dxgi::display_t>();

  if(disp->init()) {
    return nullptr;
  }

  return disp;
}

input_t input() {
  return nullptr;
}

void move_mouse(input_t &input, int deltaX, int deltaY) {}
void button_mouse(input_t &input, int button, bool release) {}
void scroll(input_t &input, int distance) {}
void keyboard(input_t &input, uint16_t modcode, bool release) {}

namespace gp {
void dpad_y(input_t &input, int button_state) {} // up pressed == -1, down pressed == 1, else 0
void dpad_x(input_t &input, int button_state) {} // left pressed == -1, right pressed == 1, else 0
void start(input_t &input, int button_down) {}
void back(input_t &input, int button_down) {}
void left_stick(input_t &input, int button_down) {}
void right_stick(input_t &input, int button_down) {}
void left_button(input_t &input, int button_down) {}
void right_button(input_t &input, int button_down) {}
void home(input_t &input, int button_down) {}
void a(input_t &input, int button_down) {}
void b(input_t &input, int button_down) {}
void x(input_t &input, int button_down) {}
void y(input_t &input, int button_down) {}
void left_trigger(input_t &input, std::uint8_t abs_z) {}
void right_trigger(input_t &input, std::uint8_t abs_z) {}
void left_stick_x(input_t &input, std::int16_t x) {}
void left_stick_y(input_t &input, std::int16_t y) {}
void right_stick_x(input_t &input, std::int16_t x) {}
void right_stick_y(input_t &input, std::int16_t y) {}
void sync(input_t &input) {}
}

void freeInput(void*) {}

namespace dxgi {
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
}
