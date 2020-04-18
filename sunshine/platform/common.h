//
// Created by loki on 6/21/19.
//

#ifndef SUNSHINE_COMMON_H
#define SUNSHINE_COMMON_H

#include <string>
#include <mutex>
#include "sunshine/utility.h"

struct sockaddr;
namespace platf {
constexpr auto MAX_GAMEPADS = 32;

constexpr std::uint16_t DPAD_UP      = 0x0001;
constexpr std::uint16_t DPAD_DOWN    = 0x0002;
constexpr std::uint16_t DPAD_LEFT    = 0x0004;
constexpr std::uint16_t DPAD_RIGHT   = 0x0008;
constexpr std::uint16_t START        = 0x0010;
constexpr std::uint16_t BACK         = 0x0020;
constexpr std::uint16_t LEFT_STICK   = 0x0040;
constexpr std::uint16_t RIGHT_STICK  = 0x0080;
constexpr std::uint16_t LEFT_BUTTON  = 0x0100;
constexpr std::uint16_t RIGHT_BUTTON = 0x0200;
constexpr std::uint16_t HOME         = 0x0400;
constexpr std::uint16_t A            = 0x1000;
constexpr std::uint16_t B            = 0x2000;
constexpr std::uint16_t X            = 0x4000;
constexpr std::uint16_t Y            = 0x8000;

enum class dev_type_e {
  none,
  dxgi,
  unknown
};

enum class pix_fmt_e {
  yuv420p,
  yuv420p10,
  nv12,
  p010,
  unknown
};

struct gamepad_state_t {
  std::uint16_t buttonFlags;
  std::uint8_t lt;
  std::uint8_t rt;
  std::int16_t lsX;
  std::int16_t lsY;
  std::int16_t rsX;
  std::int16_t rsY;
};

class deinit_t {
public:
  virtual ~deinit_t() = default;
};

struct img_t {
public:
  std::uint8_t *data  {};
  std::int32_t width  {};
  std::int32_t height {};
  std::int32_t pixel_pitch {};
  std::int32_t row_pitch {};

  img_t() = default;
  img_t(const img_t&) = delete;
  img_t(img_t&&) = delete;

  virtual ~img_t() = default;
};

struct hwdevice_t {
  void *data {};
  platf::img_t *img {};

  virtual int convert(platf::img_t &img) {
    return -1;
  }

  virtual void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) {};

  virtual ~hwdevice_t() = default;
};

enum class capture_e : int {
  ok,
  reinit,
  timeout,
  error
};

class display_t {
public:
  virtual capture_e snapshot(img_t *img, std::chrono::milliseconds timeout, bool cursor) = 0;
  virtual std::shared_ptr<img_t> alloc_img() = 0;

  virtual int dummy_img(img_t *img) = 0;

  virtual std::shared_ptr<hwdevice_t> make_hwdevice(int width, int height, pix_fmt_e pix_fmt) {
    return std::make_shared<hwdevice_t>();
  }

  virtual ~display_t() = default;

  int width, height;
};

class mic_t {
public:
  virtual capture_e sample(std::vector<std::int16_t> &frame_buffer) = 0;

  virtual ~mic_t() = default;
};


void freeInput(void*);

using input_t = util::safe_ptr<void, freeInput>;

std::string get_mac_address(const std::string_view &address);

std::string from_sockaddr(const sockaddr *const);
std::pair<std::uint16_t, std::string> from_sockaddr_ex(const sockaddr *const);

std::unique_ptr<mic_t> microphone(std::uint32_t sample_rate);
std::shared_ptr<display_t> display(dev_type_e hwdevice_type);

input_t input();
void move_mouse(input_t &input, int deltaX, int deltaY);
void button_mouse(input_t &input, int button, bool release);
void scroll(input_t &input, int distance);
void keyboard(input_t &input, uint16_t modcode, bool release);
void gamepad(input_t &input, int nr, const gamepad_state_t &gamepad_state);

int alloc_gamepad(input_t &input, int nr);
void free_gamepad(input_t &input, int nr);

[[nodiscard]] std::unique_ptr<deinit_t> init();
}

#endif //SUNSHINE_COMMON_H
