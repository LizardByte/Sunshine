/**
 * @file src/platform/macos/input.cpp
 * @brief Definitions for macOS input handling.
 */
// standard includes
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

// platform includes
#include <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hidsystem/IOLLEvent.h>
#include <mach/mach.h>

// local includes
#include "src/display_device.h"
#include "src/input.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/platform/macos/input_gamepad.h"
#include "src/utility.h"

// IOHIDUserDevice forward declarations (header absent in Command Line Tools SDK).
// Declared after the includes so every #include stays grouped at the top of the file.
// The IOKit HID property-key strings ("Product", "VendorID", etc.) are likewise
// absent from the CLT SDK, so they are passed inline at the call sites below.
extern "C" {
  typedef struct __IOHIDUserDevice *IOHIDUserDeviceRef;
  extern IOHIDUserDeviceRef IOHIDUserDeviceCreate(CFAllocatorRef allocator, CFDictionaryRef properties);
  extern void IOHIDUserDeviceScheduleWithRunLoop(IOHIDUserDeviceRef device, CFRunLoopRef runLoop, CFStringRef runLoopMode);
  extern void IOHIDUserDeviceUnscheduleFromRunLoop(IOHIDUserDeviceRef device, CFRunLoopRef runLoop, CFStringRef runLoopMode);
  extern IOReturn IOHIDUserDeviceHandleReport(IOHIDUserDeviceRef device, uint8_t *report, CFIndex reportLength);
}

/**
 * @brief Delay for a double click, in milliseconds.
 * @todo Make this configurable.
 */
constexpr std::chrono::milliseconds MULTICLICK_DELAY_MS(500);

// Gamepad HID report descriptor — emulates a Razer Serval (VID 0x1532 /
// PID 0x0900, an Android/PC Bluetooth gamepad).
//
// Why the Razer Serval?
//   - It has a built-in SDL GameControllerDB entry with ANALOG triggers
//     (lefttrigger:a5, righttrigger:a4) — so SDL/Wine/Steam auto-recognize it
//     as a proper gamepad (is_gamepad=1) without any user setup.
//   - Its VID 0x1532 (Razer) is never claimed by SDL's HIDAPI driver, so an
//     IOHIDUserDevice with this identity is fully visible and readable by all
//     consumers: IOKit, SDL, native Steam, Wine DirectInput, and winexinput.
//   - True XInput PIDs (Xbox 0x045E, etc.) are claimed by HIDAPI / ignored by
//     the IOKit backend; Logitech XInput PIDs share the same problem. The Serval
//     PID is pure generic HID — never intercepted.
//   - 11 buttons, HAT d-pad, 6 analog axes: maps cleanly onto a standard gamepad.
//
// Button order matches the Serval's SDL entry:
//   b0=A b1=B b2=X b3=Y b4=LB b5=RB b6=Back b7=Start b8=Guide b9=LS b10=RS
// D-pad is a HAT switch (the standard way — not individual buttons).
// Axis HID usages are assigned so SDL's usage-sorted axis indices match the
// Serval's SDL mapping (leftx:a0 lefty:a1 rightx:a2 righty:a3 rt:a4 lt:a5):
//   X(0x30)→LX=a0  Y(0x31)→LY=a1  Z(0x32)→RX=a2
//   Rx(0x33)→RY=a3  Ry(0x34)→RT=a4  Rz(0x35)→LT=a5
static const uint8_t kGamepadHIDDescriptor[] = {
  0x05,
  0x01,  // Usage Page: Generic Desktop
  0x09,
  0x05,  // Usage: Gamepad
  0xA1,
  0x01,  // Collection: Application
  0x85,
  0x01,  //   Report ID: 1

  // Buttons 1-11: A, B, X, Y, LB, RB, Back, Start, Guide, LS, RS
  0x05,
  0x09,  //   Usage Page: Button
  0x19,
  0x01,  //   Usage Minimum: 1
  0x29,
  0x0B,  //   Usage Maximum: 11
  0x15,
  0x00,  //   Logical Minimum: 0
  0x25,
  0x01,  //   Logical Maximum: 1
  0x75,
  0x01,  //   Report Size: 1 bit
  0x95,
  0x0B,  //   Report Count: 11
  0x81,
  0x02,  //   Input: Data, Variable, Absolute
  // Padding: 5 bits to fill the 2nd byte
  0x75,
  0x01,
  0x95,
  0x05,
  0x81,
  0x03,  //   Input: Const

  // D-pad as a HAT switch (directions run clockwise from North; value eight means centered)
  0x05,
  0x01,  //   Usage Page: Generic Desktop
  0x09,
  0x39,  //   Usage: Hat switch
  0x15,
  0x00,  //   Logical Minimum: 0
  0x25,
  0x07,  //   Logical Maximum: 7
  0x35,
  0x00,  //   Physical Minimum: 0 degrees
  0x46,
  0x3B,
  0x01,  //   Physical Maximum: 315 degrees
  0x65,
  0x14,  //   Unit: Degrees
  0x75,
  0x04,  //   Report Size: 4 bits
  0x95,
  0x01,  //   Report Count: 1
  0x81,
  0x42,  //   Input: Data, Variable, Absolute, Null State
  // Padding: 4 bits
  0x65,
  0x00,  //   Unit: None
  0x75,
  0x04,
  0x95,
  0x01,
  0x81,
  0x03,  //   Input: Const

  // Axes: LS X/Y, RS X/Y, RT, LT (6 × 16-bit signed).
  // Usage codes are chosen so SDL's usage-sorted indices yield:
  //   X(0x30)=a0=LX  Y(0x31)=a1=LY  Z(0x32)=a2=RX
  //   Rx(0x33)=a3=RY  Ry(0x34)=a4=RT  Rz(0x35)=a5=LT
  // This matches the Serval's SDL entry: leftx:a0 lefty:a1 rightx:a2 righty:a3
  //   righttrigger:a4 lefttrigger:a5
  0x09,
  0x30,  //   Usage: X   → Left Stick X  (a0)
  0x09,
  0x31,  //   Usage: Y   → Left Stick Y  (a1)
  0x09,
  0x32,  //   Usage: Z   → Right Stick X (a2)
  0x09,
  0x33,  //   Usage: Rx  → Right Stick Y (a3)
  0x09,
  0x34,  //   Usage: Ry  → Right Trigger (a4)
  0x09,
  0x35,  //   Usage: Rz  → Left Trigger  (a5)
  0x16,
  0x00,
  0x80,  //   Logical Minimum: -32768
  0x26,
  0xFF,
  0x7F,  //   Logical Maximum: 32767
  0x75,
  0x10,  //   Report Size: 16 bits
  0x95,
  0x06,  //   Report Count: 6
  0x81,
  0x02,  //   Input: Data, Variable, Absolute
  0xC0  // End Collection
};

