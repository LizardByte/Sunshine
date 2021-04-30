#include <codecvt>

#include <d3dcompiler.h>

#include "sunshine/main.h"
#include "display.h"

namespace platf {
using namespace std::literals;
}

namespace platf::dxgi {
constexpr float aquamarine[] { 0.498039246f, 1.000000000f, 0.831372619f, 1.000000000f };

using input_layout_t        = util::safe_ptr<ID3D11InputLayout, Release<ID3D11InputLayout>>;
using render_target_t       = util::safe_ptr<ID3D11RenderTargetView, Release<ID3D11RenderTargetView>>;
using shader_res_t          = util::safe_ptr<ID3D11ShaderResourceView, Release<ID3D11ShaderResourceView>>;
using raster_state_t        = util::safe_ptr<ID3D11RasterizerState, Release<ID3D11RasterizerState>>;
using sampler_state_t       = util::safe_ptr<ID3D11SamplerState, Release<ID3D11SamplerState>>;
using vs_t                  = util::safe_ptr<ID3D11VertexShader, Release<ID3D11VertexShader>>;
using ps_t                  = util::safe_ptr<ID3D11PixelShader, Release<ID3D11PixelShader>>;
using blob_t                = util::safe_ptr<ID3DBlob, Release<ID3DBlob>>;
using depth_stencil_state_t = util::safe_ptr<ID3D11DepthStencilState, Release<ID3D11DepthStencilState>>;
using depth_stencil_view_t  = util::safe_ptr<ID3D11DepthStencilView, Release<ID3D11DepthStencilView>>;

blob_t merge_UV_vs_hlsl;
blob_t merge_UV_ps_hlsl;
blob_t screen_vs_hlsl;
blob_t screen_ps_hlsl;
blob_t YCrCb_ps_hlsl;

struct img_d3d_t : public platf::img_t {
  shader_res_t input_res;
  texture2d_t texture;
  std::shared_ptr<platf::display_t> display;

