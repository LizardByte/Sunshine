#include <cmath>

#include <codecvt>

#include <d3dcompiler.h>
#include <directxmath.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_d3d11va.h>
}

#include "display.h"
#include "src/main.h"
#include "src/video.h"


#define SUNSHINE_SHADERS_DIR SUNSHINE_ASSETS_DIR "/shaders/directx"
namespace platf {
using namespace std::literals;
}

static void free_frame(AVFrame *frame) {
  av_frame_free(&frame);
}

using frame_t = util::safe_ptr<AVFrame, free_frame>;

namespace platf::dxgi {

template<class T>
buf_t make_buffer(device_t::pointer device, const T &t) {
  static_assert(sizeof(T) % 16 == 0, "Buffer needs to be aligned on a 16-byte alignment");

  D3D11_BUFFER_DESC buffer_desc {
    sizeof(T),
    D3D11_USAGE_IMMUTABLE,
    D3D11_BIND_CONSTANT_BUFFER
  };

  D3D11_SUBRESOURCE_DATA init_data {
    &t
  };

  buf_t::pointer buf_p;
  auto status = device->CreateBuffer(&buffer_desc, &init_data, &buf_p);
  if(status) {
    BOOST_LOG(error) << "Failed to create buffer: [0x"sv << util::hex(status).to_string_view() << ']';
    return nullptr;
  }

  return buf_t { buf_p };
}

blend_t make_blend(device_t::pointer device, bool enable, bool invert) {
  D3D11_BLEND_DESC bdesc {};
  auto &rt                 = bdesc.RenderTarget[0];
  rt.BlendEnable           = enable;
  rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

  if(enable) {
    rt.BlendOp      = D3D11_BLEND_OP_ADD;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;

    if(invert) {
      // Invert colors
      rt.SrcBlend  = D3D11_BLEND_INV_DEST_COLOR;
      rt.DestBlend = D3D11_BLEND_INV_SRC_COLOR;
    }
    else {
      // Regular alpha blending
      rt.SrcBlend  = D3D11_BLEND_SRC_ALPHA;
      rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    }

    rt.SrcBlendAlpha  = D3D11_BLEND_ZERO;
    rt.DestBlendAlpha = D3D11_BLEND_ZERO;
  }

  blend_t blend;
  auto status = device->CreateBlendState(&bdesc, &blend);
  if(status) {
    BOOST_LOG(error) << "Failed to create blend state: [0x"sv << util::hex(status).to_string_view() << ']';
    return nullptr;
  }

  return blend;
}

blob_t convert_UV_vs_hlsl;
blob_t convert_UV_ps_hlsl;
blob_t convert_UV_PQ_ps_hlsl;
blob_t scene_vs_hlsl;
blob_t convert_Y_ps_hlsl;
blob_t convert_Y_PQ_ps_hlsl;
blob_t scene_ps_hlsl;
blob_t scene_NW_ps_hlsl;

struct img_d3d_t : public platf::img_t {
  std::shared_ptr<platf::display_t> display;

  // These objects are owned by the display_t's ID3D11Device
  texture2d_t capture_texture;
  render_target_t capture_rt;
  keyed_mutex_t capture_mutex;

  // This is the shared handle used by hwdevice_t to open capture_texture
  HANDLE encoder_texture_handle = {};

  // Set to true if the image corresponds to a dummy texture used prior to
  // the first successful capture of a desktop frame
  bool dummy = false;

  // Unique identifier for this image
  uint32_t id = 0;

