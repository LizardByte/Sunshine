#include "sunshine/main.h"
#include "display.h"

namespace platf {
using namespace std::literals;
}

namespace platf::dxgi {
struct img_d3d_t : public platf::img_t {
  std::shared_ptr<platf::display_t> display;
  texture2d_t texture;

  ~img_d3d_t() override = default;
};

util::buffer_t<std::uint8_t> make_cursor_image(util::buffer_t<std::uint8_t> &&img_data, DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info)  {
  switch(shape_info.Type) {
    case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
    case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR:
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

      constexpr std::uint32_t black = 0xFF000000;
      constexpr std::uint32_t white = 0xFFFFFFFF;
      constexpr std::uint32_t transparent = 0;
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

    ctx->VideoProcessorSetStreamSourceRect(processor.get(), 1, TRUE, &rect_in);
    ctx->VideoProcessorSetStreamDestRect(processor.get(), 1, TRUE, &rect_out);
  }

  int set_cursor_texture(texture2d_t::pointer texture, LONG width, LONG height) {
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc = { 0, (D3D11_VPIV_DIMENSION)D3D11_VPIV_DIMENSION_TEXTURE2D, { 0, 0 } };

    video::processor_in_t::pointer processor_in_p;
    auto status = device->CreateVideoProcessorInputView(texture, processor_e.get(), &input_desc, &processor_in_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create cursor VideoProcessorInputView [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    cursor_in.reset(processor_in_p);

    cursor_width  = width;
    cursor_height = height;
    cursor_scaled_width = ((double)width) / in_width * out_width;
    cursor_scaled_height = ((double)height) / in_height * out_height;

    return 0;
  }

  int convert(platf::img_t &img_base) override {
    auto &img = (img_d3d_t&)img_base;

    auto it = texture_to_processor_in.find(img.texture.get());
    if(it == std::end(texture_to_processor_in)) {
      D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc = { 0, (D3D11_VPIV_DIMENSION)D3D11_VPIV_DIMENSION_TEXTURE2D, { 0, 0 } };

      video::processor_in_t::pointer processor_in_p;
      auto status = device->CreateVideoProcessorInputView(img.texture.get(), processor_e.get(), &input_desc, &processor_in_p);
      if(FAILED(status)) {
        BOOST_LOG(error) << "Failed to create VideoProcessorInputView [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }
      it = texture_to_processor_in.emplace(img.texture.get(), processor_in_p).first;
    }
    auto &processor_in = it->second;

    D3D11_VIDEO_PROCESSOR_STREAM stream[] {
      { TRUE, 0, 0, 0, 0, nullptr, processor_in.get(), nullptr },
      { TRUE, 0, 0, 0, 0, nullptr, cursor_in.get(), nullptr }
    };

    auto status = ctx->VideoProcessorBlt(processor.get(), processor_out.get(), 0, cursor_visible ? 2 : 1, stream);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed size and color conversion [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    return 0;
  }

  void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) override {
    colorspace |= (color_range >> 4);
    ctx->VideoProcessorSetOutputColorSpace(processor.get(), (D3D11_VIDEO_PROCESSOR_COLOR_SPACE*)&colorspace);
  }

  int init(
    std::shared_ptr<platf::display_t> display, device_t::pointer device_p, device_ctx_t::pointer device_ctx_p,
    int in_width, int in_height, int out_width, int out_height,
    pix_fmt_e pix_fmt
  ) {
    HRESULT status;

    cursor_visible = false;

    platf::hwdevice_t::img = &img;

    this->out_width  = out_width;
    this->out_height = out_height;
    this->in_width   = in_width;
    this->in_height  = in_height;

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
      D3D11_VIDEO_USAGE_OPTIMAL_QUALITY
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
    t.Format = pix_fmt == pix_fmt_e::nv12 ? DXGI_FORMAT_NV12 : DXGI_FORMAT_P010;
    t.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_VIDEO_ENCODER;

    dxgi::texture2d_t::pointer tex_p {};
    status = device_p->CreateTexture2D(&t, nullptr, &tex_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create video output texture [0x"sv << util::hex(status).to_string_view() << ']';
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
    status = device->CreateVideoProcessorOutputView(img.texture.get(), processor_e.get(), &output_desc, &processor_out_p);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to create VideoProcessorOutputView [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    processor_out.reset(processor_out_p);

    // Tell video processor alpha values need to be enabled
    ctx->VideoProcessorSetStreamAlpha(processor.get(), 1, TRUE, 1.0f);

    device_p->AddRef();
    data = device_p;
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

  img_d3d_t img;
  video::device_t device;
  video::ctx_t ctx;
  video::processor_enum_t processor_e;
  video::processor_t processor;
  video::processor_out_t processor_out;
  std::unordered_map<texture2d_t::pointer, video::processor_in_t> texture_to_processor_in;

  video::processor_in_t cursor_in;

  bool cursor_visible;

  LONG cursor_width, cursor_height;
  LONG cursor_scaled_width, cursor_scaled_height;

  LONG in_width, in_height;
  double out_width, out_height;

  std::vector<hwdevice_t*> *hwdevices_p;
};

capture_e display_vram_t::snapshot(platf::img_t *img_base, std::chrono::milliseconds timeout, bool cursor_visible) {
  auto img = (img_d3d_t*)img_base;

  HRESULT status;

  DXGI_OUTDUPL_FRAME_INFO frame_info;

  resource_t::pointer res_p {};
  auto capture_status = dup.next_frame(frame_info, timeout, &res_p);
  resource_t res{res_p};

  if (capture_status != capture_e::ok) {
    return capture_status;
  }

  const bool update_flag =
    frame_info.AccumulatedFrames != 0 || frame_info.LastPresentTime.QuadPart != 0 ||
    frame_info.LastMouseUpdateTime.QuadPart != 0 || frame_info.PointerShapeBufferSize > 0;

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
      BOOST_LOG(error) << "Failed to create dummy texture [0x"sv << util::hex(status).to_string_view() << ']';
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

  texture2d_t::pointer src_p {};
  status = res->QueryInterface(IID_ID3D11Texture2D, (void **)&src_p);

  if (FAILED(status)) {
    BOOST_LOG(error) << "Couldn't query interface [0x"sv << util::hex(status).to_string_view() << ']';
    return capture_e::error;
  }

  texture2d_t src { src_p };
  device_ctx->CopyResource(img->texture.get(), src.get());

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
  t.BindFlags = D3D11_BIND_RENDER_TARGET;

  dxgi::texture2d_t::pointer tex_p {};
  auto status = device->CreateTexture2D(&t, nullptr, &tex_p);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create img buf texture [0x"sv << util::hex(status).to_string_view() << ']';
    return nullptr;
  }

  img->data        = (std::uint8_t*)tex_p;
  img->row_pitch   = 0;
  img->pixel_pitch = 4;
  img->width       = 0;
  img->height      = 0;
  img->texture.reset(tex_p);
  img->display     = shared_from_this();

  return img;
}

int display_vram_t::dummy_img(platf::img_t *img_base) {
  auto img = (img_d3d_t*)img_base;

  img->row_pitch = width * 4;
  auto dummy_data = std::make_unique<int[]>(width * height);
  D3D11_SUBRESOURCE_DATA data {
    dummy_data.get(),
    (UINT)img->row_pitch,
    0
  };

  D3D11_TEXTURE2D_DESC t {};
  t.Width  = width;
  t.Height = height;
  t.MipLevels = 1;
  t.ArraySize = 1;
  t.SampleDesc.Count = 1;
  t.Usage = D3D11_USAGE_DEFAULT;
  t.Format = format;
  t.BindFlags = D3D11_BIND_RENDER_TARGET;

  dxgi::texture2d_t::pointer tex_p {};
  auto status = device->CreateTexture2D(&t, &data, &tex_p);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create dummy texture [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  img->data        = (std::uint8_t*)tex_p;
  img->texture.reset(tex_p);
  img->height      = height;
  img->width       = width;
  img->pixel_pitch = 4;

  return 0;
}

std::shared_ptr<platf::hwdevice_t> display_vram_t::make_hwdevice(int width, int height, pix_fmt_e pix_fmt) {
  if(pix_fmt != platf::pix_fmt_e::nv12 && pix_fmt != platf::pix_fmt_e::p010) {
    BOOST_LOG(error) << "display_vram_t doesn't support pixel format ["sv << (int)pix_fmt << ']';

    return nullptr;
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