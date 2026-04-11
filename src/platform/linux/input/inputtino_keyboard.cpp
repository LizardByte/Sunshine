/**
 * @file src/platform/linux/input/inputtino_keyboard.cpp
 * @brief Definitions for inputtino keyboard input handling.
 */
// lib includes
#include <inputtino/input.hpp>
#include <libevdev/libevdev.h>

// local includes
#include "inputtino_common.h"
#include "inputtino_keyboard.h"
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"

using namespace std::literals;

namespace platf::keyboard {

  bool utf8_to_utf32(const char *utf8, int size, std::u32string &output) {
    output.clear();
    output.reserve(size);

    for (int i = 0; i < size;) {
      const auto lead = static_cast<unsigned char>(utf8[i]);
      uint32_t code_point = 0;
      int continuation_bytes = 0;

      if (lead <= 0x7F) {
        code_point = lead;
      } else if ((lead & 0xE0) == 0xC0) {
        code_point = lead & 0x1F;
        continuation_bytes = 1;
      } else if ((lead & 0xF0) == 0xE0) {
        code_point = lead & 0x0F;
        continuation_bytes = 2;
      } else if ((lead & 0xF8) == 0xF0) {
        code_point = lead & 0x07;
        continuation_bytes = 3;
      } else {
        return false;
      }

      if (i + continuation_bytes >= size) {
        return false;
      }

      for (int j = 1; j <= continuation_bytes; ++j) {
        const auto continuation = static_cast<unsigned char>(utf8[i + j]);
        if ((continuation & 0xC0) != 0x80) {
          return false;
        }
        code_point = (code_point << 6) | (continuation & 0x3F);
      }

      if ((continuation_bytes == 1 && code_point < 0x80) ||
          (continuation_bytes == 2 && code_point < 0x800) ||
          (continuation_bytes == 3 && code_point < 0x10000) ||
          (code_point >= 0xD800 && code_point <= 0xDFFF) ||
         code_point > 0x10FFFF) {
        return false;
      }

      output.push_back(static_cast<char32_t>(code_point));
      i += continuation_bytes + 1;
    }

    return true;
  }

  /**
   * Takes an UTF-32 encoded string and returns a hex string representation of the bytes (uppercase)
   *
   * ex: ['👱'] = "1F471" // see UTF encoding at https://www.compart.com/en/unicode/U+1F471
   *
   * adapted from: https://stackoverflow.com/a/7639754
   */
  std::string to_hex(const std::basic_string<char32_t> &str) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (const auto &ch : str) {
      ss << static_cast<uint32_t>(ch);
    }

