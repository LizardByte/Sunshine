//
// Created by loki on 6/21/19.
//

#ifndef SUNSHINE_COMMON_H
#define SUNSHINE_COMMON_H

#include <string>
#include <utility.h>

namespace platf {

void freeDisplay(void*);
void freeImage(void*);
void freeAudio(void*);
void freeMic(void*);
void freeGamePad(void*);

using display_t = util::safe_ptr<void, freeDisplay>;
using img_t     = util::safe_ptr<void, freeImage>;
using mic_t     = util::safe_ptr<void, freeMic>;
using audio_t   = util::safe_ptr<void, freeAudio>;
using gamepad_t = util::safe_ptr<void, freeGamePad>;

struct gamepad_state_t {
  std::uint16_t buttonFlags;
  std::uint8_t lt;
  std::uint8_t rt;
  std::uint16_t lsX;
  std::uint16_t lsY;
  std::uint16_t rsX;
  std::uint16_t rsY;
};

std::string get_local_ip();
display_t display();
img_t snapshot(display_t &display);
mic_t microphone();
audio_t audio(mic_t &mic, std::uint32_t sample_size);
gamepad_t gamepad();

int32_t img_width(img_t &);
int32_t img_height(img_t &);

uint8_t *img_data(img_t &);
int16_t *audio_data(audio_t &);

void move_mouse(display_t::element_type *display, int deltaX, int deltaY);
void button_mouse(display_t::element_type *display, int button, bool release);
void scroll(display_t::element_type *display, int distance);
void keyboard(display_t::element_type *display, uint16_t modcode, bool release);
void gamepad_event(gamepad_t &gamepad, const gamepad_state_t &gamepad_state);
}

#endif //SUNSHINE_COMMON_H