  virtual ~img_d3d_t() override {
    if(encoder_texture_handle) {
      CloseHandle(encoder_texture_handle);
    }
  };
};

util::buffer_t<std::uint8_t> make_cursor_xor_image(const util::buffer_t<std::uint8_t> &img_data, DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info) {
  constexpr std::uint32_t inverted    = 0xFFFFFFFF;
  constexpr std::uint32_t transparent = 0;

  switch(shape_info.Type) {
  case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
    // This type doesn't require any XOR-blending
    return {};
  case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR: {
    util::buffer_t<std::uint8_t> cursor_img = img_data;
    std::for_each((std::uint32_t *)std::begin(cursor_img), (std::uint32_t *)std::end(cursor_img), [](auto &pixel) {
      auto alpha = (std::uint8_t)((pixel >> 24) & 0xFF);
      if(alpha == 0xFF) {
        // Pixels with 0xFF alpha will be XOR-blended as is.
      }
      else if(alpha == 0x00) {
        // Pixels with 0x00 alpha will be blended by make_cursor_alpha_image().
        // We make them transparent for the XOR-blended cursor image.
        pixel = transparent;
      }
      else {
        // Other alpha values are illegal in masked color cursors
        BOOST_LOG(warning) << "Illegal alpha value in masked color cursor: " << alpha;
      }
    });
    return cursor_img;
  }
  case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME:
    // Monochrome is handled below
    break;
  default:
    BOOST_LOG(error) << "Invalid cursor shape type: " << shape_info.Type;
    return {};
  }

  shape_info.Height /= 2;

  util::buffer_t<std::uint8_t> cursor_img { shape_info.Width * shape_info.Height * 4 };

  auto bytes       = shape_info.Pitch * shape_info.Height;
  auto pixel_begin = (std::uint32_t *)std::begin(cursor_img);
  auto pixel_data  = pixel_begin;
  auto and_mask    = std::begin(img_data);
  auto xor_mask    = std::begin(img_data) + bytes;

  for(auto x = 0; x < bytes; ++x) {
    for(auto c = 7; c >= 0; --c) {
      auto bit        = 1 << c;
      auto color_type = ((*and_mask & bit) ? 1 : 0) + ((*xor_mask & bit) ? 2 : 0);

      switch(color_type) {
      case 0: // Opaque black (handled by alpha-blending)
      case 2: // Opaque white (handled by alpha-blending)
      case 1: // Color of screen (transparent)
        *pixel_data = transparent;
        break;
      case 3: // Inverse of screen
        *pixel_data = inverted;
        break;
      }

      ++pixel_data;
    }
    ++and_mask;
    ++xor_mask;
  }

  return cursor_img;
}

util::buffer_t<std::uint8_t> make_cursor_alpha_image(const util::buffer_t<std::uint8_t> &img_data, DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info) {
  constexpr std::uint32_t black       = 0xFF000000;
  constexpr std::uint32_t white       = 0xFFFFFFFF;
  constexpr std::uint32_t transparent = 0;

  switch(shape_info.Type) {
  case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR: {
    util::buffer_t<std::uint8_t> cursor_img = img_data;
    std::for_each((std::uint32_t *)std::begin(cursor_img), (std::uint32_t *)std::end(cursor_img), [](auto &pixel) {
      auto alpha = (std::uint8_t)((pixel >> 24) & 0xFF);
      if(alpha == 0xFF) {
        // Pixels with 0xFF alpha will be XOR-blended by make_cursor_xor_image().
        // We make them transparent for the alpha-blended cursor image.
        pixel = transparent;
      }
      else if(alpha == 0x00) {
        // Pixels with 0x00 alpha will be blended as opaque with the alpha-blended image.
        pixel |= 0xFF000000;
      }
      else {
        // Other alpha values are illegal in masked color cursors
        BOOST_LOG(warning) << "Illegal alpha value in masked color cursor: " << alpha;
      }
    });
    return cursor_img;
  }
  case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
    // Color cursors are just an ARGB bitmap which requires no processing.
    return img_data;
  case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME:
    // Monochrome cursors are handled below.
    break;
  default:
    BOOST_LOG(error) << "Invalid cursor shape type: " << shape_info.Type;
    return {};
  }

  shape_info.Height /= 2;

  util::buffer_t<std::uint8_t> cursor_img { shape_info.Width * shape_info.Height * 4 };

  auto bytes       = shape_info.Pitch * shape_info.Height;
  auto pixel_begin = (std::uint32_t *)std::begin(cursor_img);
  auto pixel_data  = pixel_begin;
  auto and_mask    = std::begin(img_data);
  auto xor_mask    = std::begin(img_data) + bytes;

  for(auto x = 0; x < bytes; ++x) {
    for(auto c = 7; c >= 0; --c) {
      auto bit        = 1 << c;
      auto color_type = ((*and_mask & bit) ? 1 : 0) + ((*xor_mask & bit) ? 2 : 0);

      switch(color_type) {
      case 0: // Opaque black
        *pixel_data = black;
        break;
      case 2: // Opaque white
        *pixel_data = white;
        break;
      case 3: // Inverse of screen (handled by XOR blending)
      case 1: // Color of screen (transparent)
        *pixel_data = transparent;
        break;
      }

      ++pixel_data;
    }
    ++and_mask;
    ++xor_mask;
  }

  return cursor_img;
}

blob_t compile_shader(LPCSTR file, LPCSTR entrypoint, LPCSTR shader_model) {
  blob_t::pointer msg_p = nullptr;
  blob_t::pointer compiled_p;

  DWORD flags = D3DCOMPILE_ENABLE_STRICTNESS;

#ifndef NDEBUG
  flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;

  auto wFile  = converter.from_bytes(file);
  auto status = D3DCompileFromFile(wFile.c_str(), nullptr, nullptr, entrypoint, shader_model, flags, 0, &compiled_p, &msg_p);

  if(msg_p) {
    BOOST_LOG(warning) << std::string_view { (const char *)msg_p->GetBufferPointer(), msg_p->GetBufferSize() - 1 };
    msg_p->Release();
  }

  if(status) {
    BOOST_LOG(error) << "Couldn't compile ["sv << file << "] [0x"sv << util::hex(status).to_string_view() << ']';
    return nullptr;
  }

  return blob_t { compiled_p };
}

blob_t compile_pixel_shader(LPCSTR file) {
  return compile_shader(file, "main_ps", "ps_5_0");
}

blob_t compile_vertex_shader(LPCSTR file) {
  return compile_shader(file, "main_vs", "vs_5_0");
}

class hwdevice_t : public platf::hwdevice_t {
public:
  int convert(platf::img_t &img_base) override {
    auto &img     = (img_d3d_t &)img_base;
    auto &img_ctx = img_ctx_map[img.id];

    // Open the shared capture texture with our ID3D11Device
    if(initialize_image_context(img, img_ctx)) {
      return -1;
    }

    // Acquire encoder mutex to synchronize with capture code
    auto status = img_ctx.encoder_mutex->AcquireSync(0, INFINITE);
    if(status != S_OK) {
      BOOST_LOG(error) << "Failed to acquire encoder mutex [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    device_ctx->OMSetRenderTargets(1, &nv12_Y_rt, nullptr);
    device_ctx->VSSetShader(scene_vs.get(), nullptr, 0);
    device_ctx->PSSetShader(convert_Y_ps.get(), nullptr, 0);
    device_ctx->RSSetViewports(1, &outY_view);
    device_ctx->PSSetShaderResources(0, 1, &img_ctx.encoder_input_res);
    device_ctx->Draw(3, 0);

    // Artifacts start appearing on the rendered image if Sunshine doesn't flush
    // before rendering on the UV part of the image.
    device_ctx->Flush();

    device_ctx->OMSetRenderTargets(1, &nv12_UV_rt, nullptr);
    device_ctx->VSSetShader(convert_UV_vs.get(), nullptr, 0);
    device_ctx->PSSetShader(convert_UV_ps.get(), nullptr, 0);
    device_ctx->RSSetViewports(1, &outUV_view);
    device_ctx->Draw(3, 0);

    // Release encoder mutex to allow capture code to reuse this image
    img_ctx.encoder_mutex->ReleaseSync(0);

    return 0;
  }

  void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) override {
    switch(colorspace) {
    case 5: // SWS_CS_SMPTE170M
      color_p = &::video::colors[0];
      break;
    case 1: // SWS_CS_ITU709
      color_p = &::video::colors[2];
      break;
    case 9: // SWS_CS_BT2020
      color_p = &::video::colors[4];
      break;
    default:
      BOOST_LOG(warning) << "Colorspace: ["sv << colorspace << "] not yet supported: switching to default"sv;
      color_p = &::video::colors[0];
    };

    if(color_range > 1) {
      // Full range
      ++color_p;
    }

    auto color_matrix = make_buffer((device_t::pointer)data, *color_p);
    if(!color_matrix) {
      BOOST_LOG(warning) << "Failed to create color matrix"sv;
      return;
    }

    device_ctx->VSSetConstantBuffers(0, 1, &info_scene);
    device_ctx->PSSetConstantBuffers(0, 1, &color_matrix);
    this->color_matrix = std::move(color_matrix);
  }