// gamepad_hid_report_t (the HID report layout matching kGamepadHIDDescriptor)
// and the state→report mapping live in input_gamepad.h so the mapping can be
// unit-tested without a real IOHIDUserDevice.

struct macos_gamepad_t {
  IOHIDUserDeviceRef hid_device = nullptr;
  platf::gamepad_hid_report_t report {};
  CFRunLoopRef run_loop = nullptr;  // run loop of the dedicated HID thread
  std::jthread run_loop_thread;  // owns the dedicated HID run-loop thread

  macos_gamepad_t() = default;

  // Non-copyable / non-movable: the destructor joins a thread that captures
  // `this`-owned state, so the object must stay put for its whole lifetime.
  macos_gamepad_t(const macos_gamepad_t &) = delete;
  macos_gamepad_t &operator=(const macos_gamepad_t &) = delete;

  /**
   * @brief Tears down the virtual device: stops the run loop, joins its thread,
   *        then releases the HID device.
   *
   * Keeping cleanup in the destructor (rather than only in free_gamepad) means
   * the device and its thread are released no matter how the object dies —
   * including when the whole macos_input_t is torn down at shutdown.
   *
   * The stop request is *enqueued* on the run loop via CFRunLoopPerformBlock
   * instead of calling CFRunLoopStop directly. CFRunLoopStop only takes effect
   * if the loop is already running; if free happens right after alloc, the stop
   * could land before CFRunLoopRun() starts and be lost, hanging the thread
   * forever. A queued block instead runs as soon as the loop spins up and stops
   * it from the inside, which is race-free. The device is released only after
   * the thread has joined, so the thread never touches a freed device.
   */
  ~macos_gamepad_t() {
    if (run_loop) {
      CFRunLoopPerformBlock(run_loop, kCFRunLoopDefaultMode, ^{
        CFRunLoopStop(CFRunLoopGetCurrent());
      });
      CFRunLoopWakeUp(run_loop);
    }
    if (run_loop_thread.joinable()) {
      run_loop_thread.join();
    }
    if (run_loop) {
      CFRelease(run_loop);  // balance the CFRetain in alloc_gamepad
    }
    if (hid_device) {
      CFRelease(hid_device);
    }
  }
};

namespace platf {
  using namespace std::literals;

  constexpr int WHEEL_DELTA = 120;
  constexpr double DEFAULT_SCROLLWHEEL_SCALING = 0.3125;
  constexpr int DEFAULT_SCROLL_LINES_PER_DETENT = 5;

  struct macos_input_t {
  public:
    CGDirectDisplayID display {};
    CGFloat displayScaling {};
    CGEventSourceRef source {};

    // keyboard related stuff
    CGEventSourceRef keyboard_source {};
    CGEventFlags kb_flags {};

    // mouse related stuff
    CGEventRef mouse_event {};  // mouse event source
    double scrollwheel_scaling {DEFAULT_SCROLLWHEEL_SCALING};
    int scroll_lines_per_detent {DEFAULT_SCROLL_LINES_PER_DETENT};
    bool mouse_down[3] {};  // mouse button status
    std::chrono::steady_clock::steady_clock::time_point last_mouse_event[3][2];  // timestamp of last mouse events

    // gamepad related stuff
    std::array<std::unique_ptr<macos_gamepad_t>, platf::MAX_GAMEPADS> gamepads {};
    // Guards the gamepads array. alloc_gamepad / free_gamepad / gamepad_update
    // can run on different threads (e.g. the control stream and task_pool
    // workers — the back→home button emulation calls gamepad_update from a
    // delayed task while a session teardown may push free_gamepad), so all
    // access to the array is serialized.
    std::mutex gamepads_mutex;
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
    KeyCodeMap key_map {};

