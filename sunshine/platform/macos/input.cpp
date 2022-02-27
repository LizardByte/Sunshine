#import <Carbon/Carbon.h>
#include <mach/mach.h>
#include <mach/mach_time.h>

#include "sunshine/main.h"
#include "sunshine/platform/common.h"
#include "sunshine/utility.h"

// Delay for a double click
// FIXME: we probably want to make this configurable
#define MULTICLICK_DELAY_NS 500000000

namespace platf {
using namespace std::literals;

struct macos_input_t {
public:
  CGDirectDisplayID display;
  CGFloat displayScaling;
  CGEventSourceRef source;

  // keyboard related stuff
  CGEventRef kb_event;
  CGEventFlags kb_flags;

  // mouse related stuff
  CGEventRef mouse_event;          // mouse event source
  bool mouse_down[3];              // mouse button status
  uint64_t last_mouse_event[3][2]; // timestamp of last mouse events
};

// A struct to hold a Windows keycode to Mac virtual keycode mapping.
struct KeyCodeMap {
  int win_keycode;
  int mac_keycode;
};

// Customized less operator for using std::lower_bound() on a KeyCodeMap array.
bool operator<(const KeyCodeMap &a, const KeyCodeMap &b) {
  return a.win_keycode < b.win_keycode;
}

// clang-format off
const KeyCodeMap kKeyCodesMap[] = {
  { 0x08 /* VKEY_BACK */,                      kVK_Delete              },
  { 0x09 /* VKEY_TAB */,                       kVK_Tab                 },
  { 0x0A /* VKEY_BACKTAB */,                   0x21E4                  },
  { 0x0C /* VKEY_CLEAR */,                     kVK_ANSI_KeypadClear    },
  { 0x0D /* VKEY_RETURN */,                    kVK_Return              },
  { 0x10 /* VKEY_SHIFT */,                     kVK_Shift               },
  { 0x11 /* VKEY_CONTROL */,                   kVK_Control             },
  { 0x12 /* VKEY_MENU */,                      kVK_Option              },
  { 0x13 /* VKEY_PAUSE */,                     -1                      },
  { 0x14 /* VKEY_CAPITAL */,                   kVK_CapsLock            },
  { 0x15 /* VKEY_KANA */,                      kVK_JIS_Kana            },
  { 0x15 /* VKEY_HANGUL */,                    -1                      },
  { 0x17 /* VKEY_JUNJA */,                     -1                      },
  { 0x18 /* VKEY_FINAL */,                     -1                      },
  { 0x19 /* VKEY_HANJA */,                     -1                      },
  { 0x19 /* VKEY_KANJI */,                     -1                      },
  { 0x1B /* VKEY_ESCAPE */,                    kVK_Escape              },
  { 0x1C /* VKEY_CONVERT */,                   -1                      },
  { 0x1D /* VKEY_NONCONVERT */,                -1                      },
  { 0x1E /* VKEY_ACCEPT */,                    -1                      },
  { 0x1F /* VKEY_MODECHANGE */,                -1                      },
  { 0x20 /* VKEY_SPACE */,                     kVK_Space               },
  { 0x21 /* VKEY_PRIOR */,                     kVK_PageUp              },
  { 0x22 /* VKEY_NEXT */,                      kVK_PageDown            },
  { 0x23 /* VKEY_END */,                       kVK_End                 },
  { 0x24 /* VKEY_HOME */,                      kVK_Home                },
  { 0x25 /* VKEY_LEFT */,                      kVK_LeftArrow           },
  { 0x26 /* VKEY_UP */,                        kVK_UpArrow             },
  { 0x27 /* VKEY_RIGHT */,                     kVK_RightArrow          },
  { 0x28 /* VKEY_DOWN */,                      kVK_DownArrow           },
  { 0x29 /* VKEY_SELECT */,                    -1                      },
  { 0x2A /* VKEY_PRINT */,                     -1                      },
  { 0x2B /* VKEY_EXECUTE */,                   -1                      },
  { 0x2C /* VKEY_SNAPSHOT */,                  -1                      },
  { 0x2D /* VKEY_INSERT */,                    kVK_Help                },
  { 0x2E /* VKEY_DELETE */,                    kVK_ForwardDelete       },
  { 0x2F /* VKEY_HELP */,                      kVK_Help                },
  { 0x30 /* VKEY_0 */,                         kVK_ANSI_0              },
  { 0x31 /* VKEY_1 */,                         kVK_ANSI_1              },
  { 0x32 /* VKEY_2 */,                         kVK_ANSI_2              },
  { 0x33 /* VKEY_3 */,                         kVK_ANSI_3              },
  { 0x34 /* VKEY_4 */,                         kVK_ANSI_4              },
  { 0x35 /* VKEY_5 */,                         kVK_ANSI_5              },
  { 0x36 /* VKEY_6 */,                         kVK_ANSI_6              },
  { 0x37 /* VKEY_7 */,                         kVK_ANSI_7              },
  { 0x38 /* VKEY_8 */,                         kVK_ANSI_8              },
  { 0x39 /* VKEY_9 */,                         kVK_ANSI_9              },
  { 0x41 /* VKEY_A */,                         kVK_ANSI_A              },
  { 0x42 /* VKEY_B */,                         kVK_ANSI_B              },
  { 0x43 /* VKEY_C */,                         kVK_ANSI_C              },
  { 0x44 /* VKEY_D */,                         kVK_ANSI_D              },
  { 0x45 /* VKEY_E */,                         kVK_ANSI_E              },
  { 0x46 /* VKEY_F */,                         kVK_ANSI_F              },
  { 0x47 /* VKEY_G */,                         kVK_ANSI_G              },
  { 0x48 /* VKEY_H */,                         kVK_ANSI_H              },
  { 0x49 /* VKEY_I */,                         kVK_ANSI_I              },
  { 0x4A /* VKEY_J */,                         kVK_ANSI_J              },
  { 0x4B /* VKEY_K */,                         kVK_ANSI_K              },
  { 0x4C /* VKEY_L */,                         kVK_ANSI_L              },
  { 0x4D /* VKEY_M */,                         kVK_ANSI_M              },
  { 0x4E /* VKEY_N */,                         kVK_ANSI_N              },
  { 0x4F /* VKEY_O */,                         kVK_ANSI_O              },
  { 0x50 /* VKEY_P */,                         kVK_ANSI_P              },
  { 0x51 /* VKEY_Q */,                         kVK_ANSI_Q              },
  { 0x52 /* VKEY_R */,                         kVK_ANSI_R              },
  { 0x53 /* VKEY_S */,                         kVK_ANSI_S              },
  { 0x54 /* VKEY_T */,                         kVK_ANSI_T              },
  { 0x55 /* VKEY_U */,                         kVK_ANSI_U              },
  { 0x56 /* VKEY_V */,                         kVK_ANSI_V              },
  { 0x57 /* VKEY_W */,                         kVK_ANSI_W              },
  { 0x58 /* VKEY_X */,                         kVK_ANSI_X              },
  { 0x59 /* VKEY_Y */,                         kVK_ANSI_Y              },
  { 0x5A /* VKEY_Z */,                         kVK_ANSI_Z              },
  { 0x5B /* VKEY_LWIN */,                      kVK_Command             },
  { 0x5C /* VKEY_RWIN */,                      kVK_RightCommand        },
  { 0x5D /* VKEY_APPS */,                      kVK_RightCommand        },
  { 0x5F /* VKEY_SLEEP */,                     -1                      },
  { 0x60 /* VKEY_NUMPAD0 */,                   kVK_ANSI_Keypad0        },
  { 0x61 /* VKEY_NUMPAD1 */,                   kVK_ANSI_Keypad1        },
  { 0x62 /* VKEY_NUMPAD2 */,                   kVK_ANSI_Keypad2        },
  { 0x63 /* VKEY_NUMPAD3 */,                   kVK_ANSI_Keypad3        },
  { 0x64 /* VKEY_NUMPAD4 */,                   kVK_ANSI_Keypad4        },
  { 0x65 /* VKEY_NUMPAD5 */,                   kVK_ANSI_Keypad5        },
  { 0x66 /* VKEY_NUMPAD6 */,                   kVK_ANSI_Keypad6        },
  { 0x67 /* VKEY_NUMPAD7 */,                   kVK_ANSI_Keypad7        },
  { 0x68 /* VKEY_NUMPAD8 */,                   kVK_ANSI_Keypad8        },
  { 0x69 /* VKEY_NUMPAD9 */,                   kVK_ANSI_Keypad9        },
  { 0x6A /* VKEY_MULTIPLY */,                  kVK_ANSI_KeypadMultiply },
  { 0x6B /* VKEY_ADD */,                       kVK_ANSI_KeypadPlus     },
  { 0x6C /* VKEY_SEPARATOR */,                 -1                      },
  { 0x6D /* VKEY_SUBTRACT */,                  kVK_ANSI_KeypadMinus    },
  { 0x6E /* VKEY_DECIMAL */,                   kVK_ANSI_KeypadDecimal  },
  { 0x6F /* VKEY_DIVIDE */,                    kVK_ANSI_KeypadDivide   },
  { 0x70 /* VKEY_F1 */,                        kVK_F1                  },
  { 0x71 /* VKEY_F2 */,                        kVK_F2                  },
  { 0x72 /* VKEY_F3 */,                        kVK_F3                  },
  { 0x73 /* VKEY_F4 */,                        kVK_F4                  },
  { 0x74 /* VKEY_F5 */,                        kVK_F5                  },
  { 0x75 /* VKEY_F6 */,                        kVK_F6                  },
  { 0x76 /* VKEY_F7 */,                        kVK_F7                  },
  { 0x77 /* VKEY_F8 */,                        kVK_F8                  },
  { 0x78 /* VKEY_F9 */,                        kVK_F9                  },
  { 0x79 /* VKEY_F10 */,                       kVK_F10                 },
  { 0x7A /* VKEY_F11 */,                       kVK_F11                 },
  { 0x7B /* VKEY_F12 */,                       kVK_F12                 },
  { 0x7C /* VKEY_F13 */,                       kVK_F13                 },
  { 0x7D /* VKEY_F14 */,                       kVK_F14                 },
  { 0x7E /* VKEY_F15 */,                       kVK_F15                 },
  { 0x7F /* VKEY_F16 */,                       kVK_F16                 },
  { 0x80 /* VKEY_F17 */,                       kVK_F17                 },
  { 0x81 /* VKEY_F18 */,                       kVK_F18                 },
  { 0x82 /* VKEY_F19 */,                       kVK_F19                 },
  { 0x83 /* VKEY_F20 */,                       kVK_F20                 },
  { 0x84 /* VKEY_F21 */,                       -1                      },
  { 0x85 /* VKEY_F22 */,                       -1                      },
  { 0x86 /* VKEY_F23 */,                       -1                      },
  { 0x87 /* VKEY_F24 */,                       -1                      },
  { 0x90 /* VKEY_NUMLOCK */,                   -1                      },
  { 0x91 /* VKEY_SCROLL */,                    -1                      },
  { 0xA0 /* VKEY_LSHIFT */,                    kVK_Shift               },
  { 0xA1 /* VKEY_RSHIFT */,                    kVK_RightShift          },
  { 0xA2 /* VKEY_LCONTROL */,                  kVK_Control             },
  { 0xA3 /* VKEY_RCONTROL */,                  kVK_RightControl        },
  { 0xA4 /* VKEY_LMENU */,                     kVK_Option              },
  { 0xA5 /* VKEY_RMENU */,                     kVK_RightOption         },
  { 0xA6 /* VKEY_BROWSER_BACK */,              -1                      },
  { 0xA7 /* VKEY_BROWSER_FORWARD */,           -1                      },
  { 0xA8 /* VKEY_BROWSER_REFRESH */,           -1                      },
  { 0xA9 /* VKEY_BROWSER_STOP */,              -1                      },
  { 0xAA /* VKEY_BROWSER_SEARCH */,            -1                      },
  { 0xAB /* VKEY_BROWSER_FAVORITES */,         -1                      },
  { 0xAC /* VKEY_BROWSER_HOME */,              -1                      },
  { 0xAD /* VKEY_VOLUME_MUTE */,               -1                      },
  { 0xAE /* VKEY_VOLUME_DOWN */,               -1                      },
  { 0xAF /* VKEY_VOLUME_UP */,                 -1                      },
  { 0xB0 /* VKEY_MEDIA_NEXT_TRACK */,          -1                      },
  { 0xB1 /* VKEY_MEDIA_PREV_TRACK */,          -1                      },
  { 0xB2 /* VKEY_MEDIA_STOP */,                -1                      },
  { 0xB3 /* VKEY_MEDIA_PLAY_PAUSE */,          -1                      },
  { 0xB4 /* VKEY_MEDIA_LAUNCH_MAIL */,         -1                      },
  { 0xB5 /* VKEY_MEDIA_LAUNCH_MEDIA_SELECT */, -1                      },
  { 0xB6 /* VKEY_MEDIA_LAUNCH_APP1 */,         -1                      },
  { 0xB7 /* VKEY_MEDIA_LAUNCH_APP2 */,         -1                      },
  { 0xBA /* VKEY_OEM_1 */,                     kVK_ANSI_Semicolon      },
  { 0xBB /* VKEY_OEM_PLUS */,                  kVK_ANSI_Equal          },
  { 0xBC /* VKEY_OEM_COMMA */,                 kVK_ANSI_Comma          },
  { 0xBD /* VKEY_OEM_MINUS */,                 kVK_ANSI_Minus          },
  { 0xBE /* VKEY_OEM_PERIOD */,                kVK_ANSI_Period         },
  { 0xBF /* VKEY_OEM_2 */,                     kVK_ANSI_Slash          },
  { 0xC0 /* VKEY_OEM_3 */,                     kVK_ANSI_Grave          },
  { 0xDB /* VKEY_OEM_4 */,                     kVK_ANSI_LeftBracket    },
  { 0xDC /* VKEY_OEM_5 */,                     kVK_ANSI_Backslash      },
  { 0xDD /* VKEY_OEM_6 */,                     kVK_ANSI_RightBracket   },
  { 0xDE /* VKEY_OEM_7 */,                     kVK_ANSI_Quote          },
  { 0xDF /* VKEY_OEM_8 */,                     -1                      },
  { 0xE2 /* VKEY_OEM_102 */,                   -1                      },
  { 0xE5 /* VKEY_PROCESSKEY */,                -1                      },
  { 0xE7 /* VKEY_PACKET */,                    -1                      },
  { 0xF6 /* VKEY_ATTN */,                      -1                      },
  { 0xF7 /* VKEY_CRSEL */,                     -1                      },
  { 0xF8 /* VKEY_EXSEL */,                     -1                      },
  { 0xF9 /* VKEY_EREOF */,                     -1                      },
  { 0xFA /* VKEY_PLAY */,                      -1                      },
  { 0xFB /* VKEY_ZOOM */,                      -1                      },
  { 0xFC /* VKEY_NONAME */,                    -1                      },
  { 0xFD /* VKEY_PA1 */,                       -1                      },
  { 0xFE /* VKEY_OEM_CLEAR */,                 kVK_ANSI_KeypadClear    }
};
// clang-format on

int keysym(int keycode) {
  KeyCodeMap key_map;

  key_map.win_keycode        = keycode;
  const KeyCodeMap *temp_map = std::lower_bound(
    kKeyCodesMap, kKeyCodesMap + sizeof(kKeyCodesMap) / sizeof(kKeyCodesMap[0]), key_map);

  if(temp_map >= kKeyCodesMap + sizeof(kKeyCodesMap) / sizeof(kKeyCodesMap[0]) ||
     temp_map->win_keycode != keycode || temp_map->mac_keycode == -1) {
    return -1;
  }

  return temp_map->mac_keycode;
}

void keyboard(input_t &input, uint16_t modcode, bool release) {
  auto key = keysym(modcode);

  BOOST_LOG(debug) << "got keycode: 0x"sv << std::hex << modcode << ", translated to: 0x" << std::hex << key << ", release:" << release;

  if(key < 0) {
    return;
  }

  auto macos_input = ((macos_input_t *)input.get());
  auto event       = macos_input->kb_event;

  if(key == kVK_Shift || key == kVK_RightShift ||
     key == kVK_Command || key == kVK_RightCommand ||
     key == kVK_Option || key == kVK_RightOption ||
     key == kVK_Control || key == kVK_RightControl) {

    CGEventFlags mask;

    switch(key) {
    case kVK_Shift:
    case kVK_RightShift:
      mask = kCGEventFlagMaskShift;
      break;
    case kVK_Command:
    case kVK_RightCommand:
      mask = kCGEventFlagMaskCommand;
      break;
    case kVK_Option:
    case kVK_RightOption:
      mask = kCGEventFlagMaskAlternate;
      break;
    case kVK_Control:
    case kVK_RightControl:
      mask = kCGEventFlagMaskControl;
      break;
    }

    macos_input->kb_flags = release ? macos_input->kb_flags & ~mask : macos_input->kb_flags | mask;
    CGEventSetType(event, kCGEventFlagsChanged);
    CGEventSetFlags(event, macos_input->kb_flags);
  }
  else {
    CGEventSetIntegerValueField(event, kCGKeyboardEventKeycode, key);
    CGEventSetType(event, release ? kCGEventKeyUp : kCGEventKeyDown);
  }

  CGEventPost(kCGHIDEventTap, event);
}

int alloc_gamepad(input_t &input, int nr, rumble_queue_t rumble_queue) {
  BOOST_LOG(info) << "alloc_gamepad: Gamepad not yet implemented for MacOS."sv;
  return -1;
}

void free_gamepad(input_t &input, int nr) {
  BOOST_LOG(info) << "free_gamepad: Gamepad not yet implemented for MacOS."sv;
}

void gamepad(input_t &input, int nr, const gamepad_state_t &gamepad_state) {
  BOOST_LOG(info) << "gamepad: Gamepad not yet implemented for MacOS."sv;
}

// returns current mouse location:
inline CGPoint get_mouse_loc(input_t &input) {
  return CGEventGetLocation(((macos_input_t *)input.get())->mouse_event);
}

void post_mouse(input_t &input, CGMouseButton button, CGEventType type, CGPoint location, int click_count) {
  BOOST_LOG(debug) << "mouse_event: "sv << button << ", type: "sv << type << ", location:"sv << location.x << ":"sv << location.y << " click_count: "sv << click_count;

  auto macos_input = (macos_input_t *)input.get();
  auto display     = macos_input->display;
  auto event       = macos_input->mouse_event;

  if(location.x < 0)
    location.x = 0;
  if(location.x >= CGDisplayPixelsWide(display))
    location.x = CGDisplayPixelsWide(display) - 1;

  if(location.y < 0)
    location.y = 0;
  if(location.y >= CGDisplayPixelsHigh(display))
    location.y = CGDisplayPixelsHigh(display) - 1;

  CGEventSetType(event, type);
  CGEventSetLocation(event, location);
  CGEventSetIntegerValueField(event, kCGMouseEventButtonNumber, button);
  CGEventSetIntegerValueField(event, kCGMouseEventClickState, click_count);

  CGEventPost(kCGHIDEventTap, event);

  // For why this is here, see:
  // https://stackoverflow.com/questions/15194409/simulated-mouseevent-not-working-properly-osx
  CGWarpMouseCursorPosition(location);
}

inline CGEventType event_type_mouse(input_t &input) {
  auto macos_input = ((macos_input_t *)input.get());

  if(macos_input->mouse_down[0]) {
    return kCGEventLeftMouseDragged;
  }
  else if(macos_input->mouse_down[1]) {
    return kCGEventOtherMouseDragged;
  }
  else if(macos_input->mouse_down[2]) {
    return kCGEventRightMouseDragged;
  }
  else {
    return kCGEventMouseMoved;
  }
}

void move_mouse(input_t &input, int deltaX, int deltaY) {
  auto current = get_mouse_loc(input);

  CGPoint location = CGPointMake(current.x + deltaX, current.y + deltaY);

  post_mouse(input, kCGMouseButtonLeft, event_type_mouse(input), location, 0);
}

void abs_mouse(input_t &input, const touch_port_t &touch_port, float x, float y) {
  auto scaling = ((macos_input_t *)input.get())->displayScaling;

  CGPoint location = CGPointMake(x * scaling, y * scaling);

  post_mouse(input, kCGMouseButtonLeft, event_type_mouse(input), location, 0);
}

uint64_t time_diff(uint64_t start) {
  uint64_t elapsed;
  Nanoseconds elapsedNano;

  elapsed     = mach_absolute_time() - start;
  elapsedNano = AbsoluteToNanoseconds(*(AbsoluteTime *)&elapsed);

  return *(uint64_t *)&elapsedNano;
}

void button_mouse(input_t &input, int button, bool release) {
  CGMouseButton mac_button;
  CGEventType event;

  auto mouse = ((macos_input_t *)input.get());

  switch(button) {
  case 1:
    mac_button = kCGMouseButtonLeft;
    event      = release ? kCGEventLeftMouseUp : kCGEventLeftMouseDown;
    break;
  case 2:
    mac_button = kCGMouseButtonCenter;
    event      = release ? kCGEventOtherMouseUp : kCGEventOtherMouseDown;
    break;
  case 3:
    mac_button = kCGMouseButtonRight;
    event      = release ? kCGEventRightMouseUp : kCGEventRightMouseDown;
    break;
  default:
    BOOST_LOG(warning) << "Unsupported mouse button for MacOS: "sv << button;
    return;
  }

  mouse->mouse_down[mac_button] = !release;

  // if the last mouse down was less than MULTICLICK_DELAY_NS, we send a double click event
  if(time_diff(mouse->last_mouse_event[mac_button][release]) < MULTICLICK_DELAY_NS) {
    post_mouse(input, mac_button, event, get_mouse_loc(input), 2);
  }
  else {
    post_mouse(input, mac_button, event, get_mouse_loc(input), 1);
  }

  mouse->last_mouse_event[mac_button][release] = mach_absolute_time();
}

void scroll(input_t &input, int high_res_distance) {
  CGEventRef upEvent = CGEventCreateScrollWheelEvent(
    NULL,
    kCGScrollEventUnitLine,
    2, high_res_distance > 0 ? 1 : -1, high_res_distance);
  CGEventPost(kCGHIDEventTap, upEvent);
  CFRelease(upEvent);
}

input_t input() {
  input_t result { new macos_input_t() };

  auto macos_input = (macos_input_t *)result.get();

  // If we don't use the main display in the future, this has to be adapted
  macos_input->display = CGMainDisplayID();

  // Input coordinates are based on the virtual resolution not the physical, so we need the scaling factor
  CGDisplayModeRef mode       = CGDisplayCopyDisplayMode(macos_input->display);
  macos_input->displayScaling = ((CGFloat)CGDisplayPixelsWide(macos_input->display)) / ((CGFloat)CGDisplayModeGetPixelWidth(mode));
  CFRelease(mode);

  macos_input->source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);

  macos_input->kb_event = CGEventCreate(macos_input->source);
  macos_input->kb_flags = 0;

  macos_input->mouse_event            = CGEventCreate(macos_input->source);
  macos_input->mouse_down[0]          = false;
  macos_input->mouse_down[1]          = false;
  macos_input->mouse_down[2]          = false;
  macos_input->last_mouse_event[0][0] = 0;
  macos_input->last_mouse_event[0][1] = 0;
  macos_input->last_mouse_event[1][0] = 0;
  macos_input->last_mouse_event[1][1] = 0;
  macos_input->last_mouse_event[2][0] = 0;
  macos_input->last_mouse_event[2][1] = 0;

  BOOST_LOG(debug) << "Display "sv << macos_input->display << ", pixel dimention: " << CGDisplayPixelsWide(macos_input->display) << "x"sv << CGDisplayPixelsHigh(macos_input->display);

  return result;
}

void freeInput(void *p) {
  auto *input = (macos_input_t *)p;

  CFRelease(input->source);
  CFRelease(input->kb_event);
  CFRelease(input->mouse_event);

  delete input;
}

std::vector<std::string_view> &supported_gamepads() {
  static std::vector<std::string_view> gamepads { ""sv };

  return gamepads;
}
} // namespace platf
