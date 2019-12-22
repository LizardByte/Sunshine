//
// Created by loki on 6/21/19.
//

#ifndef SUNSHINE_COMMON_H
#define SUNSHINE_COMMON_H

#include <string>
#include "sunshine/utility.h"

namespace platf {

void freeDisplay(void*);
void freeImage(void*);
void freeAudio(void*);
void freeMic(void*);
void freeInput(void*);

using display_t = util::safe_ptr<void, freeDisplay>;
using img_t     = util::safe_ptr<void, freeImage>;
using mic_t     = util::safe_ptr<void, freeMic>;
using audio_t   = util::safe_ptr<void, freeAudio>;
using input_t   = util::safe_ptr<void, freeInput>;

struct gamepad_state_t {
  std::uint16_t buttonFlags;
  std::uint8_t lt;
  std::uint8_t rt;
  std::int16_t lsX;
  std::int16_t lsY;
  std::int16_t rsX;
  std::int16_t rsY;
};

std::string get_local_ip();

void terminate_process(std::uint64_t handle);

mic_t microphone();
audio_t audio(mic_t &mic, std::uint32_t sample_size);

display_t display();
img_t snapshot(display_t &display);
int32_t img_width(img_t &);
int32_t img_height(img_t &);

uint8_t *img_data(img_t &);
int16_t *audio_data(audio_t &);

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