    key_map.win_keycode = keycode;
    const KeyCodeMap *temp_map = std::lower_bound(
      kKeyCodesMap,
      kKeyCodesMap + sizeof(kKeyCodesMap) / sizeof(kKeyCodesMap[0]),
      key_map
    );

    if (temp_map >= kKeyCodesMap + sizeof(kKeyCodesMap) / sizeof(kKeyCodesMap[0]) ||
        temp_map->win_keycode != keycode || temp_map->mac_keycode == -1) {
      return -1;
    }

    return temp_map->mac_keycode;
  }

  struct modifier_flags_t {
    CGEventFlags generic {};
    CGEventFlags device {};
    CGEventFlags all_devices {};
  };

  bool modifier_flags_for_key(int key, modifier_flags_t &flags) {
    switch (key) {
      case kVK_Shift:
        flags = {kCGEventFlagMaskShift, NX_DEVICELSHIFTKEYMASK, NX_DEVICELSHIFTKEYMASK | NX_DEVICERSHIFTKEYMASK};
        return true;
      case kVK_RightShift:
        flags = {kCGEventFlagMaskShift, NX_DEVICERSHIFTKEYMASK, NX_DEVICELSHIFTKEYMASK | NX_DEVICERSHIFTKEYMASK};
        return true;
      case kVK_Command:
        flags = {kCGEventFlagMaskCommand, NX_DEVICELCMDKEYMASK, NX_DEVICELCMDKEYMASK | NX_DEVICERCMDKEYMASK};
        return true;
      case kVK_RightCommand:
        flags = {kCGEventFlagMaskCommand, NX_DEVICERCMDKEYMASK, NX_DEVICELCMDKEYMASK | NX_DEVICERCMDKEYMASK};
        return true;
      case kVK_Option:
        flags = {kCGEventFlagMaskAlternate, NX_DEVICELALTKEYMASK, NX_DEVICELALTKEYMASK | NX_DEVICERALTKEYMASK};
        return true;
      case kVK_RightOption:
        flags = {kCGEventFlagMaskAlternate, NX_DEVICERALTKEYMASK, NX_DEVICELALTKEYMASK | NX_DEVICERALTKEYMASK};
        return true;
      case kVK_Control:
        flags = {kCGEventFlagMaskControl, NX_DEVICELCTLKEYMASK, NX_DEVICELCTLKEYMASK | NX_DEVICERCTLKEYMASK};
        return true;
      case kVK_RightControl:
        flags = {kCGEventFlagMaskControl, NX_DEVICERCTLKEYMASK, NX_DEVICELCTLKEYMASK | NX_DEVICERCTLKEYMASK};
        return true;
      default:
        return false;
    }
  }

  void keyboard_update(input_t &input, uint16_t modcode, bool release, uint8_t flags) {
    auto key = keysym(modcode);

    BOOST_LOG(debug) << "got keycode: 0x"sv << std::hex << modcode << ", translated to: 0x" << std::hex << key << ", release:" << release;

    if (key < 0) {
      return;
    }

    auto macos_input = ((macos_input_t *) input.get());
    CGEventRef event = nullptr;
    modifier_flags_t modifier_flags;

    if (modifier_flags_for_key(key, modifier_flags)) {
      event = CGEventCreateKeyboardEvent(macos_input->keyboard_source, key, !release);
      if (!event) {
        return;
      }

      CGEventSetIntegerValueField(event, kCGKeyboardEventKeycode, key);

      if (release) {
        macos_input->kb_flags &= ~modifier_flags.device;
        if ((macos_input->kb_flags & modifier_flags.all_devices) == 0) {
          macos_input->kb_flags &= ~modifier_flags.generic;
        }
      } else {
        macos_input->kb_flags |= modifier_flags.generic | modifier_flags.device;
      }

      CGEventSetType(event, kCGEventFlagsChanged);
    } else {
      event = CGEventCreateKeyboardEvent(macos_input->keyboard_source, key, !release);
      if (!event) {
        return;
      }

      CGEventSetType(event, release ? kCGEventKeyUp : kCGEventKeyDown);
    }

    CGEventSetFlags(event, macos_input->kb_flags);
    CGEventPost(kCGSessionEventTap, event);
    CFRelease(event);
  }

  void unicode(input_t &input, char *utf8, int size) {
    BOOST_LOG(info) << "unicode: Unicode input not yet implemented for MacOS."sv;
  }