  ~img_d3d_t() override = default;
};

util::buffer_t<std::uint8_t> make_cursor_image(util::buffer_t<std::uint8_t> &&img_data, DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info)  {
  constexpr std::uint32_t black = 0xFF000000;
  constexpr std::uint32_t white = 0xFFFFFFFF;
  constexpr std::uint32_t transparent = 0;

  switch(shape_info.Type) {
    case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR:
      std::for_each((std::uint32_t*)std::begin(img_data), (std::uint32_t*)std::end(img_data), [](auto &pixel) {
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

  auto bytes = shape_info.Pitch * shape_info.Height;
  auto pixel_begin = (std::uint32_t*)std::begin(cursor_img);
  auto pixel_data = pixel_begin;
  auto and_mask = std::begin(img_data);
  auto xor_mask = std::begin(img_data) + bytes;

  for(auto x = 0; x < bytes; ++x)  {
    for(auto c = 7; c >= 0; --c) {
      auto bit = 1 << c;
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

          if(bottom_p < (std::uint32_t*)std::end(cursor_img)) {
            *bottom_p = black;
          }

          if(column != shape_info.Width -1) {
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

  auto wFile = converter.from_bytes(file);
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
  return compile_shader(file, "PS", "ps_5_0");
}

blob_t compile_vertex_shader(LPCSTR file) {
  return compile_shader(file, "VS", "vs_5_0");
}

class hwdevice_t : public platf::hwdevice_t {
public:
  hwdevice_t(std::vector<hwdevice_t*> *hwdevices_p) : hwdevices_p { hwdevices_p } {}
  hwdevice_t() = delete;

  void set_cursor_pos(LONG rel_x, LONG rel_y, bool visible) {
    cursor_visible = visible;

    if(!visible) {
      return;
    }

    LONG x = ((double)rel_x) * out_width / (double)in_width;
    LONG y = ((double)rel_y) * out_height / (double)in_height;

    // Ensure it's within bounds
    auto left_out   = std::min<LONG>(out_width, std::max<LONG>(0, x));
    auto top_out    = std::min<LONG>(out_height, std::max<LONG>(0, y));
    auto right_out  = std::max<LONG>(0, std::min<LONG>(out_width, x + cursor_scaled_width));
    auto bottom_out = std::max<LONG>(0, std::min<LONG>(out_height, y + cursor_scaled_height));

    auto left_in   = std::max<LONG>(0, -rel_x);
    auto top_in    = std::max<LONG>(0, -rel_y);
    auto right_in  = std::min<LONG>(in_width - rel_x, cursor_width);
    auto bottom_in = std::min<LONG>(in_height - rel_y, cursor_height);

    RECT rect_in { left_in, top_in, right_in, bottom_in };
    RECT rect_out { left_out, top_out, right_out, bottom_out };
  }

  int set_cursor_texture(texture2d_t::pointer texture, LONG width, LONG height) {
    cursor_width  = width;
    cursor_height = height;
    cursor_scaled_width = ((double)width) / in_width * out_width;
    cursor_scaled_height = ((double)height) / in_height * out_height;

    return 0;
  }

  int convert(platf::img_t &img_base) override {
    auto &img = (img_d3d_t&)img_base;

    if(!img.input_res) {
      auto device = (device_t::pointer)data;

      D3D11_SHADER_RESOURCE_VIEW_DESC desc {
        DXGI_FORMAT_B8G8R8A8_UNORM,
        D3D11_SRV_DIMENSION_TEXTURE2D
      };
      desc.Texture2D.MipLevels = 1;

      shader_res_t::pointer input_rec_p;
      auto status = device->CreateShaderResourceView(img.texture.get(), &desc, &input_rec_p);
      if(FAILED(status)) {
        BOOST_LOG(error) << "Failed to create input shader resource view [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }
      img.input_res.reset(input_rec_p);
    }

    auto nv12_rt_p = nv12_rt.get();
    auto sampler_point_p = sampler_point.get();
    auto input_res_p = img.input_res.get();
    auto luma_sr_p = luma_sr.get();

    render_target_t::pointer pYCbCrRT[] {
      luma_rt.get(), chromaCB_rt.get(), chromaCR_rt.get()
    };

    shader_res_t::pointer merge_ress[] {
      chromaCB_sr.get(), chromaCR_sr.get(), shift_sr.get()
    };

    _init_view_port(out_width, out_height);
    device_ctx_p->PSSetSamplers(0, 1, &sampler_point_p);

    device_ctx_p->OMSetRenderTargets(3, pYCbCrRT, nullptr);
    for(auto rt : pYCbCrRT) {
      device_ctx_p->ClearRenderTargetView(rt, aquamarine);
    }
    device_ctx_p->VSSetShader(screen_vs.get(), nullptr, 0);
    device_ctx_p->PSSetShader(YCrCb_ps.get(), nullptr, 0);
    device_ctx_p->PSSetShaderResources(0, 1, &input_res_p);
    device_ctx_p->Draw(4, 0);
    device_ctx_p->Flush();

    // downsample
    device_ctx_p->GenerateMips(chromaCR_sr.get());
    device_ctx_p->GenerateMips(chromaCB_sr.get());

    device_ctx_p->OMSetRenderTargets(1, &nv12_rt_p, nullptr);
    device_ctx_p->ClearRenderTargetView(nv12_rt_p, aquamarine);
    device_ctx_p->VSSetShader(screen_vs.get(), nullptr, 0);
    device_ctx_p->PSSetShader(screen_ps.get(), nullptr, 0);
    device_ctx_p->PSSetShaderResources(0, 1, &luma_sr_p);
    device_ctx_p->Draw(4, 0);
    device_ctx_p->Flush();

    _init_view_port(out_width, out_height *2);
    device_ctx_p->VSSetShader(merge_UV_vs.get(), nullptr, 0);
    device_ctx_p->PSSetShader(merge_UV_ps.get(), nullptr, 0);
    for(int x = 0; x < ARRAYSIZE(merge_ress); ++x) {
      device_ctx_p->PSSetShaderResources(x, 1, &merge_ress[x]);
    }
    device_ctx_p->Draw(4, 0);
    device_ctx_p->Flush();

    return 0;
  }

  void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) override {}

  int init(
    std::shared_ptr<platf::display_t> display, device_t::pointer device_p, device_ctx_t::pointer device_ctx_p,
    int in_width, int in_height, int out_width, int out_height,
    pix_fmt_e pix_fmt
  ) {
    HRESULT status;

    device_p->AddRef();
    data = device_p;

    this->device_ctx_p = device_ctx_p;

    cursor_visible = false;

    platf::hwdevice_t::img = &img;

    this->out_width  = out_width;
    this->out_height = out_height;
    this->in_width   = in_width;
    this->in_height  = in_height;

    vs_t::pointer screen_vs_p;
    status = device_p->CreateVertexShader(screen_vs_hlsl->GetBufferPointer(), screen_vs_hlsl->GetBufferSize(), nullptr, &screen_vs_p);
    if(status) {
      BOOST_LOG(error) << "Failed to create screen vertex shader [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    screen_vs.reset(screen_vs_p);

    ps_t::pointer screen_ps_p;
    status = device_p->CreatePixelShader(screen_ps_hlsl->GetBufferPointer(), screen_ps_hlsl->GetBufferSize(), nullptr, &screen_ps_p);
    if(status) {
      BOOST_LOG(error) << "Failed to create screen pixel shader [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    screen_ps.reset(screen_ps_p);

    ps_t::pointer YCrCb_ps_p;
    status = device_p->CreatePixelShader(YCrCb_ps_hlsl->GetBufferPointer(), YCrCb_ps_hlsl->GetBufferSize(), nullptr, &YCrCb_ps_p);
    if(status) {
      BOOST_LOG(error) << "Failed to create YCrCb pixel shader [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    YCrCb_ps.reset(YCrCb_ps_p);

    ps_t::pointer merge_UV_ps_p;
    status = device_p->CreatePixelShader(merge_UV_ps_hlsl->GetBufferPointer(), merge_UV_ps_hlsl->GetBufferSize(), nullptr, &merge_UV_ps_p);
    if(status) {
      BOOST_LOG(error) << "Failed to create mergeUV pixel shader [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    merge_UV_ps.reset(merge_UV_ps_p);

    vs_t::pointer merge_UV_vs_p;
    status = device_p->CreateVertexShader(merge_UV_vs_hlsl->GetBufferPointer(), merge_UV_vs_hlsl->GetBufferSize(), nullptr, &merge_UV_vs_p);
    if(status) {
      BOOST_LOG(error) << "Failed to create mergeUV vertex shader [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    merge_UV_vs.reset(merge_UV_vs_p);

    D3D11_INPUT_ELEMENT_DESC layout_desc {
      "SV_Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0
    };

    input_layout_t::pointer input_layout_p;
    status = device_p->CreateInputLayout(
      &layout_desc, 1,
      merge_UV_vs_hlsl->GetBufferPointer(), merge_UV_vs_hlsl->GetBufferSize(),
      &input_layout_p);
    input_layout.reset(input_layout_p);

    D3D11_TEXTURE2D_DESC t {};
    t.Width  = out_width;
    t.Height = out_height;
    t.MipLevels = 1;
    t.ArraySize = 1;
    t.SampleDesc.Count = 1;
    t.Usage = D3D11_USAGE_DEFAULT;
    t.Format = pix_fmt == pix_fmt_e::nv12 ? DXGI_FORMAT_NV12 : DXGI_FORMAT_P010;
    t.BindFlags = D3D11_BIND_RENDER_TARGET;

    dxgi::texture2d_t::pointer tex_p {};
    status = device_p->CreateTexture2D(&t, nullptr, &tex_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create render target texture [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    img.texture.reset(tex_p);
    img.display = std::move(display);
    img.width = out_width;
    img.height = out_height;
    img.data = (std::uint8_t*)tex_p;
    img.row_pitch = out_width;
    img.pixel_pitch = 1;

    D3D11_RENDER_TARGET_VIEW_DESC nv12_rt_desc {
      DXGI_FORMAT_R8_UNORM,
      D3D11_RTV_DIMENSION_TEXTURE2D
    };

    render_target_t::pointer nv12_rt_p;
    status = device_p->CreateRenderTargetView(img.texture.get(), &nv12_rt_desc, &nv12_rt_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create render target view [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    nv12_rt.reset(nv12_rt_p);

    if(
      _init_rt(&luma_sr, &luma_rt, out_width, out_height, 1, DXGI_FORMAT_R8_UNORM) ||
      _init_rt(&chromaCB_sr, &chromaCB_rt, out_width, out_height, 2, DXGI_FORMAT_R8_UNORM, D3D11_RESOURCE_MISC_GENERATE_MIPS) ||
      _init_rt(&chromaCR_sr, &chromaCR_rt, out_width, out_height, 2, DXGI_FORMAT_R8_UNORM, D3D11_RESOURCE_MISC_GENERATE_MIPS) ||
      _init_shift_sr(out_width))
    {
      return -1;
    }

    // t.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    // t.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    // status = device_p->CreateTexture2D(&t, nullptr, &tex_p);
    // if(FAILED(status)) {
    //   BOOST_LOG(error) << "Failed to create depth stencil texture [0x"sv << util::hex(status).to_string_view() << ']';
    //   return -1;
    // }
    // depth_stencil.reset(tex_p);

    D3D11_SAMPLER_DESC sampler_desc {};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

    sampler_state_t::pointer sampler_state_p;
    status = device_p->CreateSamplerState(&sampler_desc, &sampler_state_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create point sampler state [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    sampler_point.reset(sampler_state_p);

    // D3D11_DEPTH_STENCIL_DESC depth_stencil_desc {};
    // depth_stencil_desc.DepthEnable = FALSE;
    // depth_stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    // depth_stencil_desc.StencilEnable = true;
    // depth_stencil_desc.StencilReadMask = 0xFF;
    // depth_stencil_desc.StencilWriteMask = 0xFF;

    // depth_stencil_desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    // depth_stencil_desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
    // depth_stencil_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    // depth_stencil_desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

    // depth_stencil_desc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    // depth_stencil_desc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
    // depth_stencil_desc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    // depth_stencil_desc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

    // depth_stencil_state_t::pointer depth_state_p;
    // status = device_p->CreateDepthStencilState(&depth_stencil_desc, &depth_state_p);
    // if(FAILED(status)) {
    //   BOOST_LOG(error) << "Failed to create depth stencil state [0x"sv << util::hex(status).to_string_view() << ']';
    //   return -1;
    // }
    // depth_state.reset(depth_state_p);

    // D3D11_DEPTH_STENCIL_VIEW_DESC depth_view_desc {};
    // depth_view_desc.Format = t.Format;
    // depth_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

    // depth_stencil_view_t::pointer depth_view_p;
    // status = device_p->CreateDepthStencilView(depth_stencil.get(), &depth_view_desc, &depth_view_p);
    // if(FAILED(status)) {
    //   BOOST_LOG(error) << "Failed to create depth stencil view [0x"sv << util::hex(status).to_string_view() << ']';
    //   return -1;
    // }
    // depth_view.reset(depth_view_p);

    // // Setup the raster description which will determine how and what polygons will be drawn.
    // D3D11_RASTERIZER_DESC raster_desc;
    // raster_desc.AntialiasedLineEnable = false;
    // raster_desc.CullMode = D3D11_CULL_BACK;
    // raster_desc.DepthBias = 0;
    // raster_desc.DepthBiasClamp = 0.0f;
    // raster_desc.DepthClipEnable = true;
    // raster_desc.FillMode = D3D11_FILL_SOLID;
    // raster_desc.FrontCounterClockwise = false;
    // raster_desc.MultisampleEnable = false;
    // raster_desc.ScissorEnable = false;
    // raster_desc.SlopeScaledDepthBias = 0.0f;

    // raster_state_t::pointer raster_state_p;
    // status = device_p->CreateRasterizerState(&raster_desc, &raster_state_p);
    // if(FAILED(status)) {
    //   BOOST_LOG(error) << "Failed to create rasterizer state [0x"sv << util::hex(status).to_string_view() << ']';
    //   return -1;
    // }
    // raster_state.reset(raster_state_p);

    auto sampler_p = sampler_point.get();
    device_ctx_p->PSSetSamplers(0, 1, &sampler_p);
    // device_ctx_p->RSSetState(raster_state.get());
    device_ctx_p->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    device_ctx_p->IASetInputLayout(input_layout.get());

    return 0;
  }

  ~hwdevice_t() override {
    if(data) {
      ((ID3D11Device*)data)->Release();
    }

    auto it = std::find(std::begin(*hwdevices_p), std::end(*hwdevices_p), this);
    if(it != std::end(*hwdevices_p)) {
      hwdevices_p->erase(it);
    }
  }
private:
  void _init_view_port(float width, float height) {
    D3D11_VIEWPORT view {
      0.0f, 0.0f,
      width, height,
      0.0f, 1.0f
    };

    device_ctx_p->RSSetViewports(1, &view);
  }

  int _init_rt(shader_res_t *shader_res, render_target_t *render_target, int width, int height, int mip_levels, DXGI_FORMAT format, int flags = 0) {
    D3D11_TEXTURE2D_DESC desc {};

    desc.Width            = width;
    desc.Height           = height;
    desc.Format           = format;
    desc.Usage            = D3D11_USAGE_DEFAULT;
    desc.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MipLevels        = mip_levels;
    desc.ArraySize        = 1;
    desc.SampleDesc.Count = 1;
    desc.MiscFlags        = flags;

    auto device = (device_t::pointer)data;

    texture2d_t::pointer tex_p;
    auto status = device->CreateTexture2D(&desc, nullptr, &tex_p);
    if(status) {
      BOOST_LOG(error) << "Failed to create render target texture for luma [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    texture2d_t tex { tex_p };

    if(shader_res) {
      D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_desc {
        format,
        D3D11_SRV_DIMENSION_TEXTURE2D
      };
      shader_resource_desc.Texture2D.MipLevels = mip_levels;

      shader_res_t::pointer shader_res_p;
      device->CreateShaderResourceView(tex_p, &shader_resource_desc, &shader_res_p);
      if(status) {
        BOOST_LOG(error) << "Failed to create render target texture for luma [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }
      shader_res->reset(shader_res_p);
    }

    if(render_target) {
      D3D11_RENDER_TARGET_VIEW_DESC render_target_desc {
        format,
        D3D11_RTV_DIMENSION_TEXTURE2D
      };

      render_target_t::pointer render_target_p;
      device->CreateRenderTargetView(tex_p, &render_target_desc, &render_target_p);
      if(status) {
        BOOST_LOG(error) << "Failed to create render target view [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }
      render_target->reset(render_target_p);
    }

    return 0;
  }

  int _init_shift_sr(int width) {
    auto device = (device_t::pointer)data;
    D3D11_TEXTURE1D_DESC desc {};
    desc.Width = width;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    util::buffer_t<BYTE> data { (std::size_t)width };
    for(int x = 0; x < data.size(); ++x) {
      data[x] = x & 1;
    }

    D3D11_SUBRESOURCE_DATA data_res {
      std::begin(data),
      (UINT)data.size()
    };

    texture1d_t::pointer tex_p {};
    auto status = device->CreateTexture1D(&desc, &data_res, &tex_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create shift texture [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    texture1d_t tex { tex_p };

    D3D11_SHADER_RESOURCE_VIEW_DESC res_desc {
      DXGI_FORMAT_R8_UNORM,
      D3D11_SRV_DIMENSION_TEXTURE1D
    };
    res_desc.Texture1D.MipLevels = 1;

    shader_res_t::pointer shader_res_p;
    device->CreateShaderResourceView(tex_p, &res_desc, &shader_res_p);
    if(status) {
      BOOST_LOG(error) << "Failed to create render target texture for luma [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    shift_sr.reset(shader_res_p);

    return 0;
  }

public:
  // raster_state_t raster_state;

  sampler_state_t sampler_point;

  // depth_stencil_view_t depth_view;
  // depth_stencil_state_t depth_state;

  shader_res_t chromaCB_sr;
  shader_res_t chromaCR_sr;
  shader_res_t luma_sr;
  shader_res_t shift_sr;

  input_layout_t input_layout;
  // texture2d_t depth_stencil;

  render_target_t luma_rt;
  render_target_t nv12_rt;
  render_target_t chromaCB_rt;
  render_target_t chromaCR_rt;

  img_d3d_t img;

  vs_t merge_UV_vs;
  ps_t merge_UV_ps;
  vs_t screen_vs;
  ps_t screen_ps;
  ps_t YCrCb_ps;
  ps_t ChromaCbCr_ps;

  bool cursor_visible;

  LONG cursor_width, cursor_height;
  LONG cursor_scaled_width, cursor_scaled_height;

  LONG in_width, in_height;
  double out_width, out_height;

  device_ctx_t::pointer device_ctx_p;

  std::vector<hwdevice_t*> *hwdevices_p;
};

capture_e display_vram_t::snapshot(platf::img_t *img_base, std::chrono::milliseconds timeout, bool cursor_visible) {
  auto img = (img_d3d_t*)img_base;

  HRESULT status;

  DXGI_OUTDUPL_FRAME_INFO frame_info;

  resource_t::pointer res_p {};
  auto capture_status = dup.next_frame(frame_info, timeout, &res_p);
  resource_t res{ res_p };

  if(capture_status != capture_e::ok) {
    return capture_status;
  }

  const bool mouse_update_flag = frame_info.LastMouseUpdateTime.QuadPart != 0 || frame_info.PointerShapeBufferSize > 0;
  const bool frame_update_flag = frame_info.AccumulatedFrames != 0 || frame_info.LastPresentTime.QuadPart != 0;
  const bool update_flag = mouse_update_flag || frame_update_flag;

  if(!update_flag) {
    return capture_e::timeout;
  }

  if(frame_info.PointerShapeBufferSize > 0) {
    DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info {};

    util::buffer_t<std::uint8_t> img_data { frame_info.PointerShapeBufferSize };

    UINT dummy;
    status = dup.dup->GetFramePointerShape(img_data.size(), std::begin(img_data), &dummy, &shape_info);
    if (FAILED(status)) {
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
    t.Width  = shape_info.Width;
    t.Height = cursor_img.size() / data.SysMemPitch;
    t.MipLevels = 1;
    t.ArraySize = 1;
    t.SampleDesc.Count = 1;
    t.Usage = D3D11_USAGE_DEFAULT;
    t.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    t.BindFlags = D3D11_BIND_RENDER_TARGET;

    dxgi::texture2d_t::pointer tex_p {};
    auto status = device->CreateTexture2D(&t, &data, &tex_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create mouse texture [0x"sv << util::hex(status).to_string_view() << ']';
      return capture_e::error;
    }
    texture2d_t texture { tex_p };

    for(auto *hwdevice : hwdevices) {
      if(hwdevice->set_cursor_texture(tex_p, t.Width, t.Height)) {
        return capture_e::error;
      }
    }

    cursor.texture = std::move(texture);
    cursor.width   = t.Width;
    cursor.height  = t.Height;
  }

  if(frame_info.LastMouseUpdateTime.QuadPart) {
    for(auto *hwdevice : hwdevices) {
      hwdevice->set_cursor_pos(frame_info.PointerPosition.Position.x, frame_info.PointerPosition.Position.y, frame_info.PointerPosition.Visible && cursor_visible);
    }
  }

  if(frame_update_flag) {
    texture2d_t::pointer src_p {};
    status = res->QueryInterface(IID_ID3D11Texture2D, (void **)&src_p);

    if(FAILED(status)) {
      BOOST_LOG(error) << "Couldn't query interface [0x"sv << util::hex(status).to_string_view() << ']';
      return capture_e::error;
    }

    texture2d_t src { src_p };
    device_ctx->CopyResource(img->texture.get(), src.get());
  }

  return capture_e::ok;
}

std::shared_ptr<platf::img_t> display_vram_t::alloc_img() {
  auto img = std::make_shared<img_d3d_t>();

  D3D11_TEXTURE2D_DESC t {};
  t.Width  = width;
  t.Height = height;
  t.MipLevels = 1;
  t.ArraySize = 1;
  t.SampleDesc.Count = 1;
  t.Usage = D3D11_USAGE_DEFAULT;
  t.Format = format;
  t.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  dxgi::texture2d_t::pointer tex_p {};
  auto status = device->CreateTexture2D(&t, nullptr, &tex_p);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create img buf texture [0x"sv << util::hex(status).to_string_view() << ']';
    return nullptr;
  }

  img->texture.reset(tex_p);
  img->data        = (std::uint8_t*)tex_p;
  img->row_pitch   = 0;
  img->pixel_pitch = 4;
  img->width       = 0;
  img->height      = 0;
  img->display     = shared_from_this();

  return img;
}

int display_vram_t::dummy_img(platf::img_t *img_base) {
  auto img = (img_d3d_t*)img_base;

  img->row_pitch = width * 4;
  auto dummy_data = std::make_unique<int[]>(width * height);
  D3D11_SUBRESOURCE_DATA data {
    dummy_data.get(),
    (UINT)img->row_pitch
  };

  D3D11_TEXTURE2D_DESC t {};
  t.Width  = width;
  t.Height = height;
  t.MipLevels = 1;
  t.ArraySize = 1;
  t.SampleDesc.Count = 1;
  t.Usage = D3D11_USAGE_DEFAULT;
  t.Format = format;
  t.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  dxgi::texture2d_t::pointer tex_p {};
  auto status = device->CreateTexture2D(&t, &data, &tex_p);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create dummy texture [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  img->texture.reset(tex_p);
  img->data        = (std::uint8_t*)tex_p;
  img->height      = height;
  img->width       = width;
  img->pixel_pitch = 4;

  return 0;
}

std::shared_ptr<platf::hwdevice_t> display_vram_t::make_hwdevice(int width, int height, pix_fmt_e pix_fmt) {
  if(pix_fmt != platf::pix_fmt_e::nv12) {
    BOOST_LOG(error) << "display_vram_t doesn't support pixel format ["sv << from_pix_fmt(pix_fmt) << ']';

    return nullptr;
  }

  if(!screen_ps_hlsl) {
    BOOST_LOG(info) << "Compiling shaders..."sv;
    screen_vs_hlsl = compile_vertex_shader(SUNSHINE_ASSETS_DIR "/ScreenVS.hlsl");
    if(!screen_vs_hlsl) {
      return nullptr;
    }

    screen_ps_hlsl = compile_pixel_shader(SUNSHINE_ASSETS_DIR "/ScreenPS.hlsl");
    if(!screen_ps_hlsl) {
      return nullptr;
    }

    YCrCb_ps_hlsl = compile_pixel_shader(SUNSHINE_ASSETS_DIR "/YCbCrPS.hlsl");
    if(!YCrCb_ps_hlsl) {
      return nullptr;
    }

    merge_UV_ps_hlsl = compile_pixel_shader(SUNSHINE_ASSETS_DIR "/MergeUVPS.hlsl");
    if(!merge_UV_ps_hlsl) {
      return nullptr;
    }

    merge_UV_vs_hlsl = compile_vertex_shader(SUNSHINE_ASSETS_DIR "/MergeUVVS.hlsl");
    if(!merge_UV_vs_hlsl) {
      return nullptr;
    }

    BOOST_LOG(info) << "Compiled shaders"sv;
  }

  auto hwdevice = std::make_shared<hwdevice_t>(&hwdevices);

  auto ret = hwdevice->init(
    shared_from_this(),
    device.get(),
    device_ctx.get(),
    this->width, this->height,
    width, height,
    pix_fmt);

  if(ret) {
    return nullptr;
  }

  if(cursor.texture && hwdevice->set_cursor_texture(cursor.texture.get(), cursor.width, cursor.height)) {
    return nullptr;
  }

  hwdevices.emplace_back(hwdevice.get());

  return hwdevice;
}
}