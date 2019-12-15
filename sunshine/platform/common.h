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
void gamepad(input_t &input, const gamepad_state_t &gamepad_state);
}

#endif //SUNSHINE_COMMON_H