  // Creates a virtual HID gamepad for the given slot (documented in
  // src/platform/common.h, the shared platf:: interface).
  //
  // The macOS virtual gamepad is currently input-only: it emulates a single
  // fixed device (a Razer Serval) and reports buttons/axes to the OS, but does
  // not consume the arrival metadata or post anything to the feedback queue. As
  // a result none of the feedback features other platforms support — rumble,
  // trigger rumble, RGB LED, adaptive triggers, motion/battery (see
  // gamepad_feedback_e and the inputtino/ViGEm backends) — are implemented here.
  // The descriptor also has no output report, so OS-side SET_REPORTs (e.g.
  // rumble) are not received. Wiring these up would require an output report in
  // kGamepadHIDDescriptor plus a SET_REPORT callback on the run loop.
  int alloc_gamepad(input_t &input, const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue) {
    auto *macos_input = static_cast<macos_input_t *>(input.get());
    const int nr = id.globalIndex;

    if (nr < 0 || nr >= platf::MAX_GAMEPADS) {
      BOOST_LOG(error) << "alloc_gamepad: slot " << nr << " out of range";
      return -1;
    }

    // If this slot is already occupied (e.g. the client re-sends a controller
    // arrival without an intervening removal, or on reconnect), release the
    // previous device first. Otherwise the old IOHIDUserDevice and its dedicated
    // run-loop thread leak — overwriting gamepads[nr] does not stop that thread,
    // so the stale virtual device stays registered with the HID system.
    // free_gamepad takes gamepads_mutex itself, so don't hold it across this.
    bool occupied;
    {
      std::scoped_lock lock(macos_input->gamepads_mutex);
      occupied = static_cast<bool>(macos_input->gamepads[nr]);
    }
    if (occupied) {
      BOOST_LOG(warning) << "alloc_gamepad: slot " << nr << " already occupied; releasing previous device";
      free_gamepad(input, nr);
    }

    // Build device properties
    CFMutableDictionaryRef props = CFDictionaryCreateMutable(
      kCFAllocatorDefault,
      0,
      &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks
    );

    // Helper: create a CFNumber, set it on props, then release our reference
    // (the dictionary retains its own copy via kCFTypeDictionaryValueCallBacks).
    auto set_int32 = [&](CFStringRef key, int32_t value) {
      CFNumberRef num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
      CFDictionarySetValue(props, key, num);
      CFRelease(num);
    };

    set_int32(CFSTR("PrimaryUsagePage"), 0x01);  // Generic Desktop usage page
    set_int32(CFSTR("PrimaryUsage"), 0x05);  // Gamepad usage

    // Embed the HID report descriptor
    CFDataRef descriptor = CFDataCreate(kCFAllocatorDefault, kGamepadHIDDescriptor, sizeof(kGamepadHIDDescriptor));
    CFDictionarySetValue(props, CFSTR("ReportDescriptor"), descriptor);
    CFRelease(descriptor);

    // Vendor/product identity — see the kGamepadHIDDescriptor comment for why
    // we emulate a Razer Serval (0x1532/0x0900).
    CFDictionarySetValue(props, CFSTR("Manufacturer"), CFSTR("Razer"));
    CFDictionarySetValue(props, CFSTR("Product"), CFSTR("Razer Serval"));
    set_int32(CFSTR("VendorID"), 0x1532);  // Razer
    set_int32(CFSTR("ProductID"), 0x0900);  // Serval

    IOHIDUserDeviceRef device = IOHIDUserDeviceCreate(kCFAllocatorDefault, props);
    CFRelease(props);

    if (!device) {
      BOOST_LOG(error) << "alloc_gamepad: IOHIDUserDeviceCreate failed for slot " << nr;
      return -1;
    }

    // Sunshine's main thread runs Boost.Asio, not a CFRunLoop, so scheduling
    // on CFRunLoopGetMain() would leave the device with an unspun run loop.
    // Spin a dedicated thread that schedules the device on its own CFRunLoop
    // and keeps it running. Use a promise to hand the run loop ref back so the
    // destructor can stop it cleanly. The thread is owned by macos_gamepad_t
    // and joined in its destructor, so `device` stays valid for the thread's
    // whole lifetime (it is CFRelease'd only after the join).
    auto gp = std::make_unique<macos_gamepad_t>();
    gp->hid_device = device;
    gp->report.report_id = 1;
    gp->report.hat = 8;  // null state (no D-pad direction)

    // Spinning up the thread (or taking the lock) can throw std::system_error
    // under resource exhaustion. Callers only inspect the return code, so
    // translate that failure into -1 rather than letting it escape. If we throw
    // here, gp's destructor releases the device (and joins the thread if it was
    // already started), so nothing leaks.
    try {
      std::promise<CFRunLoopRef> rl_promise;
      auto rl_future = rl_promise.get_future();

      gp->run_loop_thread = std::jthread([device, nr, promise = std::move(rl_promise)]() mutable {
        // Name the thread so it's identifiable in Console/Instruments/lldb
        // (repo convention; up to platf::MAX_GAMEPADS of these can exist at once).
        set_thread_name(std::format("gamepad::hid[{}]", nr));
        CFRunLoopRef rl = CFRunLoopGetCurrent();
        IOHIDUserDeviceScheduleWithRunLoop(device, rl, kCFRunLoopDefaultMode);
        promise.set_value(rl);  // hand run loop ref back to alloc_gamepad
        CFRunLoopRun();  // blocks until the destructor's queued block stops it
        IOHIDUserDeviceUnscheduleFromRunLoop(device, rl, kCFRunLoopDefaultMode);
      });

      // Wait until the thread has scheduled the device, then take our own
      // reference on its run loop. CFRunLoopGetCurrent() (used inside the
      // thread) returns a non-owning reference tied to the thread's lifetime;
      // retaining here keeps the run loop valid for the destructor even in the
      // unlikely event the thread exits early (e.g. CFRunLoopRun returns with
      // no source).
      CFRunLoopRef rl = rl_future.get();
      CFRetain(rl);
      gp->run_loop = rl;

      // The slot is empty here: alloc runs on the single control-stream thread
      // and global ids are unique, so no other thread fills nr while we're in
      // this function (and the "occupied" case above already freed it). The
      // assignment therefore never destroys a live device, i.e. never joins a
      // run-loop thread while holding the lock — the one thing free_gamepad
      // takes care to avoid.
      std::scoped_lock lock(macos_input->gamepads_mutex);
      macos_input->gamepads[nr] = std::move(gp);
    } catch (const std::system_error &e) {
      BOOST_LOG(error) << "alloc_gamepad: failed to start HID run-loop thread for slot " << nr << ": " << e.what();
      return -1;
    }

    BOOST_LOG(info) << "alloc_gamepad: created virtual gamepad in slot " << nr;
    return 0;
  }

