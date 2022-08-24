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

blend_t make_blend(device_t::pointer device, bool enable) {
  D3D11_BLEND_DESC bdesc {};
  auto &rt                 = bdesc.RenderTarget[0];
  rt.BlendEnable           = enable;
  rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

  if(enable) {
    rt.BlendOp      = D3D11_BLEND_OP_ADD;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;

    rt.SrcBlend  = D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;

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
blob_t scene_vs_hlsl;
blob_t convert_Y_ps_hlsl;
blob_t scene_ps_hlsl;

struct img_d3d_t : public platf::img_t {
  std::shared_ptr<platf::display_t> display;

  shader_res_t input_res;
  render_target_t scene_rt;

  texture2d_t texture;

  ~img_d3d_t() override = default;
};

util::buffer_t<std::uint8_t> make_cursor_image(util::buffer_t<std::uint8_t> &&img_data, DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info) {
  constexpr std::uint32_t black       = 0xFF000000;
  constexpr std::uint32_t white       = 0xFFFFFFFF;
  constexpr std::uint32_t transparent = 0;

  switch(shape_info.Type) {
  case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR:
    std::for_each((std::uint32_t *)std::begin(img_data), (std::uint32_t *)std::end(img_data), [](auto &pixel) {
      if(pixel & 0xFF000000) {
        pixel = transparent;
      }
    });
  case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
    return std::move(img_data);
  default:
    break;
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
      case 0: //black
        *pixel_data = black;
        break;
      case 2: //white
        *pixel_data = white;
        break;
      case 1: //transparent
      {
        *pixel_data = transparent;

        break;
      }
      case 3: //inverse
      {
        auto top_p    = pixel_data - shape_info.Width;
        auto left_p   = pixel_data - 1;
        auto right_p  = pixel_data + 1;
        auto bottom_p = pixel_data + shape_info.Width;

        // Get the x coordinate of the pixel
        auto column = (pixel_data - pixel_begin) % shape_info.Width != 0;

        if(top_p >= pixel_begin && *top_p == transparent) {
          *top_p = black;
        }

        if(column != 0 && left_p >= pixel_begin && *left_p == transparent) {
          *left_p = black;
        }

        if(bottom_p < (std::uint32_t *)std::end(cursor_img)) {
          *bottom_p = black;
        }

        if(column != shape_info.Width - 1) {
          *right_p = black;
        }
        *pixel_data = white;
      }
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

int init_rt(device_t::pointer device, shader_res_t &shader_res, render_target_t &render_target, int width, int height, DXGI_FORMAT format, texture2d_t::pointer tex) {
  D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_desc {
    format,
    D3D11_SRV_DIMENSION_TEXTURE2D
  };
  shader_resource_desc.Texture2D.MipLevels = 1;

  auto status = device->CreateShaderResourceView(tex, &shader_resource_desc, &shader_res);
  if(status) {
    BOOST_LOG(error) << "Failed to create render target texture for luma [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  D3D11_RENDER_TARGET_VIEW_DESC render_target_desc {
    format,
    D3D11_RTV_DIMENSION_TEXTURE2D
  };

  status = device->CreateRenderTargetView(tex, &render_target_desc, &render_target);
  if(status) {
    BOOST_LOG(error) << "Failed to create render target view [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  return 0;
}

int init_rt(device_t::pointer device, shader_res_t &shader_res, render_target_t &render_target, int width, int height, DXGI_FORMAT format) {
  D3D11_TEXTURE2D_DESC desc {};

  desc.Width            = width;
  desc.Height           = height;
  desc.Format           = format;
  desc.Usage            = D3D11_USAGE_DEFAULT;
  desc.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  desc.MipLevels        = 1;
  desc.ArraySize        = 1;
  desc.SampleDesc.Count = 1;

  texture2d_t tex;
  auto status = device->CreateTexture2D(&desc, nullptr, &tex);
  if(status) {
    BOOST_LOG(error) << "Failed to create render target texture for luma [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  return init_rt(device, shader_res, render_target, width, height, format, tex.get());
}

class hwdevice_t : public platf::hwdevice_t {
public:
  int convert(platf::img_t &img_base) override {
    auto &img = (img_d3d_t &)img_base;

    device_ctx_p->IASetInputLayout(input_layout.get());

    _init_view_port(this->img.width, this->img.height);
    device_ctx_p->OMSetRenderTargets(1, &nv12_Y_rt, nullptr);
    device_ctx_p->VSSetShader(scene_vs.get(), nullptr, 0);
    device_ctx_p->PSSetShader(convert_Y_ps.get(), nullptr, 0);
    device_ctx_p->PSSetShaderResources(0, 1, &back_img.input_res);
    device_ctx_p->Draw(3, 0);

    device_ctx_p->RSSetViewports(1, &outY_view);
    device_ctx_p->PSSetShaderResources(0, 1, &img.input_res);
    device_ctx_p->Draw(3, 0);

    // Artifacts start appearing on the rendered image if Sunshine doesn't flush
    // before rendering on the UV part of the image.
    device_ctx_p->Flush();

    _init_view_port(this->img.width / 2, this->img.height / 2);
    device_ctx_p->OMSetRenderTargets(1, &nv12_UV_rt, nullptr);
    device_ctx_p->VSSetShader(convert_UV_vs.get(), nullptr, 0);
    device_ctx_p->PSSetShader(convert_UV_ps.get(), nullptr, 0);
    device_ctx_p->PSSetShaderResources(0, 1, &back_img.input_res);
    device_ctx_p->Draw(3, 0);

    device_ctx_p->RSSetViewports(1, &outUV_view);
    device_ctx_p->PSSetShaderResources(0, 1, &img.input_res);
    device_ctx_p->Draw(3, 0);
    device_ctx_p->Flush();

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

    device_ctx_p->PSSetConstantBuffers(0, 1, &color_matrix);
    this->color_matrix = std::move(color_matrix);
  }

  int set_frame(AVFrame *frame) {
    this->hwframe.reset(frame);
    this->frame = frame;

    auto device_p = (device_t::pointer)data;

    auto out_width  = frame->width;
    auto out_height = frame->height;

    float in_width  = img.display->width;
    float in_height = img.display->height;

    // // Ensure aspect ratio is maintained
    auto scalar       = std::fminf(out_width / in_width, out_height / in_height);
    auto out_width_f  = in_width * scalar;
    auto out_height_f = in_height * scalar;

    // result is always positive
    auto offsetX = (out_width - out_width_f) / 2;
    auto offsetY = (out_height - out_height_f) / 2;

    outY_view  = D3D11_VIEWPORT { offsetX, offsetY, out_width_f, out_height_f, 0.0f, 1.0f };
    outUV_view = D3D11_VIEWPORT { offsetX / 2, offsetY / 2, out_width_f / 2, out_height_f / 2, 0.0f, 1.0f };

    D3D11_TEXTURE2D_DESC t {};
    t.Width            = out_width;
    t.Height           = out_height;
    t.MipLevels        = 1;
    t.ArraySize        = 1;
    t.SampleDesc.Count = 1;
    t.Usage            = D3D11_USAGE_DEFAULT;
    t.Format           = format;
    t.BindFlags        = D3D11_BIND_RENDER_TARGET;

    auto status = device_p->CreateTexture2D(&t, nullptr, &img.texture);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create render target texture [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    img.width       = out_width;
    img.height      = out_height;
    img.data        = (std::uint8_t *)img.texture.get();
    img.row_pitch   = out_width * 4;
    img.pixel_pitch = 4;

    float info_in[16 / sizeof(float)] { 1.0f / (float)out_width }; //aligned to 16-byte
    info_scene = make_buffer(device_p, info_in);

    if(!info_in) {
      BOOST_LOG(error) << "Failed to create info scene buffer"sv;
      return -1;
    }

    D3D11_RENDER_TARGET_VIEW_DESC nv12_rt_desc {
      DXGI_FORMAT_R8_UNORM,
      D3D11_RTV_DIMENSION_TEXTURE2D
    };

    status = device_p->CreateRenderTargetView(img.texture.get(), &nv12_rt_desc, &nv12_Y_rt);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create render target view [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    nv12_rt_desc.Format = DXGI_FORMAT_R8G8_UNORM;

    status = device_p->CreateRenderTargetView(img.texture.get(), &nv12_rt_desc, &nv12_UV_rt);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create render target view [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    // Need to have something refcounted
    if(!frame->buf[0]) {
      frame->buf[0] = av_buffer_allocz(sizeof(AVD3D11FrameDescriptor));
    }

    auto desc     = (AVD3D11FrameDescriptor *)frame->buf[0]->data;
    desc->texture = (ID3D11Texture2D *)img.data;
    desc->index   = 0;

    frame->data[0] = img.data;
    frame->data[1] = 0;

    frame->linesize[0] = img.row_pitch;

    frame->height = img.height;
    frame->width  = img.width;

    return 0;
  }

  int init(
    std::shared_ptr<platf::display_t> display, device_t::pointer device_p, device_ctx_t::pointer device_ctx_p,
    pix_fmt_e pix_fmt) {

    HRESULT status;

    device_p->AddRef();
    data = device_p;

    this->device_ctx_p = device_ctx_p;

    format = (pix_fmt == pix_fmt_e::nv12 ? DXGI_FORMAT_NV12 : DXGI_FORMAT_P010);
    status = device_p->CreateVertexShader(scene_vs_hlsl->GetBufferPointer(), scene_vs_hlsl->GetBufferSize(), nullptr, &scene_vs);
    if(status) {
      BOOST_LOG(error) << "Failed to create scene vertex shader [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    status = device_p->CreatePixelShader(convert_Y_ps_hlsl->GetBufferPointer(), convert_Y_ps_hlsl->GetBufferSize(), nullptr, &convert_Y_ps);
    if(status) {
      BOOST_LOG(error) << "Failed to create convertY pixel shader [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    status = device_p->CreatePixelShader(convert_UV_ps_hlsl->GetBufferPointer(), convert_UV_ps_hlsl->GetBufferSize(), nullptr, &convert_UV_ps);
    if(status) {
      BOOST_LOG(error) << "Failed to create convertUV pixel shader [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    status = device_p->CreateVertexShader(convert_UV_vs_hlsl->GetBufferPointer(), convert_UV_vs_hlsl->GetBufferSize(), nullptr, &convert_UV_vs);
    if(status) {
      BOOST_LOG(error) << "Failed to create convertUV vertex shader [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    status = device_p->CreatePixelShader(scene_ps_hlsl->GetBufferPointer(), scene_ps_hlsl->GetBufferSize(), nullptr, &scene_ps);
    if(status) {
      BOOST_LOG(error) << "Failed to create scene pixel shader [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    color_matrix = make_buffer(device_p, ::video::colors[0]);
    if(!color_matrix) {
      BOOST_LOG(error) << "Failed to create color matrix buffer"sv;
      return -1;
    }

    D3D11_INPUT_ELEMENT_DESC layout_desc {
      "SV_Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0
    };

    status = device_p->CreateInputLayout(
      &layout_desc, 1,
      convert_UV_vs_hlsl->GetBufferPointer(), convert_UV_vs_hlsl->GetBufferSize(),
      &input_layout);

    img.display = std::move(display);

    // Color the background black, so that the padding for keeping the aspect ratio
    // is black
    if(img.display->dummy_img(&back_img)) {
      BOOST_LOG(warning) << "Couldn't create an image to set background color to black"sv;
      return -1;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC desc {
      DXGI_FORMAT_B8G8R8A8_UNORM,
      D3D11_SRV_DIMENSION_TEXTURE2D
    };
    desc.Texture2D.MipLevels = 1;

    status = device_p->CreateShaderResourceView(back_img.texture.get(), &desc, &back_img.input_res);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create input shader resource view [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    device_ctx_p->IASetInputLayout(input_layout.get());
    device_ctx_p->PSSetConstantBuffers(0, 1, &color_matrix);
    device_ctx_p->VSSetConstantBuffers(0, 1, &info_scene);

    return 0;
  }

  ~hwdevice_t() override {
    if(data) {
      ((ID3D11Device *)data)->Release();
    }
  }

private:
  void _init_view_port(float x, float y, float width, float height) {
    D3D11_VIEWPORT view {
      x, y,
      width, height,
      0.0f, 1.0f
    };

    device_ctx_p->RSSetViewports(1, &view);
  }

  void _init_view_port(float width, float height) {
    _init_view_port(0.0f, 0.0f, width, height);
  }

public:
  frame_t hwframe;

  ::video::color_t *color_p;

  buf_t info_scene;
  buf_t color_matrix;

  input_layout_t input_layout;

  render_target_t nv12_Y_rt;
  render_target_t nv12_UV_rt;

  // The image referenced by hwframe
  // The resulting image is stored here.
  img_d3d_t img;

  // Clear nv12 render target to black
  img_d3d_t back_img;

  vs_t convert_UV_vs;
  ps_t convert_UV_ps;
  ps_t convert_Y_ps;
  ps_t scene_ps;
  vs_t scene_vs;

  D3D11_VIEWPORT outY_view;
  D3D11_VIEWPORT outUV_view;

  DXGI_FORMAT format;

  device_ctx_t::pointer device_ctx_p;
};

capture_e display_vram_t::capture(snapshot_cb_t &&snapshot_cb, std::shared_ptr<::platf::img_t> img, bool *cursor) {
  auto next_frame = std::chrono::steady_clock::now();

  while(img) {
    auto now = std::chrono::steady_clock::now();
    while(next_frame > now) {
      now = std::chrono::steady_clock::now();
    }
    next_frame = now + delay;

    auto status = snapshot(img.get(), 1000ms, *cursor);
    switch(status) {
    case platf::capture_e::reinit:
    case platf::capture_e::error:
      return status;
    case platf::capture_e::timeout:
      std::this_thread::sleep_for(1ms);
      continue;
    case platf::capture_e::ok:
      img = snapshot_cb(img);
      break;
    default:
      BOOST_LOG(error) << "Unrecognized capture status ["sv << (int)status << ']';
      return status;
    }
  }

  return capture_e::ok;
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

    auto cursor_img = make_cursor_image(std::move(img_data), shape_info);

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
    t.Usage            = D3D11_USAGE_DEFAULT;
    t.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    t.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    texture2d_t texture;
    auto status = device->CreateTexture2D(&t, &data, &texture);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create mouse texture [0x"sv << util::hex(status).to_string_view() << ']';
      return capture_e::error;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC desc {
      DXGI_FORMAT_B8G8R8A8_UNORM,
      D3D11_SRV_DIMENSION_TEXTURE2D
    };
    desc.Texture2D.MipLevels = 1;

    // Free resources before allocating on the next line.
    cursor.input_res.reset();
    status = device->CreateShaderResourceView(texture.get(), &desc, &cursor.input_res);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create cursor shader resource view [0x"sv << util::hex(status).to_string_view() << ']';
      return capture_e::error;
    }

    cursor.set_texture(t.Width, t.Height, std::move(texture));
  }

  if(frame_info.LastMouseUpdateTime.QuadPart) {
    cursor.set_pos(frame_info.PointerPosition.Position.x, frame_info.PointerPosition.Position.y, frame_info.PointerPosition.Visible && cursor_visible);
  }

  if(frame_update_flag) {
    src.reset();
    status = res->QueryInterface(IID_ID3D11Texture2D, (void **)&src);

    if(FAILED(status)) {
      BOOST_LOG(error) << "Couldn't query interface [0x"sv << util::hex(status).to_string_view() << ']';
      return capture_e::error;
    }
  }

  device_ctx->CopyResource(img->texture.get(), src.get());
  if(cursor.visible) {
    D3D11_VIEWPORT view {
      0.0f, 0.0f,
      (float)width, (float)height,
      0.0f, 1.0f
    };

    device_ctx->VSSetShader(scene_vs.get(), nullptr, 0);
    device_ctx->PSSetShader(scene_ps.get(), nullptr, 0);
    device_ctx->RSSetViewports(1, &view);
    device_ctx->OMSetRenderTargets(1, &img->scene_rt, nullptr);
    device_ctx->PSSetShaderResources(0, 1, &cursor.input_res);
    device_ctx->OMSetBlendState(blend_enable.get(), nullptr, 0xFFFFFFFFu);
    device_ctx->RSSetViewports(1, &cursor.cursor_view);
    device_ctx->Draw(3, 0);
    device_ctx->OMSetBlendState(blend_disable.get(), nullptr, 0xFFFFFFFFu);
  }

  return capture_e::ok;
}

int display_vram_t::init(int framerate, const std::string &display_name) {
  if(display_base_t::init(framerate, display_name)) {
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

  status = device->CreatePixelShader(scene_ps_hlsl->GetBufferPointer(), scene_ps_hlsl->GetBufferSize(), nullptr, &scene_ps);
  if(status) {
    BOOST_LOG(error) << "Failed to create scene pixel shader [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  blend_enable  = make_blend(device.get(), true);
  blend_disable = make_blend(device.get(), false);

  if(!blend_disable || !blend_enable) {
    return -1;
  }

  device_ctx->OMSetBlendState(blend_disable.get(), nullptr, 0xFFFFFFFFu);
  device_ctx->PSSetSamplers(0, 1, &sampler_linear);
  device_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  return 0;
}

std::shared_ptr<platf::img_t> display_vram_t::alloc_img() {
  auto img = std::make_shared<img_d3d_t>();

  img->pixel_pitch = 4;
  img->row_pitch   = img->pixel_pitch * width;
  img->width       = width;
  img->height      = height;
  img->display     = shared_from_this();

  auto dummy_data = std::make_unique<std::uint8_t[]>(img->row_pitch * height);
  D3D11_SUBRESOURCE_DATA data {
    dummy_data.get(),
    (UINT)img->row_pitch
  };
  std::fill_n(dummy_data.get(), img->row_pitch * height, 0);

  D3D11_TEXTURE2D_DESC t {};
  t.Width            = width;
  t.Height           = height;
  t.MipLevels        = 1;
  t.ArraySize        = 1;
  t.SampleDesc.Count = 1;
  t.Usage            = D3D11_USAGE_DEFAULT;
  t.Format           = format;
  t.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

  auto status = device->CreateTexture2D(&t, &data, &img->texture);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create img buf texture [0x"sv << util::hex(status).to_string_view() << ']';
    return nullptr;
  }

  if(init_rt(device.get(), img->input_res, img->scene_rt, width, height, format, img->texture.get())) {
    return nullptr;
  }

  img->data = (std::uint8_t *)img->texture.get();

  return img;
}

int display_vram_t::dummy_img(platf::img_t *img_base) {
  auto img = (img_d3d_t *)img_base;

  if(img->texture) {
    return 0;
  }

  img->row_pitch  = width * 4;
  auto dummy_data = std::make_unique<int[]>(width * height);
  D3D11_SUBRESOURCE_DATA data {
    dummy_data.get(),
    (UINT)img->row_pitch
  };
  std::fill_n(dummy_data.get(), width * height, 0);

  D3D11_TEXTURE2D_DESC t {};
  t.Width            = width;
  t.Height           = height;
  t.MipLevels        = 1;
  t.ArraySize        = 1;
  t.SampleDesc.Count = 1;
  t.Usage            = D3D11_USAGE_DEFAULT;
  t.Format           = format;
  t.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

  dxgi::texture2d_t tex;
  auto status = device->CreateTexture2D(&t, &data, &tex);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create dummy texture [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  img->texture = std::move(tex);
  img->data    = (std::uint8_t *)img->texture.get();

  return 0;
}

std::shared_ptr<platf::hwdevice_t> display_vram_t::make_hwdevice(pix_fmt_e pix_fmt) {
  if(pix_fmt != platf::pix_fmt_e::nv12) {
    BOOST_LOG(error) << "display_vram_t doesn't support pixel format ["sv << from_pix_fmt(pix_fmt) << ']';

    return nullptr;
  }

  auto hwdevice = std::make_shared<hwdevice_t>();

  auto ret = hwdevice->init(
    shared_from_this(),
    device.get(),
    device_ctx.get(),
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

  convert_UV_ps_hlsl = compile_pixel_shader(SUNSHINE_SHADERS_DIR "/ConvertUVPS.hlsl");
  if(!convert_UV_ps_hlsl) {
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
  BOOST_LOG(info) << "Compiled shaders"sv;

  return 0;
}
} // namespace platf::dxgi