  void init_hwframes(AVHWFramesContext *frames) override {
    // We may be called with a QSV or D3D11VA context
    if(frames->device_ctx->type == AV_HWDEVICE_TYPE_D3D11VA) {
      auto d3d11_frames = (AVD3D11VAFramesContext *)frames->hwctx;

      // The encoder requires textures with D3D11_BIND_RENDER_TARGET set
      d3d11_frames->BindFlags = D3D11_BIND_RENDER_TARGET;
      d3d11_frames->MiscFlags = 0;
    }

    // We require a single texture
    frames->initial_pool_size = 1;
  }

  int prepare_to_derive_context(int hw_device_type) override {
    // QuickSync requires our device to be multithread-protected
    if(hw_device_type == AV_HWDEVICE_TYPE_QSV) {
      multithread_t mt;

      auto status = device->QueryInterface(IID_ID3D11Multithread, (void **)&mt);
      if(FAILED(status)) {
        BOOST_LOG(warning) << "Failed to query ID3D11Multithread interface from device [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }

      mt->SetMultithreadProtected(TRUE);
    }

    return 0;
  }

  int set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) override {
    this->hwframe.reset(frame);
    this->frame = frame;

    // Populate this frame with a hardware buffer if one isn't there already
    if(!frame->buf[0]) {
      auto err = av_hwframe_get_buffer(hw_frames_ctx, frame, 0);
      if(err) {
        char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
        BOOST_LOG(error) << "Failed to get hwframe buffer: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, err);
        return -1;
      }
    }

    // If this is a frame from a derived context, we'll need to map it to D3D11
    ID3D11Texture2D *frame_texture;
    if(frame->format != AV_PIX_FMT_D3D11) {
      frame_t d3d11_frame { av_frame_alloc() };

      d3d11_frame->format = AV_PIX_FMT_D3D11;

      auto err = av_hwframe_map(d3d11_frame.get(), frame, AV_HWFRAME_MAP_WRITE | AV_HWFRAME_MAP_OVERWRITE);
      if(err) {
        char err_str[AV_ERROR_MAX_STRING_SIZE] { 0 };
        BOOST_LOG(error) << "Failed to map D3D11 frame: "sv << av_make_error_string(err_str, AV_ERROR_MAX_STRING_SIZE, err);
        return -1;
      }

      // Get the texture from the mapped frame
      frame_texture = (ID3D11Texture2D *)d3d11_frame->data[0];
    }
    else {
      // Otherwise, we can just use the texture inside the original frame
      frame_texture = (ID3D11Texture2D *)frame->data[0];
    }

    auto out_width  = frame->width;
    auto out_height = frame->height;

    float in_width  = display->width;
    float in_height = display->height;

    // Ensure aspect ratio is maintained
    auto scalar       = std::fminf(out_width / in_width, out_height / in_height);
    auto out_width_f  = in_width * scalar;
    auto out_height_f = in_height * scalar;

    // result is always positive
    auto offsetX = (out_width - out_width_f) / 2;
    auto offsetY = (out_height - out_height_f) / 2;

    outY_view  = D3D11_VIEWPORT { offsetX, offsetY, out_width_f, out_height_f, 0.0f, 1.0f };
    outUV_view = D3D11_VIEWPORT { offsetX / 2, offsetY / 2, out_width_f / 2, out_height_f / 2, 0.0f, 1.0f };

    // The underlying frame pool owns the texture, so we must reference it for ourselves
    frame_texture->AddRef();
    hwframe_texture.reset(frame_texture);

    float info_in[16 / sizeof(float)] { 1.0f / (float)out_width_f }; //aligned to 16-byte
    info_scene = make_buffer(device.get(), info_in);

    if(!info_scene) {
      BOOST_LOG(error) << "Failed to create info scene buffer"sv;
      return -1;
    }

    D3D11_RENDER_TARGET_VIEW_DESC nv12_rt_desc {
      format == DXGI_FORMAT_P010 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM,
      D3D11_RTV_DIMENSION_TEXTURE2D
    };

    auto status = device->CreateRenderTargetView(hwframe_texture.get(), &nv12_rt_desc, &nv12_Y_rt);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create render target view [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    nv12_rt_desc.Format = (format == DXGI_FORMAT_P010) ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;

    status = device->CreateRenderTargetView(hwframe_texture.get(), &nv12_rt_desc, &nv12_UV_rt);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create render target view [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    // Clear the RTVs to ensure the aspect ratio padding is black
    const float y_black[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    device_ctx->ClearRenderTargetView(nv12_Y_rt.get(), y_black);
    const float uv_black[] = { 0.5f, 0.5f, 0.5f, 0.5f };
    device_ctx->ClearRenderTargetView(nv12_UV_rt.get(), uv_black);

    return 0;
  }

  int init(
    std::shared_ptr<platf::display_t> display, adapter_t::pointer adapter_p,
    pix_fmt_e pix_fmt) {

    D3D_FEATURE_LEVEL featureLevels[] {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,
      D3D_FEATURE_LEVEL_9_2,
      D3D_FEATURE_LEVEL_9_1
    };

    HRESULT status = D3D11CreateDevice(
      adapter_p,
      D3D_DRIVER_TYPE_UNKNOWN,
      nullptr,
      D3D11_CREATE_DEVICE_FLAGS,
      featureLevels, sizeof(featureLevels) / sizeof(D3D_FEATURE_LEVEL),
      D3D11_SDK_VERSION,
      &device,
      nullptr,
      &device_ctx);

    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create encoder D3D11 device [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    dxgi::dxgi_t dxgi;
    status = device->QueryInterface(IID_IDXGIDevice, (void **)&dxgi);
    if(FAILED(status)) {
      BOOST_LOG(warning) << "Failed to query DXGI interface from device [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    status = dxgi->SetGPUThreadPriority(7);
    if(FAILED(status)) {
      BOOST_LOG(warning) << "Failed to increase encoding GPU thread priority. Please run application as administrator for optimal performance.";
    }

    data = device.get();

    format = (pix_fmt == pix_fmt_e::nv12 ? DXGI_FORMAT_NV12 : DXGI_FORMAT_P010);
    status = device->CreateVertexShader(scene_vs_hlsl->GetBufferPointer(), scene_vs_hlsl->GetBufferSize(), nullptr, &scene_vs);
    if(status) {
      BOOST_LOG(error) << "Failed to create scene vertex shader [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    status = device->CreateVertexShader(convert_UV_vs_hlsl->GetBufferPointer(), convert_UV_vs_hlsl->GetBufferSize(), nullptr, &convert_UV_vs);
    if(status) {
      BOOST_LOG(error) << "Failed to create convertUV vertex shader [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    // If the display is in HDR and we're streaming HDR, we'll be converting scRGB to SMPTE 2084 PQ.
    // NB: We can consume scRGB in SDR with our regular shaders because it behaves like UNORM input.
    if(format == DXGI_FORMAT_P010 && display->is_hdr()) {
      status = device->CreatePixelShader(convert_Y_PQ_ps_hlsl->GetBufferPointer(), convert_Y_PQ_ps_hlsl->GetBufferSize(), nullptr, &convert_Y_ps);
      if(status) {
        BOOST_LOG(error) << "Failed to create convertY pixel shader [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }

      status = device->CreatePixelShader(convert_UV_PQ_ps_hlsl->GetBufferPointer(), convert_UV_PQ_ps_hlsl->GetBufferSize(), nullptr, &convert_UV_ps);
      if(status) {
        BOOST_LOG(error) << "Failed to create convertUV pixel shader [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }
    }
    else {
      status = device->CreatePixelShader(convert_Y_ps_hlsl->GetBufferPointer(), convert_Y_ps_hlsl->GetBufferSize(), nullptr, &convert_Y_ps);
      if(status) {
        BOOST_LOG(error) << "Failed to create convertY pixel shader [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }

      status = device->CreatePixelShader(convert_UV_ps_hlsl->GetBufferPointer(), convert_UV_ps_hlsl->GetBufferSize(), nullptr, &convert_UV_ps);
      if(status) {
        BOOST_LOG(error) << "Failed to create convertUV pixel shader [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }
    }

    color_matrix = make_buffer(device.get(), ::video::colors[0]);
    if(!color_matrix) {
      BOOST_LOG(error) << "Failed to create color matrix buffer"sv;
      return -1;
    }

    D3D11_INPUT_ELEMENT_DESC layout_desc {
      "SV_Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0
    };

    status = device->CreateInputLayout(
      &layout_desc, 1,
      convert_UV_vs_hlsl->GetBufferPointer(), convert_UV_vs_hlsl->GetBufferSize(),
      &input_layout);

    this->display = std::move(display);

    blend_disable = make_blend(device.get(), false, false);
    if(!blend_disable) {
      return -1;
    }

    D3D11_SAMPLER_DESC sampler_desc {};
    sampler_desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler_desc.MinLOD         = 0;
    sampler_desc.MaxLOD         = D3D11_FLOAT32_MAX;

    status = device->CreateSamplerState(&sampler_desc, &sampler_linear);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create point sampler state [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    device_ctx->IASetInputLayout(input_layout.get());
    device_ctx->PSSetConstantBuffers(0, 1, &color_matrix);
    device_ctx->VSSetConstantBuffers(0, 1, &info_scene);

    device_ctx->OMSetBlendState(blend_disable.get(), nullptr, 0xFFFFFFFFu);
    device_ctx->PSSetSamplers(0, 1, &sampler_linear);
    device_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    return 0;
  }

private:
  struct encoder_img_ctx_t {
    // Used to determine if the underlying texture changes.
    // Not safe for actual use by the encoder!
    texture2d_t::pointer capture_texture_p;

    texture2d_t encoder_texture;
    shader_res_t encoder_input_res;
    keyed_mutex_t encoder_mutex;

    void reset() {
      capture_texture_p = nullptr;
      encoder_texture.reset();
      encoder_input_res.reset();
      encoder_mutex.reset();
    }
  };

  int initialize_image_context(const img_d3d_t &img, encoder_img_ctx_t &img_ctx) {
    // If we've already opened the shared texture, we're done
    if(img_ctx.encoder_texture && img.capture_texture.get() == img_ctx.capture_texture_p) {
      return 0;
    }

    // Reset this image context in case it was used before with a different texture.
    // Textures can change when transitioning from a dummy image to a real image.
    img_ctx.reset();

    device1_t device1;
    auto status = device->QueryInterface(__uuidof(ID3D11Device1), (void **)&device1);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to query ID3D11Device1 [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    // Open a handle to the shared texture
    status = device1->OpenSharedResource1(img.encoder_texture_handle, __uuidof(ID3D11Texture2D), (void **)&img_ctx.encoder_texture);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to open shared image texture [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    // Get the keyed mutex to synchronize with the capture code
    status = img_ctx.encoder_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **)&img_ctx.encoder_mutex);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to query IDXGIKeyedMutex [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    // Create the SRV for the encoder texture
    status = device->CreateShaderResourceView(img_ctx.encoder_texture.get(), nullptr, &img_ctx.encoder_input_res);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create shader resource view for encoding [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    img_ctx.capture_texture_p = img.capture_texture.get();
    return 0;
  }

public:
  frame_t hwframe;

  ::video::color_t *color_p;

  buf_t info_scene;
  buf_t color_matrix;

  input_layout_t input_layout;

  blend_t blend_disable;
  sampler_state_t sampler_linear;

  render_target_t nv12_Y_rt;
  render_target_t nv12_UV_rt;

  // The image referenced by hwframe
  texture2d_t hwframe_texture;

  // d3d_img_t::id -> encoder_img_ctx_t
  // These store the encoder textures for each img_t that passes through
  // convert(). We can't store them in the img_t itself because it is shared
  // amongst multiple hwdevice_t objects (and therefore multiple ID3D11Devices).
  std::map<uint32_t, encoder_img_ctx_t> img_ctx_map;

  std::shared_ptr<platf::display_t> display;

  vs_t convert_UV_vs;
  ps_t convert_UV_ps;
  ps_t convert_Y_ps;
  vs_t scene_vs;

  D3D11_VIEWPORT outY_view;
  D3D11_VIEWPORT outUV_view;

  DXGI_FORMAT format;

  device_t device;
  device_ctx_t device_ctx;
};

bool set_cursor_texture(device_t::pointer device, gpu_cursor_t &cursor, util::buffer_t<std::uint8_t> &&cursor_img, DXGI_OUTDUPL_POINTER_SHAPE_INFO &shape_info) {
  // This cursor image may not be used
  if(cursor_img.size() == 0) {
    cursor.input_res.reset();
    cursor.set_texture(0, 0, nullptr);
    return true;
  }

  D3D11_SUBRESOURCE_DATA data {
    std::begin(cursor_img),
    4 * shape_info.Width,
    0
  };

  // Create texture for cursor
  D3D11_TEXTURE2D_DESC t {};
  t.Width            = shape_info.Width;
  t.Height           = cursor_img.size() / data.SysMemPitch;
  t.MipLevels        = 1;
  t.ArraySize        = 1;
  t.SampleDesc.Count = 1;
  t.Usage            = D3D11_USAGE_IMMUTABLE;
  t.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
  t.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

  texture2d_t texture;
  auto status = device->CreateTexture2D(&t, &data, &texture);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create mouse texture [0x"sv << util::hex(status).to_string_view() << ']';
    return false;
  }

  // Free resources before allocating on the next line.
  cursor.input_res.reset();
  status = device->CreateShaderResourceView(texture.get(), nullptr, &cursor.input_res);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create cursor shader resource view [0x"sv << util::hex(status).to_string_view() << ']';
    return false;
  }

  cursor.set_texture(t.Width, t.Height, std::move(texture));
  return true;
}

capture_e display_vram_t::snapshot(platf::img_t *img_base, std::chrono::milliseconds timeout, bool cursor_visible) {
  auto img = (img_d3d_t *)img_base;

  HRESULT status;

  DXGI_OUTDUPL_FRAME_INFO frame_info;

  resource_t::pointer res_p {};
  auto capture_status = dup.next_frame(frame_info, timeout, &res_p);
  resource_t res { res_p };

  if(capture_status != capture_e::ok) {
    return capture_status;
  }

  const bool mouse_update_flag = frame_info.LastMouseUpdateTime.QuadPart != 0 || frame_info.PointerShapeBufferSize > 0;
  const bool frame_update_flag = frame_info.AccumulatedFrames != 0 || frame_info.LastPresentTime.QuadPart != 0;
  const bool update_flag       = mouse_update_flag || frame_update_flag;

  if(!update_flag) {
    return capture_e::timeout;
  }

  if(frame_info.PointerShapeBufferSize > 0) {
    DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info {};

    util::buffer_t<std::uint8_t> img_data { frame_info.PointerShapeBufferSize };

    UINT dummy;
    status = dup.dup->GetFramePointerShape(img_data.size(), std::begin(img_data), &dummy, &shape_info);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to get new pointer shape [0x"sv << util::hex(status).to_string_view() << ']';

      return capture_e::error;
    }

    auto alpha_cursor_img = make_cursor_alpha_image(img_data, shape_info);
    auto xor_cursor_img   = make_cursor_xor_image(img_data, shape_info);

    if(!set_cursor_texture(device.get(), cursor_alpha, std::move(alpha_cursor_img), shape_info) ||
       !set_cursor_texture(device.get(), cursor_xor, std::move(xor_cursor_img), shape_info)) {
      return capture_e::error;
    }
  }

  if(frame_info.LastMouseUpdateTime.QuadPart) {
    cursor_alpha.set_pos(frame_info.PointerPosition.Position.x, frame_info.PointerPosition.Position.y, frame_info.PointerPosition.Visible);
    cursor_xor.set_pos(frame_info.PointerPosition.Position.x, frame_info.PointerPosition.Position.y, frame_info.PointerPosition.Visible);
  }

  if(frame_update_flag) {
    texture2d_t src {};

    // Get the texture object from this frame
    status = res->QueryInterface(IID_ID3D11Texture2D, (void **)&src);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Couldn't query interface [0x"sv << util::hex(status).to_string_view() << ']';
      return capture_e::error;
    }

    D3D11_TEXTURE2D_DESC desc;
    src->GetDesc(&desc);

    // It's possible for our display enumeration to race with mode changes and result in
    // mismatched image pool and desktop texture sizes. If this happens, just reinit again.
    if(desc.Width != width || desc.Height != height) {
      BOOST_LOG(info) << "Capture size changed ["sv << width << 'x' << height << " -> "sv << desc.Width << 'x' << desc.Height << ']';
      return capture_e::reinit;
    }

    // If we don't know the capture format yet, grab it from this texture
    if(capture_format == DXGI_FORMAT_UNKNOWN) {
      capture_format = desc.Format;
      BOOST_LOG(info) << "Capture format ["sv << dxgi_format_to_string(capture_format) << ']';

      D3D11_TEXTURE2D_DESC t {};
      t.Width            = width;
      t.Height           = height;
      t.MipLevels        = 1;
      t.ArraySize        = 1;
      t.SampleDesc.Count = 1;
      t.Usage            = D3D11_USAGE_DEFAULT;
      t.Format           = capture_format;
      t.BindFlags        = 0;

      // Create a texture to store the most recent copy of the desktop
      status = device->CreateTexture2D(&t, nullptr, &last_frame_copy);
      if(FAILED(status)) {
        BOOST_LOG(error) << "Failed to create frame copy texture [0x"sv << util::hex(status).to_string_view() << ']';
        return capture_e::error;
      }
    }

    // It's also possible for the capture format to change on the fly. If that happens,
    // reinitialize capture to try format detection again and create new images.
    if(capture_format != desc.Format) {
      BOOST_LOG(info) << "Capture format changed ["sv << dxgi_format_to_string(capture_format) << " -> "sv << dxgi_format_to_string(desc.Format) << ']';
      return capture_e::reinit;
    }

    // Now that we know the capture format, we can finish creating the image
    if(complete_img(img, false)) {
      return capture_e::error;
    }

    // Copy the texture to use for cursor-only updates
    device_ctx->CopyResource(last_frame_copy.get(), src.get());

    // Copy into the capture texture on the image with the mutex held
    status = img->capture_mutex->AcquireSync(0, INFINITE);
    if(status != S_OK) {
      BOOST_LOG(error) << "Failed to acquire capture mutex [0x"sv << util::hex(status).to_string_view() << ']';
      return capture_e::error;
    }
    device_ctx->CopyResource(img->capture_texture.get(), src.get());
  }
  else if(capture_format == DXGI_FORMAT_UNKNOWN) {
    // We don't know the final capture format yet, so we will encode a dummy image
    BOOST_LOG(debug) << "Capture format is still unknown. Encoding a blank image"sv;

    // Finish creating the image as a dummy (if it hasn't happened already)
    if(complete_img(img, true)) {
      return capture_e::error;
    }

    auto dummy_data = std::make_unique<std::uint8_t[]>(img->row_pitch * img->height);
    std::fill_n(dummy_data.get(), img->row_pitch * img->height, 0);

    status = img->capture_mutex->AcquireSync(0, INFINITE);
    if(status != S_OK) {
      BOOST_LOG(error) << "Failed to acquire capture mutex [0x"sv << util::hex(status).to_string_view() << ']';
      return capture_e::error;
    }

    // Populate the image with dummy data. This is required because these images could be reused
    // after rendering (in which case they would have a cursor already rendered into them).
    device_ctx->UpdateSubresource(img->capture_texture.get(), 0, nullptr, dummy_data.get(), img->row_pitch, 0);
  }
  else {
    // We must know the capture format in this path or we would have hit the above unknown format case
    if(complete_img(img, false)) {
      return capture_e::error;
    }

    // We have a previously captured frame to reuse. We can't just grab the src texture from
    // the call to AcquireNextFrame() because that won't be valid. It seems to return a texture
    // in the unmodified desktop format (rather than the formats we passed to DuplicateOutput1())
    // if called in that case.
    status = img->capture_mutex->AcquireSync(0, INFINITE);
    if(status != S_OK) {
      BOOST_LOG(error) << "Failed to acquire capture mutex [0x"sv << util::hex(status).to_string_view() << ']';
      return capture_e::error;
    }
    device_ctx->CopyResource(img->capture_texture.get(), last_frame_copy.get());
  }

  if((cursor_alpha.visible || cursor_xor.visible) && cursor_visible) {
    device_ctx->VSSetShader(scene_vs.get(), nullptr, 0);
    device_ctx->PSSetShader(scene_ps.get(), nullptr, 0);
    device_ctx->OMSetRenderTargets(1, &img->capture_rt, nullptr);

    if(cursor_alpha.texture.get()) {
      // Perform an alpha blending operation
      device_ctx->OMSetBlendState(blend_alpha.get(), nullptr, 0xFFFFFFFFu);

      device_ctx->PSSetShaderResources(0, 1, &cursor_alpha.input_res);
      device_ctx->RSSetViewports(1, &cursor_alpha.cursor_view);
      device_ctx->Draw(3, 0);
    }

    if(cursor_xor.texture.get()) {
      // Perform an invert blending without touching alpha values
      device_ctx->OMSetBlendState(blend_invert.get(), nullptr, 0x00FFFFFFu);

      device_ctx->PSSetShaderResources(0, 1, &cursor_xor.input_res);
      device_ctx->RSSetViewports(1, &cursor_xor.cursor_view);
      device_ctx->Draw(3, 0);
    }

    device_ctx->OMSetBlendState(blend_disable.get(), nullptr, 0xFFFFFFFFu);
  }

  // Release the mutex to allow encoding of this frame
  img->capture_mutex->ReleaseSync(0);

  return capture_e::ok;
}

int display_vram_t::init(const ::video::config_t &config, const std::string &display_name) {
  if(display_base_t::init(config, display_name)) {
    return -1;
  }

  D3D11_SAMPLER_DESC sampler_desc {};
  sampler_desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler_desc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler_desc.MinLOD         = 0;
  sampler_desc.MaxLOD         = D3D11_FLOAT32_MAX;

  auto status = device->CreateSamplerState(&sampler_desc, &sampler_linear);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create point sampler state [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  status = device->CreateVertexShader(scene_vs_hlsl->GetBufferPointer(), scene_vs_hlsl->GetBufferSize(), nullptr, &scene_vs);
  if(status) {
    BOOST_LOG(error) << "Failed to create scene vertex shader [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  if(config.dynamicRange && is_hdr()) {
    // This shader will normalize scRGB white levels to a user-defined white level
    status = device->CreatePixelShader(scene_NW_ps_hlsl->GetBufferPointer(), scene_NW_ps_hlsl->GetBufferSize(), nullptr, &scene_ps);
    if(status) {
      BOOST_LOG(error) << "Failed to create scene pixel shader [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    // Use a 300 nit target for the mouse cursor. We should really get
    // the user's SDR white level in nits, but there is no API that
    // provides that information to Win32 apps.
    float sdr_multiplier_data[16 / sizeof(float)] { 300.0f / 80.f }; // aligned to 16-byte
    auto sdr_multiplier = make_buffer(device.get(), sdr_multiplier_data);
    if(!sdr_multiplier) {
      BOOST_LOG(warning) << "Failed to create SDR multiplier"sv;
      return -1;
    }

    device_ctx->PSSetConstantBuffers(0, 1, &sdr_multiplier);
  }
  else {
    status = device->CreatePixelShader(scene_ps_hlsl->GetBufferPointer(), scene_ps_hlsl->GetBufferSize(), nullptr, &scene_ps);
    if(status) {
      BOOST_LOG(error) << "Failed to create scene pixel shader [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
  }

  blend_alpha   = make_blend(device.get(), true, false);
  blend_invert  = make_blend(device.get(), true, true);
  blend_disable = make_blend(device.get(), false, false);

  if(!blend_disable || !blend_alpha || !blend_invert) {
    return -1;
  }

  device_ctx->OMSetBlendState(blend_disable.get(), nullptr, 0xFFFFFFFFu);
  device_ctx->PSSetSamplers(0, 1, &sampler_linear);
  device_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  return 0;
}

std::shared_ptr<platf::img_t> display_vram_t::alloc_img() {
  auto img = std::make_shared<img_d3d_t>();

  // Initialize format-independent fields
  img->width   = width;
  img->height  = height;
  img->display = shared_from_this();
  img->id      = next_image_id++;

  return img;
}

// This cannot use ID3D11DeviceContext because it can be called concurrently by the encoding thread
int display_vram_t::complete_img(platf::img_t *img_base, bool dummy) {
  auto img = (img_d3d_t *)img_base;

  // If this already has a capture texture and it's not switching dummy state, nothing to do
  if(img->capture_texture && img->dummy == dummy) {
    return 0;
  }

  // If this is not a dummy image, we must know the format by now
  if(!dummy && capture_format == DXGI_FORMAT_UNKNOWN) {
    BOOST_LOG(error) << "display_vram_t::complete_img() called with unknown capture format!";
    return -1;
  }

  // Reset the image (in case this was previously a dummy)
  img->capture_texture.reset();
  img->capture_rt.reset();
  img->capture_mutex.reset();
  img->data = nullptr;
  if(img->encoder_texture_handle) {
    CloseHandle(img->encoder_texture_handle);
    img->encoder_texture_handle = NULL;
  }

  // Initialize format-dependent fields
  img->pixel_pitch = get_pixel_pitch();
  img->row_pitch   = img->pixel_pitch * img->width;
  img->dummy       = dummy;

  D3D11_TEXTURE2D_DESC t {};
  t.Width            = img->width;
  t.Height           = img->height;
  t.MipLevels        = 1;
  t.ArraySize        = 1;
  t.SampleDesc.Count = 1;
  t.Usage            = D3D11_USAGE_DEFAULT;
  t.Format           = (capture_format == DXGI_FORMAT_UNKNOWN) ? DXGI_FORMAT_B8G8R8A8_UNORM : capture_format;
  t.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  t.MiscFlags        = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

  auto dummy_data = std::make_unique<std::uint8_t[]>(img->row_pitch * img->height);
  std::fill_n(dummy_data.get(), img->row_pitch * img->height, 0);
  D3D11_SUBRESOURCE_DATA initial_data {
    dummy_data.get(),
    (UINT)img->row_pitch,
    0
  };

  auto status = device->CreateTexture2D(&t, &initial_data, &img->capture_texture);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create img buf texture [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  status = device->CreateRenderTargetView(img->capture_texture.get(), nullptr, &img->capture_rt);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create render target view [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  // Get the keyed mutex to synchronize with the encoding code
  status = img->capture_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **)&img->capture_mutex);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to query IDXGIKeyedMutex [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  resource1_t resource;
  status = img->capture_texture->QueryInterface(__uuidof(IDXGIResource1), (void **)&resource);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to query IDXGIResource1 [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  // Create a handle for the encoder device to use to open this texture
  status = resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &img->encoder_texture_handle);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create shared texture handle [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  img->data = (std::uint8_t *)img->capture_texture.get();

  return 0;
}

// This cannot use ID3D11DeviceContext because it can be called concurrently by the encoding thread
int display_vram_t::dummy_img(platf::img_t *img_base) {
  return complete_img(img_base, true);
}

std::vector<DXGI_FORMAT> display_vram_t::get_supported_sdr_capture_formats() {
  return { DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM };
}

std::vector<DXGI_FORMAT> display_vram_t::get_supported_hdr_capture_formats() {
  return {
    // scRGB FP16 is the desired format for HDR content. This will also handle
    // 10-bit SDR displays with the increased precision of FP16 vs 8-bit UNORMs.
    DXGI_FORMAT_R16G16B16A16_FLOAT,

    // DXGI_FORMAT_R10G10B10A2_UNORM seems like it might give us frames already
    // converted to SMPTE 2084 PQ, however it seems to actually just clamp the
    // scRGB FP16 values that DWM is using when the desktop format is scRGB FP16.
    //
    // If there is a case where the desktop format is really SMPTE 2084 PQ, it
    // might make sense to support capturing it without conversion to scRGB,
    // but we avoid it for now.

    // We include the 8-bit modes too for when the display is in SDR mode,
    // while the client stream is HDR-capable. These UNORM formats behave
    // like a degenerate case of scRGB FP16 with values between 0.0f-1.0f.
    DXGI_FORMAT_B8G8R8A8_UNORM,
    DXGI_FORMAT_R8G8B8A8_UNORM,
  };
}

std::shared_ptr<platf::hwdevice_t> display_vram_t::make_hwdevice(pix_fmt_e pix_fmt) {
  if(pix_fmt != platf::pix_fmt_e::nv12 && pix_fmt != platf::pix_fmt_e::p010) {
    BOOST_LOG(error) << "display_vram_t doesn't support pixel format ["sv << from_pix_fmt(pix_fmt) << ']';

    return nullptr;
  }

  auto hwdevice = std::make_shared<hwdevice_t>();

  auto ret = hwdevice->init(
    shared_from_this(),
    adapter.get(),
    pix_fmt);

  if(ret) {
    return nullptr;
  }

  return hwdevice;
}

int init() {
  BOOST_LOG(info) << "Compiling shaders..."sv;
  scene_vs_hlsl = compile_vertex_shader(SUNSHINE_SHADERS_DIR "/SceneVS.hlsl");
  if(!scene_vs_hlsl) {
    return -1;
  }

  convert_Y_ps_hlsl = compile_pixel_shader(SUNSHINE_SHADERS_DIR "/ConvertYPS.hlsl");
  if(!convert_Y_ps_hlsl) {
    return -1;
  }

  convert_Y_PQ_ps_hlsl = compile_pixel_shader(SUNSHINE_SHADERS_DIR "/ConvertYPS_PQ.hlsl");
  if(!convert_Y_PQ_ps_hlsl) {
    return -1;
  }

  convert_UV_ps_hlsl = compile_pixel_shader(SUNSHINE_SHADERS_DIR "/ConvertUVPS.hlsl");
  if(!convert_UV_ps_hlsl) {
    return -1;
  }

  convert_UV_PQ_ps_hlsl = compile_pixel_shader(SUNSHINE_SHADERS_DIR "/ConvertUVPS_PQ.hlsl");
  if(!convert_UV_PQ_ps_hlsl) {
    return -1;
  }

  convert_UV_vs_hlsl = compile_vertex_shader(SUNSHINE_SHADERS_DIR "/ConvertUVVS.hlsl");
  if(!convert_UV_vs_hlsl) {
    return -1;
  }

  scene_ps_hlsl = compile_pixel_shader(SUNSHINE_SHADERS_DIR "/ScenePS.hlsl");
  if(!scene_ps_hlsl) {
    return -1;
  }

  scene_NW_ps_hlsl = compile_pixel_shader(SUNSHINE_SHADERS_DIR "/ScenePS_NW.hlsl");
  if(!scene_NW_ps_hlsl) {
    return -1;
  }
  BOOST_LOG(info) << "Compiled shaders"sv;

  return 0;
}
} // namespace platf::dxgi