  // Destroys the virtual HID gamepad in the given slot (documented in src/platform/common.h).
  void free_gamepad(input_t &input, int nr) {
    auto *macos_input = static_cast<macos_input_t *>(input.get());
    if (nr < 0 || nr >= platf::MAX_GAMEPADS) {
      return;
    }

    // Detach the device from the slot under the lock so the slot reads as empty
    // immediately, then let it destruct *outside* the lock. ~macos_gamepad_t()
    // joins the run-loop thread, which we must not do while holding the mutex
    // (that would block gamepad_update for the whole teardown).
    std::unique_ptr<macos_gamepad_t> doomed;
    {
      std::scoped_lock lock(macos_input->gamepads_mutex);
      doomed = std::move(macos_input->gamepads[nr]);
    }

    if (doomed) {
      doomed.reset();  // stops the run loop, joins its thread, releases the HID device
      BOOST_LOG(info) << "free_gamepad: released slot " << nr;
    }
  }

  /**
   * @brief Negates a signed 16-bit axis value without overflowing.
   *
   * A plain unary minus on INT16_MIN (-32768) would produce +32768, which is
   * not representable as int16_t and wraps back to -32768 — leaving a
   * fully-deflected stick stuck at the wrong extreme. Clamp that one case to
   * INT16_MAX so the negation stays monotonic across the whole range.
   *
   * @param v The axis value to negate.
   * @return -v, clamped to the int16_t range.
   */
  static int16_t negate_axis(int16_t v) {
    return v == INT16_MIN ? INT16_MAX : static_cast<int16_t>(-v);
  }

  /**
   * @brief Computes the HID HAT-switch value for the current D-pad button state.
   *
   * The HAT encodes eight directions running clockwise from North; a value of
   * eight means centered. Opposing presses (e.g. up and down together) cancel.
   *
   * @param buttonFlags The Moonlight button bitmask.
   * @return The HAT value: zero through seven for a direction, eight for centered.
   */
  static uint8_t compute_dpad_hat(std::uint32_t buttonFlags) {
    const bool up = buttonFlags & DPAD_UP;
    const bool down = buttonFlags & DPAD_DOWN;
    const bool left = buttonFlags & DPAD_LEFT;
    const bool right = buttonFlags & DPAD_RIGHT;
    if (up && !down) {
      if (right) {
        return 1;  // NE
      }
      if (left) {
        return 7;  // NW
      }
      return 0;  // N
    }
    if (down && !up) {
      if (right) {
        return 3;  // SE
      }
      if (left) {
        return 5;  // SW
      }
      return 4;  // S
    }
    if (right && !left) {
      return 2;  // E
    }
    if (left && !right) {
      return 6;  // W
    }
    return 8;  // centered
  }

  gamepad_hid_report_t map_gamepad_state_to_hid_report(const gamepad_state_t &gamepad_state) {
    gamepad_hid_report_t report {};
    report.report_id = 1;

    // Buttons (bits 0-10 → HID buttons 1-11). Order matches the Razer Serval's
    // SDL GameControllerDB entry so SDL/Steam/Wine auto-map correctly:
    //   b0=A b1=B b2=X b3=Y b4=LB b5=RB b6=Back b7=Start b8=Guide b9=LS b10=RS
    // (SDL Serval mapping: a:b0, b:b1, x:b2, y:b3, leftshoulder:b4,
    //  rightshoulder:b5, back:b6, start:b7, guide:b8, leftstick:b9, rightstick:b10)
    if (gamepad_state.buttonFlags & A) {
      report.buttons |= (1 << 0);  // b0  A
    }
    if (gamepad_state.buttonFlags & B) {
      report.buttons |= (1 << 1);  // b1  B
    }
    if (gamepad_state.buttonFlags & X) {
      report.buttons |= (1 << 2);  // b2  X
    }
    if (gamepad_state.buttonFlags & Y) {
      report.buttons |= (1 << 3);  // b3  Y
    }
    if (gamepad_state.buttonFlags & LEFT_BUTTON) {
      report.buttons |= (1 << 4);  // b4  LB
    }
    if (gamepad_state.buttonFlags & RIGHT_BUTTON) {
      report.buttons |= (1 << 5);  // b5  RB
    }
    if (gamepad_state.buttonFlags & BACK) {
      report.buttons |= (1 << 6);  // b6  Back
    }
    if (gamepad_state.buttonFlags & START) {
      report.buttons |= (1 << 7);  // b7  Start
    }
    if (gamepad_state.buttonFlags & HOME) {
      report.buttons |= (1 << 8);  // b8  Guide
    }
    if (gamepad_state.buttonFlags & LEFT_STICK) {
      report.buttons |= (1 << 9);  // b9  LS
    }
    if (gamepad_state.buttonFlags & RIGHT_STICK) {
      report.buttons |= (1 << 10);  // b10 RS
    }

    report.hat = compute_dpad_hat(gamepad_state.buttonFlags);

    // Sticks: pass through as-is (-32768..32767); Y axes are negated to match
    // HID convention where up is the negative direction.
    // Triggers: scale 0..255 → full signed range -32768..32767 so SDL reads
    // them as a full-range axis (rest = -32768 → SDL_GameController 0; full =
    // 32767 → 32767). Sending 0..32767 made the axis idle at the midpoint,
    // which SDL reported as a half-pressed trigger.
    report.left_x = gamepad_state.lsX;
    report.left_y = negate_axis(gamepad_state.lsY);
    report.right_x = gamepad_state.rsX;
    report.right_y = negate_axis(gamepad_state.rsY);
    report.l2 = static_cast<int16_t>((gamepad_state.lt / 255.0f) * 65535.0f - 32768.0f);
    report.r2 = static_cast<int16_t>((gamepad_state.rt / 255.0f) * 65535.0f - 32768.0f);

    return report;
  }