    std::string hex_unicode(ss.str());
    std::ranges::transform(hex_unicode, hex_unicode.begin(), ::toupper);
    return hex_unicode;
  }

  /**
   * A map of linux scan code -> Moonlight keyboard code
   */
  static const std::map<short, short> key_mappings = {
    {KEY_BACKSPACE, 0x08},
    {KEY_TAB, 0x09},
    {KEY_ENTER, 0x0D},
    {KEY_LEFTSHIFT, 0x10},
    {KEY_LEFTCTRL, 0x11},
    {KEY_CAPSLOCK, 0x14},
    {KEY_ESC, 0x1B},
    {KEY_SPACE, 0x20},
    {KEY_PAGEUP, 0x21},
    {KEY_PAGEDOWN, 0x22},
    {KEY_END, 0x23},
    {KEY_HOME, 0x24},
    {KEY_LEFT, 0x25},
    {KEY_UP, 0x26},
    {KEY_RIGHT, 0x27},
    {KEY_DOWN, 0x28},
    {KEY_SYSRQ, 0x2C},
    {KEY_INSERT, 0x2D},
    {KEY_DELETE, 0x2E},
    {KEY_0, 0x30},
    {KEY_1, 0x31},
    {KEY_2, 0x32},
    {KEY_3, 0x33},
    {KEY_4, 0x34},
    {KEY_5, 0x35},
    {KEY_6, 0x36},
    {KEY_7, 0x37},
    {KEY_8, 0x38},
    {KEY_9, 0x39},
    {KEY_A, 0x41},
    {KEY_B, 0x42},
    {KEY_C, 0x43},
    {KEY_D, 0x44},
    {KEY_E, 0x45},
    {KEY_F, 0x46},
    {KEY_G, 0x47},
    {KEY_H, 0x48},
    {KEY_I, 0x49},
    {KEY_J, 0x4A},
    {KEY_K, 0x4B},
    {KEY_L, 0x4C},
    {KEY_M, 0x4D},
    {KEY_N, 0x4E},
    {KEY_O, 0x4F},
    {KEY_P, 0x50},
    {KEY_Q, 0x51},
    {KEY_R, 0x52},
    {KEY_S, 0x53},
    {KEY_T, 0x54},
    {KEY_U, 0x55},
    {KEY_V, 0x56},
    {KEY_W, 0x57},
    {KEY_X, 0x58},
    {KEY_Y, 0x59},
    {KEY_Z, 0x5A},
    {KEY_LEFTMETA, 0x5B},
    {KEY_RIGHTMETA, 0x5C},
    {KEY_KP0, 0x60},
    {KEY_KP1, 0x61},
    {KEY_KP2, 0x62},
    {KEY_KP3, 0x63},
    {KEY_KP4, 0x64},
    {KEY_KP5, 0x65},
    {KEY_KP6, 0x66},
    {KEY_KP7, 0x67},
    {KEY_KP8, 0x68},
    {KEY_KP9, 0x69},
    {KEY_KPASTERISK, 0x6A},
    {KEY_KPPLUS, 0x6B},
    {KEY_KPMINUS, 0x6D},
    {KEY_KPDOT, 0x6E},
    {KEY_KPSLASH, 0x6F},
    {KEY_F1, 0x70},
    {KEY_F2, 0x71},
    {KEY_F3, 0x72},
    {KEY_F4, 0x73},
    {KEY_F5, 0x74},
    {KEY_F6, 0x75},
    {KEY_F7, 0x76},
    {KEY_F8, 0x77},
    {KEY_F9, 0x78},
    {KEY_F10, 0x79},
    {KEY_F11, 0x7A},
    {KEY_F12, 0x7B},
    {KEY_F13, 0x7C},
    {KEY_F14, 0x7D},
    {KEY_F15, 0x7E},
    {KEY_F16, 0x7F},
    {KEY_F17, 0x80},
    {KEY_F18, 0x81},
    {KEY_F19, 0x82},
    {KEY_F20, 0x83},
    {KEY_F21, 0x84},
    {KEY_F22, 0x85},
    {KEY_F23, 0x86},
    {KEY_F24, 0x87},
    {KEY_NUMLOCK, 0x90},
    {KEY_SCROLLLOCK, 0x91},
    {KEY_LEFTSHIFT, 0xA0},
    {KEY_RIGHTSHIFT, 0xA1},
    {KEY_LEFTCTRL, 0xA2},
    {KEY_RIGHTCTRL, 0xA3},
    {KEY_LEFTALT, 0xA4},
    {KEY_RIGHTALT, 0xA5},
    {KEY_SEMICOLON, 0xBA},
    {KEY_EQUAL, 0xBB},
    {KEY_COMMA, 0xBC},
    {KEY_MINUS, 0xBD},
    {KEY_DOT, 0xBE},
    {KEY_SLASH, 0xBF},
    {KEY_GRAVE, 0xC0},
    {KEY_LEFTBRACE, 0xDB},
    {KEY_BACKSLASH, 0xDC},
    {KEY_RIGHTBRACE, 0xDD},
    {KEY_APOSTROPHE, 0xDE},
    {KEY_102ND, 0xE2}
  };

  void update(input_raw_t *raw, uint16_t modcode, bool release, uint8_t flags) {
    if (raw->keyboard) {
      if (release) {
        (*raw->keyboard).release(modcode);
      } else {
        (*raw->keyboard).press(modcode);
      }
    }
  }

  void unicode(input_raw_t *raw, char *utf8, int size) {
    if (raw->keyboard) {
      std::u32string utf32_str;
      if (!utf8_to_utf32(utf8, size, utf32_str)) {
        BOOST_LOG(warning) << "Failed to decode UTF-8 keyboard input";
        return;
      }

      /* To HEX string */
      auto hex_unicode = to_hex(utf32_str);
      BOOST_LOG(debug) << "Unicode, typing U+"sv << hex_unicode;

      /* pressing <CTRL> + <SHIFT> + U */
      (*raw->keyboard).press(0xA2);  // LEFTCTRL
      (*raw->keyboard).press(0xA0);  // LEFTSHIFT
      (*raw->keyboard).press(0x55);  // U
      (*raw->keyboard).release(0x55);  // U

      /* input each HEX character */
      for (auto &ch : hex_unicode) {
        auto key_str = "KEY_"s + ch;
        auto keycode = libevdev_event_code_from_name(EV_KEY, key_str.c_str());
        auto wincode = key_mappings.find(keycode);
        if (keycode == -1 || wincode == key_mappings.end()) {
          BOOST_LOG(warning) << "Unicode, unable to find keycode for: "sv << ch;
        } else {
          (*raw->keyboard).press(wincode->second);
          (*raw->keyboard).release(wincode->second);
        }
      }

      /* releasing <SHIFT> and <CTRL> */
      (*raw->keyboard).release(0xA0);  // LEFTSHIFT
      (*raw->keyboard).release(0xA2);  // LEFTCTRL
    }
  }
}  // namespace platf::keyboard
