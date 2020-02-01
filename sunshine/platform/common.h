//
// Created by loki on 6/21/19.
//

#ifndef SUNSHINE_COMMON_H
#define SUNSHINE_COMMON_H

#include <string>
#include "sunshine/utility.h"

namespace platf {
constexpr auto MAX_GAMEPADS = 2;

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

enum class capture_e : int {
  ok,
  reinit,
  timeout,
  error
};

class display_t {
public:
  virtual capture_e snapshot(img_t *img, bool cursor) = 0;
  virtual std::unique_ptr<img_t> alloc_img() = 0;

  virtual ~display_t() = default;
};

class mic_t {
public:
  virtual capture_e sample(std::vector<std::int16_t> &frame_buffer) = 0;

  virtual ~mic_t() = default;
};


void freeInput(void*);

using input_t = util::safe_ptr<void, freeInput>;

std::string get_mac_address(const std::string_view &address);

std::unique_ptr<mic_t> microphone(std::uint32_t sample_rate);
std::unique_ptr<display_t> display();

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