  // Sends a gamepad state update as a HID report (documented in src/platform/common.h):
  // maps the Moonlight button flags and axis values into the packed HID report
  // (see map_gamepad_state_to_hid_report) and submits it to the virtual device.
  void gamepad_update(input_t &input, int nr, const gamepad_state_t &gamepad_state) {
    auto *macos_input = static_cast<macos_input_t *>(input.get());
    if (nr < 0 || nr >= platf::MAX_GAMEPADS) {
      return;
    }

    // Hold the lock for the whole update: it keeps free_gamepad from detaching
    // and destroying the device between the null-check and HandleReport. The
    // HID submission is a fast syscall, and updates for a single gamepad are
    // serial anyway, so this does not throttle the input path.
    std::scoped_lock lock(macos_input->gamepads_mutex);
    if (!macos_input->gamepads[nr]) {
      return;
    }

    auto &gp = *macos_input->gamepads[nr];
    gp.report = map_gamepad_state_to_hid_report(gamepad_state);

    // Send the HID report
    IOReturn ret = IOHIDUserDeviceHandleReport(
      gp.hid_device,
      reinterpret_cast<uint8_t *>(&gp.report),
      sizeof(gp.report)
    );

    if (ret != kIOReturnSuccess) {
      BOOST_LOG(warning) << "gamepad_update: IOHIDUserDeviceHandleReport failed: " << ret;
    }
  }

  // returns current mouse location:
  util::point_t get_mouse_loc(input_t &input) {
    // Creating a new event every time to avoid any reuse risk
    const auto macos_input = static_cast<macos_input_t *>(input.get());
    const auto snapshot_event = CGEventCreate(macos_input->source);
    const auto current = CGEventGetLocation(snapshot_event);
    CFRelease(snapshot_event);
    return util::point_t {
      current.x,
      current.y
    };
  }

  void post_mouse(
    input_t &input,
    const CGMouseButton button,
    const CGEventType type,
    const util::point_t raw_location,
    const util::point_t previous_location,
    const int click_count
  ) {
    BOOST_LOG(debug) << "mouse_event: "sv << button << ", type: "sv << type << ", location:"sv << raw_location.x << ":"sv << raw_location.y << " click_count: "sv << click_count;

    const auto macos_input = static_cast<macos_input_t *>(input.get());
    const auto display = macos_input->display;
    const auto event = macos_input->mouse_event;

    // get display bounds for current display
    const CGRect display_bounds = CGDisplayBounds(display);

    // limit mouse to current display bounds
    const auto location = CGPoint {
      std::clamp(raw_location.x, display_bounds.origin.x, display_bounds.origin.x + display_bounds.size.width - 1),
      std::clamp(raw_location.y, display_bounds.origin.y, display_bounds.origin.y + display_bounds.size.height - 1)
    };

    CGEventSetType(event, type);
    CGEventSetLocation(event, location);
    CGEventSetIntegerValueField(event, kCGMouseEventButtonNumber, button);
    CGEventSetIntegerValueField(event, kCGMouseEventClickState, click_count);

    // Include deltas so some 3D applications can consume changes (game cameras, etc)
    const double deltaX = raw_location.x - previous_location.x;
    const double deltaY = raw_location.y - previous_location.y;
    CGEventSetDoubleValueField(event, kCGMouseEventDeltaX, deltaX);
    CGEventSetDoubleValueField(event, kCGMouseEventDeltaY, deltaY);

    // Inject modifier flags into mouse events so that shift+click and similar combinations work correctly.
    CGEventSetFlags(event, macos_input->kb_flags);
    CGEventPost(kCGHIDEventTap, event);
    // For why this is here, see:
    // https://stackoverflow.com/questions/15194409/simulated-mouseevent-not-working-properly-osx
    CGWarpMouseCursorPosition(location);
  }

  inline CGEventType event_type_mouse(input_t &input) {
    const auto macos_input = static_cast<macos_input_t *>(input.get());

    if (macos_input->mouse_down[0]) {
      return kCGEventLeftMouseDragged;
    }
    if (macos_input->mouse_down[1]) {
      return kCGEventOtherMouseDragged;
    }
    if (macos_input->mouse_down[2]) {
      return kCGEventRightMouseDragged;
    }
    return kCGEventMouseMoved;
  }

