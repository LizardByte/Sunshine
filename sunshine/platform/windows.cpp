#include "common.h"

namespace platf {
using namespace std::literals;
std::string get_local_ip() { return "192.168.0.119"s; }

class dxi_display_t : public display_t {
  std::unique_ptr<img_t> snapshot(bool cursor) override {
    return nullptr;
  }
};

class dummy_mic_t : public mic_t {
  std::vector<std::int16_t> sample(std::size_t sample_size) override {
    std::vector<std::int16_t> sample_buf;
    sample_buf.resize(sample_size);

    return sample_buf;
  }
};

std::unique_ptr<mic_t> microphone() {
  return std::unique_ptr<mic_t> { new dummy_mic_t {} };
}
std::unique_ptr<display_t> display() {
  return std::unique_ptr<display_t> { new dxi_display_t {} };
}

input_t input() {
  return nullptr;
}

void move_mouse(input_t &input, int deltaX, int deltaY) {}
void button_mouse(input_t &input, int button, bool release) {}
void scroll(input_t &input, int distance) {}
void keyboard(input_t &input, uint16_t modcode, bool release) {}

namespace gp {
void dpad_y(input_t &input, int button_state) {} // up pressed == -1, down pressed == 1, else 0
void dpad_x(input_t &input, int button_state) {} // left pressed == -1, right pressed == 1, else 0
void start(input_t &input, int button_down) {}
void back(input_t &input, int button_down) {}
void left_stick(input_t &input, int button_down) {}
void right_stick(input_t &input, int button_down) {}
void left_button(input_t &input, int button_down) {}
void right_button(input_t &input, int button_down) {}
void home(input_t &input, int button_down) {}
void a(input_t &input, int button_down) {}
void b(input_t &input, int button_down) {}
void x(input_t &input, int button_down) {}
void y(input_t &input, int button_down) {}
void left_trigger(input_t &input, std::uint8_t abs_z) {}
void right_trigger(input_t &input, std::uint8_t abs_z) {}
void left_stick_x(input_t &input, std::int16_t x) {}
void left_stick_y(input_t &input, std::int16_t y) {}
void right_stick_x(input_t &input, std::int16_t x) {}
void right_stick_y(input_t &input, std::int16_t y) {}
void sync(input_t &input) {}
}

void freeInput(void*) {}
}
