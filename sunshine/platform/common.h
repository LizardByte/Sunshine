//
// Created by loki on 6/21/19.
//

#ifndef SUNSHINE_COMMON_H
#define SUNSHINE_COMMON_H

#include <string>
#include "sunshine/utility.h"

namespace platf {

struct img_t {
public:
  std::uint8_t *data  {};
  std::int32_t width  {};
  std::int32_t height {};

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
  virtual int reinit() = 0;
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

std::string get_local_ip();

std::unique_ptr<mic_t> microphone(std::uint32_t sample_rate);
std::shared_ptr<display_t> display();

input_t input();
void move_mouse(input_t &input, int deltaX, int deltaY);
void button_mouse(input_t &input, int button, bool release);
void scroll(input_t &input, int distance);
void keyboard(input_t &input, uint16_t modcode, bool release);

namespace gp {
void dpad_y(input_t &input, int button_state); // up pressed == -1, down pressed == 1, else 0
void dpad_x(input_t &input, int button_state); // left pressed == -1, right pressed == 1, else 0
void start(input_t &input, int button_down);
void back(input_t &input, int button_down);
void left_stick(input_t &input, int button_down);
void right_stick(input_t &input, int button_down);
void left_button(input_t &input, int button_down);
void right_button(input_t &input, int button_down);
void home(input_t &input, int button_down);
void a(input_t &input, int button_down);
void b(input_t &input, int button_down);
void x(input_t &input, int button_down);
void y(input_t &input, int button_down);
void left_trigger(input_t &input, std::uint8_t abs_z);
void right_trigger(input_t &input, std::uint8_t abs_z);
void left_stick_x(input_t &input, std::int16_t x);
void left_stick_y(input_t &input, std::int16_t y);
void right_stick_x(input_t &input, std::int16_t x);
void right_stick_y(input_t &input, std::int16_t y);
void sync(input_t &input);
}
}

#endif //SUNSHINE_COMMON_H