  void move_mouse(
    input_t &input,
    const int deltaX,
    const int deltaY
  ) {
    const auto current = get_mouse_loc(input);

    const auto location = util::point_t {current.x + deltaX, current.y + deltaY};
    post_mouse(input, kCGMouseButtonLeft, event_type_mouse(input), location, current, 0);
  }

  void abs_mouse(
    input_t &input,
    const touch_port_t &touch_port,
    const float x,
    const float y
  ) {
    const auto macos_input = static_cast<macos_input_t *>(input.get());
    const auto scaling = macos_input->displayScaling;
    const auto display = macos_input->display;

    auto location = util::point_t {x * scaling, y * scaling};
    CGRect display_bounds = CGDisplayBounds(display);
    // in order to get the correct mouse location for capturing display , we need to add the display bounds to the location
    location.x += display_bounds.origin.x;
    location.y += display_bounds.origin.y;

    post_mouse(input, kCGMouseButtonLeft, event_type_mouse(input), location, get_mouse_loc(input), 0);
  }

  void button_mouse(input_t &input, const int button, const bool release) {
    CGMouseButton mac_button;
    CGEventType event;

    const auto macos_input = static_cast<macos_input_t *>(input.get());

    switch (button) {
      case 1:
        mac_button = kCGMouseButtonLeft;
        event = release ? kCGEventLeftMouseUp : kCGEventLeftMouseDown;
        break;
      case 2:
        mac_button = kCGMouseButtonCenter;
        event = release ? kCGEventOtherMouseUp : kCGEventOtherMouseDown;
        break;
      case 3:
        mac_button = kCGMouseButtonRight;
        event = release ? kCGEventRightMouseUp : kCGEventRightMouseDown;
        break;
      default:
        BOOST_LOG(warning) << "Unsupported mouse button for MacOS: "sv << button;
        return;
    }

    macos_input->mouse_down[mac_button] = !release;

    // if the last mouse down was less than MULTICLICK_DELAY_MS, we send a double click event
    const auto now = std::chrono::steady_clock::now();
    const auto mouse_position = get_mouse_loc(input);

    if (now < macos_input->last_mouse_event[mac_button][release] + MULTICLICK_DELAY_MS) {
      post_mouse(input, mac_button, event, mouse_position, mouse_position, 2);
    } else {
      post_mouse(input, mac_button, event, mouse_position, mouse_position, 1);
    }

    macos_input->last_mouse_event[mac_button][release] = now;
  }

  int get_scroll_lines_per_detent(double &scrollwheel_scaling) {
    double scale = DEFAULT_SCROLLWHEEL_SCALING;
    const auto value = CFPreferencesCopyValue(CFSTR("com.apple.scrollwheel.scaling"), kCFPreferencesAnyApplication, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
    if (value) {
      if (CFGetTypeID(value) == CFNumberGetTypeID()) {
        CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberDoubleType, &scale);
      } else if (CFGetTypeID(value) == CFStringGetTypeID()) {
        scale = CFStringGetDoubleValue(static_cast<CFStringRef>(value));
      }
      CFRelease(value);
    }

    if (!std::isfinite(scale)) {
      scale = DEFAULT_SCROLLWHEEL_SCALING;
    }

    scrollwheel_scaling = scale;

    // com.apple.scrollwheel.scaling stores the Mouse scroll speed slider position, not
    // the scroll multiplier itself. The slider is 0..1 and Apple's default is 0.3125,
    // so anchor 0 at one line per wheel detent and 0.3125 at five lines.
    const auto scroll_scale = std::clamp(scale, 0.0, 1.0);
    constexpr double lines_per_scroll_scale = (DEFAULT_SCROLL_LINES_PER_DETENT - 1.0) / DEFAULT_SCROLLWHEEL_SCALING;

    return std::max(1, static_cast<int>(std::ceil(1.0 + scroll_scale * lines_per_scroll_scale)));
  }

  int scroll_pixels(const macos_input_t *macos_input, const int high_res_distance) {
    const auto source_pixels_per_line = CGEventSourceGetPixelsPerLine(macos_input->source);
    const auto pixels_per_line = source_pixels_per_line > 0 ? static_cast<int>(source_pixels_per_line + 0.5) : 10;
    const auto scaled_pixels = static_cast<int64_t>(high_res_distance) * std::max(1, pixels_per_line) * std::max(1, macos_input->scroll_lines_per_detent);

    return static_cast<int>(scaled_pixels / WHEEL_DELTA);
  }

  void post_scroll(input_t &input, const int wheelY, const int wheelX) {
    if (wheelY == 0 && wheelX == 0) {
      return;
    }

    const auto macos_input = static_cast<macos_input_t *>(input.get());
    CGEventRef event = CGEventCreateScrollWheelEvent(macos_input->source, kCGScrollEventUnitPixel, 2, wheelY, wheelX);
    if (!event) {
      return;
    }

    CGEventSetIntegerValueField(event, kCGScrollWheelEventIsContinuous, 1);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
  }

  void scroll(input_t &input, const int high_res_distance) {
    post_scroll(input, scroll_pixels(static_cast<macos_input_t *>(input.get()), high_res_distance), 0);
  }

