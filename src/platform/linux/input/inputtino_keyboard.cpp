/**
 * @file src/platform/linux/input/inputtino_keyboard.cpp
 * @brief Definitions for inputtino keyboard input handling.
 */
// lib includes
#include <boost/locale.hpp>
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

  /**
   * US-layout map of printable ASCII -> {Moonlight/Windows VK code, needs Shift}.
   *
   * Lets us inject printable ASCII as real US-layout keystrokes instead of the
   * IBus/GTK "Ctrl+Shift+U + hex codepoint" entry method used below. That method
   * is only honored by IBus-enabled apps; in Chrome, KDE, Wayland-native apps and
   * password fields the leading Ctrl chord is interpreted as a shortcut (e.g.
   * Ctrl+U) and corrupts/clears the target field. Mobile soft keyboards (Gboard)
   * commit symbols like '@' as UTF-8 text, so this path is hit in normal typing.
   */
  static const std::map<char32_t, std::pair<short, bool>> ascii_to_vk = {
    {U' ', {0x20, false}},
    {U'0', {0x30, false}}, {U'1', {0x31, false}}, {U'2', {0x32, false}}, {U'3', {0x33, false}}, {U'4', {0x34, false}},
    {U'5', {0x35, false}}, {U'6', {0x36, false}}, {U'7', {0x37, false}}, {U'8', {0x38, false}}, {U'9', {0x39, false}},
    {U')', {0x30, true}}, {U'!', {0x31, true}}, {U'@', {0x32, true}}, {U'#', {0x33, true}}, {U'$', {0x34, true}},
    {U'%', {0x35, true}}, {U'^', {0x36, true}}, {U'&', {0x37, true}}, {U'*', {0x38, true}}, {U'(', {0x39, true}},
    {U'a', {0x41, false}}, {U'b', {0x42, false}}, {U'c', {0x43, false}}, {U'd', {0x44, false}}, {U'e', {0x45, false}},
    {U'f', {0x46, false}}, {U'g', {0x47, false}}, {U'h', {0x48, false}}, {U'i', {0x49, false}}, {U'j', {0x4A, false}},
    {U'k', {0x4B, false}}, {U'l', {0x4C, false}}, {U'm', {0x4D, false}}, {U'n', {0x4E, false}}, {U'o', {0x4F, false}},
    {U'p', {0x50, false}}, {U'q', {0x51, false}}, {U'r', {0x52, false}}, {U's', {0x53, false}}, {U't', {0x54, false}},
    {U'u', {0x55, false}}, {U'v', {0x56, false}}, {U'w', {0x57, false}}, {U'x', {0x58, false}}, {U'y', {0x59, false}},
    {U'z', {0x5A, false}},
    {U'A', {0x41, true}}, {U'B', {0x42, true}}, {U'C', {0x43, true}}, {U'D', {0x44, true}}, {U'E', {0x45, true}},
    {U'F', {0x46, true}}, {U'G', {0x47, true}}, {U'H', {0x48, true}}, {U'I', {0x49, true}}, {U'J', {0x4A, true}},
    {U'K', {0x4B, true}}, {U'L', {0x4C, true}}, {U'M', {0x4D, true}}, {U'N', {0x4E, true}}, {U'O', {0x4F, true}},
    {U'P', {0x50, true}}, {U'Q', {0x51, true}}, {U'R', {0x52, true}}, {U'S', {0x53, true}}, {U'T', {0x54, true}},
    {U'U', {0x55, true}}, {U'V', {0x56, true}}, {U'W', {0x57, true}}, {U'X', {0x58, true}}, {U'Y', {0x59, true}},
    {U'Z', {0x5A, true}},
    {U';', {0xBA, false}}, {U':', {0xBA, true}},
    {U'=', {0xBB, false}}, {U'+', {0xBB, true}},
    {U',', {0xBC, false}}, {U'<', {0xBC, true}},
    {U'-', {0xBD, false}}, {U'_', {0xBD, true}},
    {U'.', {0xBE, false}}, {U'>', {0xBE, true}},
    {U'/', {0xBF, false}}, {U'?', {0xBF, true}},
    {U'`', {0xC0, false}}, {U'~', {0xC0, true}},
    {U'[', {0xDB, false}}, {U'{', {0xDB, true}},
    {U'\\', {0xDC, false}}, {U'|', {0xDC, true}},
    {U']', {0xDD, false}}, {U'}', {0xDD, true}},
    {U'\'', {0xDE, false}}, {U'"', {0xDE, true}}
  };

  void unicode(input_raw_t *raw, char *utf8, int size) {
    if (!raw->keyboard) {
      return;
    }

    /* Reading input text as UTF-8, then converting to UTF-32 */
    auto utf8_str = boost::locale::conv::to_utf<wchar_t>(utf8, utf8 + size, "UTF-8");
    auto utf32_str = boost::locale::conv::utf_to_utf<char32_t>(utf8_str);

    for (const auto &codepoint : utf32_str) {
      /* Fast path: inject printable ASCII as a real US-layout keystroke, avoiding
         the IBus Ctrl+Shift+U method below (unsupported in many apps, where it
         corrupts the target field). */
      if (auto it = ascii_to_vk.find(codepoint); it != ascii_to_vk.end()) {
        auto [vk, needs_shift] = it->second;
        if (needs_shift) {
          (*raw->keyboard).press(0xA0);  // LEFTSHIFT
        }
        (*raw->keyboard).press(vk);
        (*raw->keyboard).release(vk);
        if (needs_shift) {
          (*raw->keyboard).release(0xA0);  // LEFTSHIFT
        }
        continue;
      }

      /* Fallback for non-ASCII (emoji, accented chars, ...): type the codepoint
         via the IBus/GTK <CTRL>+<SHIFT>+U hex-entry method. */
      auto hex_unicode = to_hex(std::basic_string<char32_t>(1, codepoint));
      BOOST_LOG(debug) << "Unicode, typing U+"sv << hex_unicode;

      (*raw->keyboard).press(0xA2);  // LEFTCTRL
      (*raw->keyboard).press(0xA0);  // LEFTSHIFT
      (*raw->keyboard).press(0x55);  // U
      (*raw->keyboard).release(0x55);  // U

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

      (*raw->keyboard).release(0xA0);  // LEFTSHIFT
      (*raw->keyboard).release(0xA2);  // LEFTCTRL
    }
  }
}  // namespace platf::keyboard
