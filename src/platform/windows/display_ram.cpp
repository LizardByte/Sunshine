#include "display.h"
#include "src/main.h"

namespace platf {
using namespace std::literals;
}

namespace platf::dxgi {
struct img_t : public ::platf::img_t {
  ~img_t() override {
    delete[] data;
    data = nullptr;
  }
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

  auto cursor_width  = width - cursor_skip_x - cursor_truncate_x;
  auto cursor_height = height - cursor_skip_y - cursor_truncate_y;

  if(cursor_height > height || cursor_width > width) {
    return;
  }

  auto img_skip_y = std::max(0, cursor.y);
  auto img_skip_x = std::max(0, cursor.x);

  auto cursor_img_data = cursor.img_data.data() + cursor_skip_y * pitch;

  int delta_height = std::min(cursor_height - cursor_truncate_y, std::max(0, img.height - img_skip_y));
  int delta_width  = std::min(cursor_width - cursor_truncate_x, std::max(0, img.width - img_skip_x));

  auto pixels_per_byte = width / pitch;
  auto bytes_per_row   = delta_width / pixels_per_byte;

  auto img_data = (int *)img.data;
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
  auto colors_out = (std::uint8_t *)&cursor_pixel;
  auto colors_in  = (std::uint8_t *)img_pixel_p;

  //TODO: When use of IDXGIOutput5 is implemented, support different color formats
  auto alpha = colors_out[3];
  if(alpha == 255) {
    *img_pixel_p = cursor_pixel;
  }
  else {
    colors_in[0] = colors_out[0] + (colors_in[0] * (255 - alpha) + 255 / 2) / 255;
    colors_in[1] = colors_out[1] + (colors_in[1] * (255 - alpha) + 255 / 2) / 255;
    colors_in[2] = colors_out[2] + (colors_in[2] * (255 - alpha) + 255 / 2) / 255;
  }
}

void apply_color_masked(int *img_pixel_p, int cursor_pixel) {
  //TODO: When use of IDXGIOutput5 is implemented, support different color formats
  auto alpha = ((std::uint8_t *)&cursor_pixel)[3];
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

  auto img_skip_y = std::max(0, cursor.y);
  auto img_skip_x = std::max(0, cursor.x);

  auto cursor_width  = width - cursor_skip_x - cursor_truncate_x;
  auto cursor_height = height - cursor_skip_y - cursor_truncate_y;

  if(cursor_height > height || cursor_width > width) {
    return;
  }

  auto cursor_img_data = (int *)&cursor.img_data[cursor_skip_y * pitch];

  int delta_height = std::min(cursor_height - cursor_truncate_y, std::max(0, img.height - img_skip_y));
  int delta_width  = std::min(cursor_width - cursor_truncate_x, std::max(0, img.width - img_skip_x));

  auto img_data = (int *)img.data;

  for(int i = 0; i < delta_height; ++i) {
    auto cursor_begin = &cursor_img_data[i * cursor.shape_info.Width + cursor_skip_x];
    auto cursor_end   = &cursor_begin[delta_width];

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

capture_e display_ram_t::capture(snapshot_cb_t &&snapshot_cb, std::shared_ptr<::platf::img_t> img, bool *cursor) {
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

capture_e display_ram_t::snapshot(::platf::img_t *img_base, std::chrono::milliseconds timeout, bool cursor_visible) {
  auto img = (img_t *)img_base;

  HRESULT status;

  DXGI_OUTDUPL_FRAME_INFO frame_info;

  resource_t::pointer res_p {};
  auto capture_status = dup.next_frame(frame_info, timeout, &res_p);
  resource_t res { res_p };

  if(capture_status != capture_e::ok) {
    return capture_status;
  }

  if(frame_info.PointerShapeBufferSize > 0) {
    auto &img_data = cursor.img_data;

    img_data.resize(frame_info.PointerShapeBufferSize);

    UINT dummy;
    status = dup.dup->GetFramePointerShape(img_data.size(), img_data.data(), &dummy, &cursor.shape_info);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to get new pointer shape [0x"sv << util::hex(status).to_string_view() << ']';

      return capture_e::error;
    }
  }

  if(frame_info.LastMouseUpdateTime.QuadPart) {
    cursor.x       = frame_info.PointerPosition.Position.x;
    cursor.y       = frame_info.PointerPosition.Position.y;
    cursor.visible = frame_info.PointerPosition.Visible;
  }

  // If frame has been updated
  if(frame_info.LastPresentTime.QuadPart != 0) {
    {
      texture2d_t src {};
      status = res->QueryInterface(IID_ID3D11Texture2D, (void **)&src);

      if(FAILED(status)) {
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
    if(FAILED(status)) {
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

  std::copy_n((std::uint8_t *)img_info.pData, height * img_info.RowPitch, (std::uint8_t *)img->data);

  if(cursor_visible && cursor.visible) {
    blend_cursor(cursor, *img);
  }

  return capture_e::ok;
}

std::shared_ptr<platf::img_t> display_ram_t::alloc_img() {
  auto img = std::make_shared<img_t>();

  img->pixel_pitch = 4;
  img->row_pitch   = img_info.RowPitch;
  img->width       = width;
  img->height      = height;
  img->data        = new std::uint8_t[img->row_pitch * height];

  return img;
}

int display_ram_t::dummy_img(platf::img_t *img) {
  return 0;
}

int display_ram_t::init(int framerate, const std::string &display_name) {
  if(display_base_t::init(framerate, display_name)) {
    return -1;
  }

  D3D11_TEXTURE2D_DESC t {};
  t.Width            = width;
  t.Height           = height;
  t.MipLevels        = 1;
  t.ArraySize        = 1;
  t.SampleDesc.Count = 1;
  t.Usage            = D3D11_USAGE_STAGING;
  t.Format           = format;
  t.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;

  auto status = device->CreateTexture2D(&t, nullptr, &texture);

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
} // namespace platf::dxgi