  void hscroll(input_t &input, int high_res_distance) {
    post_scroll(input, 0, scroll_pixels(static_cast<macos_input_t *>(input.get()), high_res_distance));
  }

  /**
   * @brief Allocates a context to store per-client input data.
   * @param input The global input context.
   * @return A unique pointer to a per-client input data context.
   */
  std::unique_ptr<client_input_t> allocate_client_input_context(input_t &input) {
    // Unused
    return nullptr;
  }

  /**
   * @brief Sends a touch event to the OS.
   * @param input The client-specific input context.
   * @param touch_port The current viewport for translating to screen coordinates.
   * @param touch The touch event.
   */
  void touch_update(client_input_t *input, const touch_port_t &touch_port, const touch_input_t &touch) {
    // Unimplemented feature - platform_caps::pen_touch
  }

  /**
   * @brief Sends a pen event to the OS.
   * @param input The client-specific input context.
   * @param touch_port The current viewport for translating to screen coordinates.
   * @param pen The pen event.
   */
  void pen_update(client_input_t *input, const touch_port_t &touch_port, const pen_input_t &pen) {
    // Unimplemented feature - platform_caps::pen_touch
  }

  // Sends a gamepad touch event to the OS (documented in src/platform/common.h). Unimplemented on macOS.
  void gamepad_touch(input_t &input, const gamepad_touch_t &touch) {
    // Unimplemented feature - platform_caps::controller_touch
  }

  // Sends a gamepad motion event to the OS (documented in src/platform/common.h). Unimplemented on macOS.
  void gamepad_motion(input_t &input, const gamepad_motion_t &motion) {
    // Unimplemented
  }

  // Sends a gamepad battery event to the OS (documented in src/platform/common.h). Unimplemented on macOS.
  void gamepad_battery(input_t &input, const gamepad_battery_t &battery) {
    // Unimplemented
  }

  input_t input() {
    input_t result {new macos_input_t()};

    const auto macos_input = static_cast<macos_input_t *>(result.get());

    // Default to main display
    macos_input->display = CGMainDisplayID();

    auto output_name = display_device::map_output_name(config::video.output_name);
    // If output_name is set, try to find the display with that display id
    if (!output_name.empty()) {
      const int MAX_DISPLAYS = 32;
      uint32_t max_display = MAX_DISPLAYS;
      uint32_t display_count;
      CGDirectDisplayID displays[MAX_DISPLAYS];
      if (CGGetActiveDisplayList(max_display, displays, &display_count) != kCGErrorSuccess) {
        BOOST_LOG(error) << "Unable to get active display list , error: "sv << std::endl;
      } else {
        for (int i = 0; i < display_count; i++) {
          CGDirectDisplayID display_id = displays[i];
          if (display_id == std::atoi(output_name.c_str())) {
            macos_input->display = display_id;
          }
        }
      }
    }

    // Input coordinates are based on the virtual resolution not the physical, so we need the scaling factor.
    // CGDisplayCopyDisplayMode can return null (e.g. no/asleep display on a headless host or CI runner);
    // fall back to 1.0 rather than dereferencing/CFRelease-ing null.
    const CGDisplayModeRef mode = CGDisplayCopyDisplayMode(macos_input->display);
    if (mode) {
      macos_input->displayScaling = ((CGFloat) CGDisplayPixelsWide(macos_input->display)) / ((CGFloat) CGDisplayModeGetPixelWidth(mode));
      CFRelease(mode);
    } else {
      macos_input->displayScaling = 1.0;
      BOOST_LOG(warning) << "input(): CGDisplayCopyDisplayMode returned null; defaulting display scaling to 1.0"sv;
    }

    macos_input->source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    macos_input->keyboard_source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    macos_input->scroll_lines_per_detent = get_scroll_lines_per_detent(macos_input->scrollwheel_scaling);

    macos_input->kb_flags = 0;

    macos_input->mouse_event = CGEventCreate(macos_input->source);
    macos_input->mouse_down[0] = false;
    macos_input->mouse_down[1] = false;
    macos_input->mouse_down[2] = false;

    BOOST_LOG(debug) << "macOS scroll speed: com.apple.scrollwheel.scaling="sv << macos_input->scrollwheel_scaling << ", lines per detent="sv << macos_input->scroll_lines_per_detent << ", pixels per line="sv << CGEventSourceGetPixelsPerLine(macos_input->source);
    BOOST_LOG(debug) << "Display "sv << macos_input->display << ", pixel dimension: " << CGDisplayPixelsWide(macos_input->display) << "x"sv << CGDisplayPixelsHigh(macos_input->display);

    return result;
  }

  void freeInput(void *p) {
    const auto *input = static_cast<macos_input_t *>(p);

    CFRelease(input->source);
    CFRelease(input->keyboard_source);
    CFRelease(input->mouse_event);

    delete input;
  }

  std::vector<supported_gamepad_t> &supported_gamepads(input_t *input) {
    static std::vector<supported_gamepad_t> gamepads {
      supported_gamepad_t {"XInput", true, ""}
    };

    return gamepads;
  }

  /**
   * @brief Returns the supported platform capabilities to advertise to the client.
   * @return Capability flags.
   */
  platform_caps::caps_t get_capabilities() {
    return 0;
  }
}  // namespace platf
