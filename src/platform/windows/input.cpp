/**
 * @file src/platform/windows/input.cpp
 * @brief Definitions for input handling on Windows.
 */
#define WINVER 0x0A00

// platform includes
#include <Windows.h>

// standard includes
#include <cmath>
#include <thread>

// lib includes
#include <ViGEm/Client.h>

// local includes
#include "keylayout.h"
#include "misc.h"
#include "src/config.h"
#include "src/globals.h"
#include "src/logging.h"
#include "src/platform/common.h"

namespace platf {
  using namespace std::literals;

  thread_local HDESK _lastKnownInputDesktop = nullptr;

  constexpr touch_port_t target_touch_port {
    0,
    0,
    65535,
    65535
  };

  using client_t = util::safe_ptr<_VIGEM_CLIENT_T, vigem_free>;
  using target_t = util::safe_ptr<_VIGEM_TARGET_T, vigem_target_free>;

  void CALLBACK x360_notify(
    client_t::pointer client,
    target_t::pointer target,
    std::uint8_t largeMotor,
    std::uint8_t smallMotor,
    std::uint8_t /* led_number */,
    void *userdata
  );

  void CALLBACK ds4_notify(
    client_t::pointer client,
    target_t::pointer target,
    std::uint8_t largeMotor,
    std::uint8_t smallMotor,
    DS4_LIGHTBAR_COLOR /* led_color */,
    void *userdata
  );

  struct gp_touch_context_t {
    uint8_t pointerIndex;
    uint16_t x;
    uint16_t y;
  };

  struct gamepad_context_t {
    target_t gp;
    feedback_queue_t feedback_queue;

    union {
      XUSB_REPORT x360;
      DS4_REPORT_EX ds4;
    } report;

    // Map from pointer ID to pointer index
    std::map<uint32_t, uint8_t> pointer_id_map;
    uint8_t available_pointers;

    uint8_t client_relative_index;

    thread_pool_util::ThreadPool::task_id_t repeat_task {};
    std::chrono::steady_clock::time_point last_report_ts;

    gamepad_feedback_msg_t last_rumble;
    gamepad_feedback_msg_t last_rgb_led;
  };

  constexpr float EARTH_G = 9.80665f;

#define MPS2_TO_DS4_ACCEL(x) (int32_t) (((x) / EARTH_G) * 8192)
#define DPS_TO_DS4_GYRO(x) (int32_t) ((x) * (1024 / 64))

#define APPLY_CALIBRATION(val, bias, scale) (int32_t) (((float) (val) + (bias)) / (scale))

  constexpr DS4_TOUCH ds4_touch_unused = {
    .bPacketCounter = 0,
    .bIsUpTrackingNum1 = 0x80,
    .bTouchData1 = {0x00, 0x00, 0x00},
    .bIsUpTrackingNum2 = 0x80,
    .bTouchData2 = {0x00, 0x00, 0x00},
  };

  // See https://github.com/ViGEm/ViGEmBus/blob/22835473d17fbf0c4d4bb2f2d42fd692b6e44df4/sys/Ds4Pdo.cpp#L153-L164
  constexpr DS4_REPORT_EX ds4_report_init_ex = {
    {{.bThumbLX = 0x80,
      .bThumbLY = 0x80,
      .bThumbRX = 0x80,
      .bThumbRY = 0x80,
      .wButtons = DS4_BUTTON_DPAD_NONE,
      .bSpecial = 0,
      .bTriggerL = 0,
      .bTriggerR = 0,
      .wTimestamp = 0,
      .bBatteryLvl = 0xFF,
      .wGyroX = 0,
      .wGyroY = 0,
      .wGyroZ = 0,
      .wAccelX = 0,
      .wAccelY = 0,
      .wAccelZ = 0,
      ._bUnknown1 = {0x00, 0x00, 0x00, 0x00, 0x00},
      .bBatteryLvlSpecial = 0x1A,  // Wired - Full battery
      ._bUnknown2 = {0x00, 0x00},
      .bTouchPacketsN = 1,
      .sCurrentTouch = ds4_touch_unused,
      .sPreviousTouch = {ds4_touch_unused, ds4_touch_unused}}}
  };

  /**
   * @brief Updates the DS4 input report with the provided motion data.
   * @details Acceleration is in m/s^2 and gyro is in deg/s.
   * @param gamepad The gamepad to update.
   * @param motion_type The type of motion data.
   * @param x X component of motion.
   * @param y Y component of motion.
   * @param z Z component of motion.
   */
  static void ds4_update_motion(gamepad_context_t &gamepad, uint8_t motion_type, float x, float y, float z) {
    auto &report = gamepad.report.ds4.Report;

    // Use int32 to process this data, so we can clamp if needed.
    int32_t intX, intY, intZ;

    switch (motion_type) {
      case LI_MOTION_TYPE_ACCEL:
        // Convert to the DS4's accelerometer scale
        intX = MPS2_TO_DS4_ACCEL(x);
        intY = MPS2_TO_DS4_ACCEL(y);
        intZ = MPS2_TO_DS4_ACCEL(z);

        // Apply the inverse of ViGEmBus's calibration data
        intX = APPLY_CALIBRATION(intX, -297, 1.010796f);
        intY = APPLY_CALIBRATION(intY, -42, 1.014614f);
        intZ = APPLY_CALIBRATION(intZ, -512, 1.024768f);
        break;
      case LI_MOTION_TYPE_GYRO:
        // Convert to the DS4's gyro scale
        intX = DPS_TO_DS4_GYRO(x);
        intY = DPS_TO_DS4_GYRO(y);
        intZ = DPS_TO_DS4_GYRO(z);

        // Apply the inverse of ViGEmBus's calibration data
        intX = APPLY_CALIBRATION(intX, 1, 0.977596f);
        intY = APPLY_CALIBRATION(intY, 0, 0.972370f);
        intZ = APPLY_CALIBRATION(intZ, 0, 0.971550f);
        break;
      default:
        return;
    }

    // Clamp the values to the range of the data type
    intX = std::clamp(intX, INT16_MIN, INT16_MAX);
    intY = std::clamp(intY, INT16_MIN, INT16_MAX);
    intZ = std::clamp(intZ, INT16_MIN, INT16_MAX);

    // Populate the report
    switch (motion_type) {
      case LI_MOTION_TYPE_ACCEL:
        report.wAccelX = (int16_t) intX;
        report.wAccelY = (int16_t) intY;
        report.wAccelZ = (int16_t) intZ;
        break;
      case LI_MOTION_TYPE_GYRO:
        report.wGyroX = (int16_t) intX;
        report.wGyroY = (int16_t) intY;
        report.wGyroZ = (int16_t) intZ;
        break;
      default:
        return;
    }
  }

  class vigem_t {
  public:
    int init() {
      // Probe ViGEm during startup to see if we can successfully attach gamepads. This will allow us to
      // immediately display the error message in the web UI even before the user tries to stream.
      client_t client {vigem_alloc()};
      VIGEM_ERROR status = vigem_connect(client.get());
      if (!VIGEM_SUCCESS(status)) {
        // Log a special fatal message for this case to show the error in the web UI
        BOOST_LOG(fatal) << "ViGEmBus is not installed or running. You must install ViGEmBus for gamepad support!"sv;
      } else {
        vigem_disconnect(client.get());
      }

      gamepads.resize(MAX_GAMEPADS);

      return 0;
    }

    /**
     * @brief Attaches a new gamepad.
     * @param id The gamepad ID.
     * @param feedback_queue The queue for posting messages back to the client.
     * @param gp_type The type of gamepad.
     * @return 0 on success.
     */
    int alloc_gamepad_internal(const gamepad_id_t &id, feedback_queue_t &feedback_queue, VIGEM_TARGET_TYPE gp_type) {
      auto &gamepad = gamepads[id.globalIndex];
      assert(!gamepad.gp);

      gamepad.client_relative_index = id.clientRelativeIndex;
      gamepad.last_report_ts = std::chrono::steady_clock::now();

      // Establish a connect to the ViGEm driver if we don't have one yet
      if (!client) {
        BOOST_LOG(debug) << "Connecting to ViGEmBus driver"sv;
        client.reset(vigem_alloc());

        auto status = vigem_connect(client.get());
        if (!VIGEM_SUCCESS(status)) {
          BOOST_LOG(warning) << "Couldn't setup connection to ViGEm for gamepad support ["sv << util::hex(status).to_string_view() << ']';
          client.reset();
          return -1;
        }
      }

      if (gp_type == Xbox360Wired) {
        gamepad.gp.reset(vigem_target_x360_alloc());
        XUSB_REPORT_INIT(&gamepad.report.x360);
      } else {
        gamepad.gp.reset(vigem_target_ds4_alloc());

        // There is no equivalent DS4_REPORT_EX_INIT()
        gamepad.report.ds4 = ds4_report_init_ex;

        // Set initial accelerometer and gyro state
        ds4_update_motion(gamepad, LI_MOTION_TYPE_ACCEL, 0.0f, EARTH_G, 0.0f);
        ds4_update_motion(gamepad, LI_MOTION_TYPE_GYRO, 0.0f, 0.0f, 0.0f);

        // Request motion events from the client at 100 Hz
        feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(gamepad.client_relative_index, LI_MOTION_TYPE_ACCEL, 100));
        feedback_queue->raise(gamepad_feedback_msg_t::make_motion_event_state(gamepad.client_relative_index, LI_MOTION_TYPE_GYRO, 100));

        // We support pointer index 0 and 1
        gamepad.available_pointers = 0x3;
      }

      auto status = vigem_target_add(client.get(), gamepad.gp.get());
      if (!VIGEM_SUCCESS(status)) {
        BOOST_LOG(error) << "Couldn't add Gamepad to ViGEm connection ["sv << util::hex(status).to_string_view() << ']';

        return -1;
      }

      gamepad.feedback_queue = std::move(feedback_queue);

      if (gp_type == Xbox360Wired) {
        status = vigem_target_x360_register_notification(client.get(), gamepad.gp.get(), x360_notify, this);
      } else {
        status = vigem_target_ds4_register_notification(client.get(), gamepad.gp.get(), ds4_notify, this);
      }

      if (!VIGEM_SUCCESS(status)) {
        BOOST_LOG(warning) << "Couldn't register notifications for rumble support ["sv << util::hex(status).to_string_view() << ']';
      }

      return 0;
    }

    /**
     * @brief Detaches the specified gamepad
     * @param nr The gamepad.
     */
    void free_target(int nr) {
      auto &gamepad = gamepads[nr];

      if (gamepad.repeat_task) {
        task_pool.cancel(gamepad.repeat_task);
        gamepad.repeat_task = nullptr;
      }

      if (gamepad.gp && vigem_target_is_attached(gamepad.gp.get())) {
        auto status = vigem_target_remove(client.get(), gamepad.gp.get());
        if (!VIGEM_SUCCESS(status)) {
          BOOST_LOG(warning) << "Couldn't detach gamepad from ViGEm ["sv << util::hex(status).to_string_view() << ']';
        }
      }

      gamepad.gp.reset();

      // Disconnect from ViGEm if we just removed the last gamepad
      bool disconnect = true;
      for (auto &gamepad : gamepads) {
        if (gamepad.gp && vigem_target_is_attached(gamepad.gp.get())) {
          disconnect = false;
          break;
        }
      }
      if (disconnect) {
        BOOST_LOG(debug) << "Disconnecting from ViGEmBus driver"sv;
        vigem_disconnect(client.get());
        client.reset();
      }
    }

    /**
     * @brief Pass rumble data back to the client.
     * @param target The gamepad.
     * @param largeMotor The large motor.
     * @param smallMotor The small motor.
     */
    void rumble(target_t::pointer target, std::uint8_t largeMotor, std::uint8_t smallMotor) {
      for (int x = 0; x < gamepads.size(); ++x) {
        auto &gamepad = gamepads[x];

        if (gamepad.gp.get() == target) {
          // Convert from 8-bit to 16-bit values
          uint16_t normalizedLargeMotor = largeMotor << 8;
          uint16_t normalizedSmallMotor = smallMotor << 8;

          // Don't resend duplicate rumble data
          if (normalizedSmallMotor != gamepad.last_rumble.data.rumble.highfreq ||
              normalizedLargeMotor != gamepad.last_rumble.data.rumble.lowfreq) {
            // We have to use the client-relative index when communicating back to the client
            gamepad_feedback_msg_t msg = gamepad_feedback_msg_t::make_rumble(
              gamepad.client_relative_index,
              normalizedLargeMotor,
              normalizedSmallMotor
            );
            gamepad.feedback_queue->raise(msg);
            gamepad.last_rumble = msg;
          }
          return;
        }
      }
    }

    /**
     * @brief Pass RGB LED data back to the client.
     * @param target The gamepad.
     * @param r The red channel.
     * @param g The red channel.
     * @param b The red channel.
     */
    void set_rgb_led(target_t::pointer target, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
      for (int x = 0; x < gamepads.size(); ++x) {
        auto &gamepad = gamepads[x];

        if (gamepad.gp.get() == target) {
          // Don't resend duplicate RGB data
          if (r != gamepad.last_rgb_led.data.rgb_led.r ||
              g != gamepad.last_rgb_led.data.rgb_led.g ||
              b != gamepad.last_rgb_led.data.rgb_led.b) {
            // We have to use the client-relative index when communicating back to the client
            gamepad_feedback_msg_t msg = gamepad_feedback_msg_t::make_rgb_led(gamepad.client_relative_index, r, g, b);
            gamepad.feedback_queue->raise(msg);
            gamepad.last_rgb_led = msg;
          }
          return;
        }
      }
    }

    /**
     * @brief vigem_t destructor.
     */
    ~vigem_t() {
      if (client) {
        for (auto &gamepad : gamepads) {
          if (gamepad.gp && vigem_target_is_attached(gamepad.gp.get())) {
            auto status = vigem_target_remove(client.get(), gamepad.gp.get());
            if (!VIGEM_SUCCESS(status)) {
              BOOST_LOG(warning) << "Couldn't detach gamepad from ViGEm ["sv << util::hex(status).to_string_view() << ']';
            }
          }
        }

        vigem_disconnect(client.get());
      }
    }

    std::vector<gamepad_context_t> gamepads;

    client_t client;
  };

  void CALLBACK x360_notify(
    client_t::pointer client,
    target_t::pointer target,
    std::uint8_t largeMotor,
    std::uint8_t smallMotor,
    std::uint8_t /* led_number */,
    void *userdata
  ) {
    BOOST_LOG(debug)
      << "largeMotor: "sv << (int) largeMotor << std::endl
      << "smallMotor: "sv << (int) smallMotor;

    task_pool.push(&vigem_t::rumble, (vigem_t *) userdata, target, largeMotor, smallMotor);
  }

  void CALLBACK ds4_notify(
    client_t::pointer client,
    target_t::pointer target,
    std::uint8_t largeMotor,
    std::uint8_t smallMotor,
    DS4_LIGHTBAR_COLOR led_color,
    void *userdata
  ) {
    BOOST_LOG(debug)
      << "largeMotor: "sv << (int) largeMotor << std::endl
      << "smallMotor: "sv << (int) smallMotor << std::endl
      << "LED: "sv << util::hex(led_color.Red).to_string_view() << ' '
      << util::hex(led_color.Green).to_string_view() << ' '
      << util::hex(led_color.Blue).to_string_view() << std::endl;

    task_pool.push(&vigem_t::rumble, (vigem_t *) userdata, target, largeMotor, smallMotor);
    task_pool.push(&vigem_t::set_rgb_led, (vigem_t *) userdata, target, led_color.Red, led_color.Green, led_color.Blue);
  }

  struct input_raw_t {
    ~input_raw_t() {
      delete vigem;
    }

    vigem_t *vigem;

    decltype(CreateSyntheticPointerDevice) *fnCreateSyntheticPointerDevice;
    decltype(InjectSyntheticPointerInput) *fnInjectSyntheticPointerInput;
    decltype(DestroySyntheticPointerDevice) *fnDestroySyntheticPointerDevice;
  };

  input_t input() {
    input_t result {new input_raw_t {}};
    auto &raw = *(input_raw_t *) result.get();

    raw.vigem = new vigem_t {};
    if (raw.vigem->init()) {
      delete raw.vigem;
      raw.vigem = nullptr;
    }

    // Get pointers to virtual touch/pen input functions (Win10 1809+)
    raw.fnCreateSyntheticPointerDevice = (decltype(CreateSyntheticPointerDevice) *) GetProcAddress(GetModuleHandleA("user32.dll"), "CreateSyntheticPointerDevice");
    raw.fnInjectSyntheticPointerInput = (decltype(InjectSyntheticPointerInput) *) GetProcAddress(GetModuleHandleA("user32.dll"), "InjectSyntheticPointerInput");
    raw.fnDestroySyntheticPointerDevice = (decltype(DestroySyntheticPointerDevice) *) GetProcAddress(GetModuleHandleA("user32.dll"), "DestroySyntheticPointerDevice");

    return result;
  }

  /**
   * @brief Calls SendInput() and switches input desktops if required.
   * @param i The `INPUT` struct to send.
   */
  void send_input(INPUT &i) {
  retry:
    auto send = SendInput(1, &i, sizeof(INPUT));
    if (send != 1) {
      auto hDesk = syncThreadDesktop();
      if (_lastKnownInputDesktop != hDesk) {
        _lastKnownInputDesktop = hDesk;
        goto retry;
      }
      BOOST_LOG(error) << "Couldn't send input"sv;
    }
  }

  /**
   * @brief Calls InjectSyntheticPointerInput() and switches input desktops if required.
   * @details Must only be called if InjectSyntheticPointerInput() is available.
   * @param input The global input context.
   * @param device The synthetic pointer device handle.
   * @param pointerInfo An array of `POINTER_TYPE_INFO` structs.
   * @param count The number of elements in `pointerInfo`.
   * @return true if input was successfully injected.
   */
  bool inject_synthetic_pointer_input(input_raw_t *input, HSYNTHETICPOINTERDEVICE device, const POINTER_TYPE_INFO *pointerInfo, UINT32 count) {
  retry:
    if (!input->fnInjectSyntheticPointerInput(device, pointerInfo, count)) {
      auto hDesk = syncThreadDesktop();
      if (_lastKnownInputDesktop != hDesk) {
        _lastKnownInputDesktop = hDesk;
        goto retry;
      }
      return false;
    }
    return true;
  }

  void abs_mouse(input_t &input, const touch_port_t &touch_port, float x, float y) {
    INPUT i {};

    i.type = INPUT_MOUSE;
    auto &mi = i.mi;

    mi.dwFlags =
      MOUSEEVENTF_MOVE |
      MOUSEEVENTF_ABSOLUTE |

      // MOUSEEVENTF_VIRTUALDESK maps to the entirety of the desktop rather than the primary desktop
      MOUSEEVENTF_VIRTUALDESK;

    auto scaled_x = std::lround((x + touch_port.offset_x) * ((float) target_touch_port.width / (float) touch_port.width));
    auto scaled_y = std::lround((y + touch_port.offset_y) * ((float) target_touch_port.height / (float) touch_port.height));

    mi.dx = scaled_x;
    mi.dy = scaled_y;

    send_input(i);
  }

  void move_mouse(input_t &input, int deltaX, int deltaY) {
    INPUT i {};

    i.type = INPUT_MOUSE;
    auto &mi = i.mi;

    mi.dwFlags = MOUSEEVENTF_MOVE;
    mi.dx = deltaX;
    mi.dy = deltaY;

    send_input(i);
  }

  util::point_t get_mouse_loc(input_t &input) {
    throw std::runtime_error("not implemented yet, has to pass tests");
    // TODO: Tests are failing, something wrong here?
    POINT p;
    if (!GetCursorPos(&p)) {
      return util::point_t {0.0, 0.0};
    }

    return util::point_t {
      (double) p.x,
      (double) p.y
    };
  }

  void button_mouse(input_t &input, int button, bool release) {
    INPUT i {};

    i.type = INPUT_MOUSE;
    auto &mi = i.mi;

    if (button == 1) {
      mi.dwFlags = release ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN;
    } else if (button == 2) {
      mi.dwFlags = release ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_MIDDLEDOWN;
    } else if (button == 3) {
      mi.dwFlags = release ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_RIGHTDOWN;
    } else if (button == 4) {
      mi.dwFlags = release ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN;
      mi.mouseData = XBUTTON1;
    } else {
      mi.dwFlags = release ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN;
      mi.mouseData = XBUTTON2;
    }

    send_input(i);
  }

  void scroll(input_t &input, int distance) {
    INPUT i {};

    i.type = INPUT_MOUSE;
    auto &mi = i.mi;

    mi.dwFlags = MOUSEEVENTF_WHEEL;
    mi.mouseData = distance;

    send_input(i);
  }

  void hscroll(input_t &input, int distance) {
    INPUT i {};

    i.type = INPUT_MOUSE;
    auto &mi = i.mi;

    mi.dwFlags = MOUSEEVENTF_HWHEEL;
    mi.mouseData = distance;

    send_input(i);
  }

  void keyboard_update(input_t &input, uint16_t modcode, bool release, uint8_t flags) {
    INPUT i {};
    i.type = INPUT_KEYBOARD;
    auto &ki = i.ki;

    // If the client did not normalize this VK code to a US English layout, we can't accurately convert it to a scancode.
    // If we're set to always send scancodes, we will use the current keyboard layout to convert to a scancode. This will
    // assume the client and host have the same keyboard layout, but it's probably better than always using US English.
    if (!(flags & SS_KBE_FLAG_NON_NORMALIZED)) {
      // Mask off the extended key byte
      ki.wScan = VK_TO_SCANCODE_MAP[modcode & 0xFF];
    } else if (config::input.always_send_scancodes && modcode != VK_LWIN && modcode != VK_RWIN && modcode != VK_PAUSE) {
      // For some reason, MapVirtualKey(VK_LWIN, MAPVK_VK_TO_VSC) doesn't seem to work :/
      ki.wScan = MapVirtualKey(modcode, MAPVK_VK_TO_VSC);
    }

    // If we can map this to a scancode, send it as a scancode for maximum game compatibility.
    if (ki.wScan) {
      ki.dwFlags = KEYEVENTF_SCANCODE;
    } else {
      // If there is no scancode mapping or it's non-normalized, send it as a regular VK event.
      ki.wVk = modcode;
    }

    // https://docs.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input#keystroke-message-flags
    switch (modcode) {
      case VK_LWIN:
      case VK_RWIN:
      case VK_RMENU:
      case VK_RCONTROL:
      case VK_INSERT:
      case VK_DELETE:
      case VK_HOME:
      case VK_END:
      case VK_PRIOR:
      case VK_NEXT:
      case VK_UP:
      case VK_DOWN:
      case VK_LEFT:
      case VK_RIGHT:
      case VK_DIVIDE:
      case VK_APPS:
        ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        break;
      default:
        break;
    }

    if (release) {
      ki.dwFlags |= KEYEVENTF_KEYUP;
    }

    send_input(i);
  }

  struct client_input_raw_t: public client_input_t {
    client_input_raw_t(input_t &input) {
      global = (input_raw_t *) input.get();
    }

    ~client_input_raw_t() override {
      if (penRepeatTask) {
        task_pool.cancel(penRepeatTask);
      }
      if (touchRepeatTask) {
        task_pool.cancel(touchRepeatTask);
      }

      if (pen) {
        global->fnDestroySyntheticPointerDevice(pen);
      }
      if (touch) {
        global->fnDestroySyntheticPointerDevice(touch);
      }
    }

    input_raw_t *global;

    // Device state and handles for pen and touch input must be stored in the per-client
    // input context, because each connected client may be sending their own independent
    // pen/touch events. To maintain separation, we expose separate pen and touch devices
    // for each client.

    HSYNTHETICPOINTERDEVICE pen {};
    POINTER_TYPE_INFO penInfo {};
    thread_pool_util::ThreadPool::task_id_t penRepeatTask {};

    HSYNTHETICPOINTERDEVICE touch {};
    POINTER_TYPE_INFO touchInfo[10] {};
    UINT32 activeTouchSlots {};
    thread_pool_util::ThreadPool::task_id_t touchRepeatTask {};
  };

  /**
   * @brief Allocates a context to store per-client input data.
   * @param input The global input context.
   * @return A unique pointer to a per-client input data context.
   */
  std::unique_ptr<client_input_t> allocate_client_input_context(input_t &input) {
    return std::make_unique<client_input_raw_t>(input);
  }

  /**
   * @brief Compacts the touch slots into a contiguous block and updates the active count.
   * @details Since this swaps entries around, all slot pointers/references are invalid after compaction.
   * @param raw The client-specific input context.
   */
  void perform_touch_compaction(client_input_raw_t *raw) {
    // Windows requires all active touches be contiguous when fed into InjectSyntheticPointerInput().
    UINT32 i;
    for (i = 0; i < ARRAYSIZE(raw->touchInfo); i++) {
      if (raw->touchInfo[i].touchInfo.pointerInfo.pointerFlags == POINTER_FLAG_NONE) {
        // This is an empty slot. Look for a later entry to move into this slot.
        for (UINT32 j = i + 1; j < ARRAYSIZE(raw->touchInfo); j++) {
          if (raw->touchInfo[j].touchInfo.pointerInfo.pointerFlags != POINTER_FLAG_NONE) {
            std::swap(raw->touchInfo[i], raw->touchInfo[j]);
            break;
          }
        }

        // If we didn't find anything, we've reached the end of active slots.
        if (raw->touchInfo[i].touchInfo.pointerInfo.pointerFlags == POINTER_FLAG_NONE) {
          break;
        }
      }
    }

    // Update the number of active touch slots
    raw->activeTouchSlots = i;
  }

  /**
   * @brief Gets a pointer slot by client-relative pointer ID, claiming a new one if necessary.
   * @param raw The raw client-specific input context.
   * @param pointerId The client's pointer ID.
   * @param eventType The LI_TOUCH_EVENT value from the client.
   * @return A pointer to the slot entry.
   */
  POINTER_TYPE_INFO *pointer_by_id(client_input_raw_t *raw, uint32_t pointerId, uint8_t eventType) {
    // Compact active touches into a single contiguous block
    perform_touch_compaction(raw);

    // Try to find a matching pointer ID
    for (UINT32 i = 0; i < ARRAYSIZE(raw->touchInfo); i++) {
      if (raw->touchInfo[i].touchInfo.pointerInfo.pointerId == pointerId &&
          raw->touchInfo[i].touchInfo.pointerInfo.pointerFlags != POINTER_FLAG_NONE) {
        if (eventType == LI_TOUCH_EVENT_DOWN && (raw->touchInfo[i].touchInfo.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT)) {
          BOOST_LOG(warning) << "Pointer "sv << pointerId << " already down. Did the client drop an up/cancel event?"sv;
        }

        return &raw->touchInfo[i];
      }
    }

    if (eventType != LI_TOUCH_EVENT_HOVER && eventType != LI_TOUCH_EVENT_DOWN) {
      BOOST_LOG(warning) << "Unexpected new pointer "sv << pointerId << " for event "sv << (uint32_t) eventType << ". Did the client drop a down/hover event?"sv;
    }

    // If there was none, grab an unused entry and increment the active slot count
    for (UINT32 i = 0; i < ARRAYSIZE(raw->touchInfo); i++) {
      if (raw->touchInfo[i].touchInfo.pointerInfo.pointerFlags == POINTER_FLAG_NONE) {
        raw->touchInfo[i].touchInfo.pointerInfo.pointerId = pointerId;
        raw->activeTouchSlots = i + 1;
        return &raw->touchInfo[i];
      }
    }

    return nullptr;
  }

  /**
   * @brief Populate common `POINTER_INFO` members shared between pen and touch events.
   * @param pointerInfo The pointer info to populate.
   * @param touchPort The current viewport for translating to screen coordinates.
   * @param eventType The type of touch/pen event.
   * @param x The normalized 0.0-1.0 X coordinate.
   * @param y The normalized 0.0-1.0 Y coordinate.
   */
  void populate_common_pointer_info(POINTER_INFO &pointerInfo, const touch_port_t &touchPort, uint8_t eventType, float x, float y) {
    switch (eventType) {
      case LI_TOUCH_EVENT_HOVER:
        pointerInfo.pointerFlags &= ~POINTER_FLAG_INCONTACT;
        pointerInfo.pointerFlags |= POINTER_FLAG_INRANGE | POINTER_FLAG_UPDATE;
        pointerInfo.ptPixelLocation.x = x * touchPort.width + touchPort.offset_x;
        pointerInfo.ptPixelLocation.y = y * touchPort.height + touchPort.offset_y;
        break;
      case LI_TOUCH_EVENT_DOWN:
        pointerInfo.pointerFlags |= POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_DOWN;
        pointerInfo.ptPixelLocation.x = x * touchPort.width + touchPort.offset_x;
        pointerInfo.ptPixelLocation.y = y * touchPort.height + touchPort.offset_y;
        break;
      case LI_TOUCH_EVENT_UP:
        // We expect to get another LI_TOUCH_EVENT_HOVER if the pointer remains in range
        pointerInfo.pointerFlags &= ~(POINTER_FLAG_INCONTACT | POINTER_FLAG_INRANGE);
        pointerInfo.pointerFlags |= POINTER_FLAG_UP;
        break;
      case LI_TOUCH_EVENT_MOVE:
        pointerInfo.pointerFlags |= POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_UPDATE;
        pointerInfo.ptPixelLocation.x = x * touchPort.width + touchPort.offset_x;
        pointerInfo.ptPixelLocation.y = y * touchPort.height + touchPort.offset_y;
        break;
      case LI_TOUCH_EVENT_CANCEL:
      case LI_TOUCH_EVENT_CANCEL_ALL:
        // If we were in contact with the touch surface at the time of the cancellation,
        // we'll set POINTER_FLAG_UP, otherwise set POINTER_FLAG_UPDATE.
        if (pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) {
          pointerInfo.pointerFlags |= POINTER_FLAG_UP;
        } else {
          pointerInfo.pointerFlags |= POINTER_FLAG_UPDATE;
        }
        pointerInfo.pointerFlags &= ~(POINTER_FLAG_INCONTACT | POINTER_FLAG_INRANGE);
        pointerInfo.pointerFlags |= POINTER_FLAG_CANCELED;
        break;
      case LI_TOUCH_EVENT_HOVER_LEAVE:
        pointerInfo.pointerFlags &= ~(POINTER_FLAG_INCONTACT | POINTER_FLAG_INRANGE);
        pointerInfo.pointerFlags |= POINTER_FLAG_UPDATE;
        break;
      case LI_TOUCH_EVENT_BUTTON_ONLY:
        // On Windows, we can only pass buttons if we have an active pointer
        if (pointerInfo.pointerFlags != POINTER_FLAG_NONE) {
          pointerInfo.pointerFlags |= POINTER_FLAG_UPDATE;
        }
        break;
      default:
        BOOST_LOG(warning) << "Unknown touch event: "sv << (uint32_t) eventType;
        break;
    }
  }

  // Active pointer interactions sent via InjectSyntheticPointerInput() seem to be automatically
  // cancelled by Windows if not repeated/updated within about a second. To avoid this, refresh
  // the injected input periodically.
  constexpr auto ISPI_REPEAT_INTERVAL = 50ms;

  /**
   * @brief Repeats the current touch state to avoid the interactions timing out.
   * @param raw The raw client-specific input context.
   */
  void repeat_touch(client_input_raw_t *raw) {
    if (!inject_synthetic_pointer_input(raw->global, raw->touch, raw->touchInfo, raw->activeTouchSlots)) {
      auto err = GetLastError();
      BOOST_LOG(warning) << "Failed to refresh virtual touch input: "sv << err;
    }

    raw->touchRepeatTask = task_pool.pushDelayed(repeat_touch, ISPI_REPEAT_INTERVAL, raw).task_id;
  }

  /**
   * @brief Repeats the current pen state to avoid the interactions timing out.
   * @param raw The raw client-specific input context.
   */
  void repeat_pen(client_input_raw_t *raw) {
    if (!inject_synthetic_pointer_input(raw->global, raw->pen, &raw->penInfo, 1)) {
      auto err = GetLastError();
      BOOST_LOG(warning) << "Failed to refresh virtual pen input: "sv << err;
    }

    raw->penRepeatTask = task_pool.pushDelayed(repeat_pen, ISPI_REPEAT_INTERVAL, raw).task_id;
  }

  /**
   * @brief Cancels all active touches.
   * @param raw The raw client-specific input context.
   */
  void cancel_all_active_touches(client_input_raw_t *raw) {
    // Cancel touch repeat callbacks
    if (raw->touchRepeatTask) {
      task_pool.cancel(raw->touchRepeatTask);
      raw->touchRepeatTask = nullptr;
    }

    // Compact touches to update activeTouchSlots
    perform_touch_compaction(raw);

    // If we have active slots, cancel them all
    if (raw->activeTouchSlots > 0) {
      for (UINT32 i = 0; i < raw->activeTouchSlots; i++) {
        populate_common_pointer_info(raw->touchInfo[i].touchInfo.pointerInfo, {}, LI_TOUCH_EVENT_CANCEL_ALL, 0.0f, 0.0f);
        raw->touchInfo[i].touchInfo.touchMask = TOUCH_MASK_NONE;
      }
      if (!inject_synthetic_pointer_input(raw->global, raw->touch, raw->touchInfo, raw->activeTouchSlots)) {
        auto err = GetLastError();
        BOOST_LOG(warning) << "Failed to cancel all virtual touch input: "sv << err;
      }
    }

    // Zero all touch state
    std::memset(raw->touchInfo, 0, sizeof(raw->touchInfo));
    raw->activeTouchSlots = 0;
  }

  // These are edge-triggered pointer state flags that should always be cleared next frame
  constexpr auto EDGE_TRIGGERED_POINTER_FLAGS = POINTER_FLAG_DOWN | POINTER_FLAG_UP | POINTER_FLAG_CANCELED | POINTER_FLAG_UPDATE;

  /**
   * @brief Sends a touch event to the OS.
   * @param input The client-specific input context.
   * @param touch_port The current viewport for translating to screen coordinates.
   * @param touch The touch event.
   */
  void touch_update(client_input_t *input, const touch_port_t &touch_port, const touch_input_t &touch) {
    auto raw = (client_input_raw_t *) input;

    // Bail if we're not running on an OS that supports virtual touch input
    if (!raw->global->fnCreateSyntheticPointerDevice ||
        !raw->global->fnInjectSyntheticPointerInput ||
        !raw->global->fnDestroySyntheticPointerDevice) {
      BOOST_LOG(warning) << "Touch input requires Windows 10 1809 or later"sv;
      return;
    }

    // If there's not already a virtual touch device, create one now
    if (!raw->touch) {
      if (touch.eventType != LI_TOUCH_EVENT_CANCEL_ALL) {
        BOOST_LOG(info) << "Creating virtual touch input device"sv;
        raw->touch = raw->global->fnCreateSyntheticPointerDevice(PT_TOUCH, ARRAYSIZE(raw->touchInfo), POINTER_FEEDBACK_DEFAULT);
        if (!raw->touch) {
          auto err = GetLastError();
          BOOST_LOG(warning) << "Failed to create virtual touch device: "sv << err;
          return;
        }
      } else {
        // No need to cancel anything if we had no touch input device
        return;
      }
    }

    // Cancel touch repeat callbacks
    if (raw->touchRepeatTask) {
      task_pool.cancel(raw->touchRepeatTask);
      raw->touchRepeatTask = nullptr;
    }

    // If this is a special request to cancel all touches, do that and return
    if (touch.eventType == LI_TOUCH_EVENT_CANCEL_ALL) {
      cancel_all_active_touches(raw);
      return;
    }

    // Find or allocate an entry for this touch pointer ID
    auto pointer = pointer_by_id(raw, touch.pointerId, touch.eventType);
    if (!pointer) {
      BOOST_LOG(error) << "No unused pointer entries! Cancelling all active touches!"sv;
      cancel_all_active_touches(raw);
      pointer = pointer_by_id(raw, touch.pointerId, touch.eventType);
    }

    pointer->type = PT_TOUCH;

    auto &touchInfo = pointer->touchInfo;
    touchInfo.pointerInfo.pointerType = PT_TOUCH;

    // Populate shared pointer info fields
    populate_common_pointer_info(touchInfo.pointerInfo, touch_port, touch.eventType, touch.x, touch.y);

    touchInfo.touchMask = TOUCH_MASK_NONE;

    // Pressure and contact area only apply to in-contact pointers.
    //
    // The clients also pass distance and tool size for hovers, but Windows doesn't
    // provide APIs to receive that data.
    if (touchInfo.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) {
      if (touch.pressureOrDistance != 0.0f) {
        touchInfo.touchMask |= TOUCH_MASK_PRESSURE;

        // Convert the 0.0f..1.0f float to the 0..1024 range that Windows uses
        touchInfo.pressure = (UINT32) (touch.pressureOrDistance * 1024);
      } else {
        // The default touch pressure is 512
        touchInfo.pressure = 512;
      }

      if (touch.contactAreaMajor != 0.0f && touch.contactAreaMinor != 0.0f) {
        // For the purposes of contact area calculation, we will assume the touches
        // are at a 45 degree angle if rotation is unknown. This will scale the major
        // axis value by width and height equally.
        float rotationAngleDegs = touch.rotation == LI_ROT_UNKNOWN ? 45 : touch.rotation;

        float majorAxisAngle = rotationAngleDegs * (M_PI / 180);
        float minorAxisAngle = majorAxisAngle + (M_PI / 2);

        // Estimate the contact rectangle
        float contactWidth = (std::cos(majorAxisAngle) * touch.contactAreaMajor) + (std::cos(minorAxisAngle) * touch.contactAreaMinor);
        float contactHeight = (std::sin(majorAxisAngle) * touch.contactAreaMajor) + (std::sin(minorAxisAngle) * touch.contactAreaMinor);

        // Convert into screen coordinates centered at the touch location and constrained by screen dimensions
        touchInfo.rcContact.left = std::max<LONG>(touch_port.offset_x, touchInfo.pointerInfo.ptPixelLocation.x - std::floor(contactWidth / 2));
        touchInfo.rcContact.right = std::min<LONG>(touch_port.offset_x + touch_port.width, touchInfo.pointerInfo.ptPixelLocation.x + std::ceil(contactWidth / 2));
        touchInfo.rcContact.top = std::max<LONG>(touch_port.offset_y, touchInfo.pointerInfo.ptPixelLocation.y - std::floor(contactHeight / 2));
        touchInfo.rcContact.bottom = std::min<LONG>(touch_port.offset_y + touch_port.height, touchInfo.pointerInfo.ptPixelLocation.y + std::ceil(contactHeight / 2));

        touchInfo.touchMask |= TOUCH_MASK_CONTACTAREA;
      }
    } else {
      touchInfo.pressure = 0;
      touchInfo.rcContact = {};
    }

    if (touch.rotation != LI_ROT_UNKNOWN) {
      touchInfo.touchMask |= TOUCH_MASK_ORIENTATION;
      touchInfo.orientation = touch.rotation;
    } else {
      touchInfo.orientation = 0;
    }

    if (!inject_synthetic_pointer_input(raw->global, raw->touch, raw->touchInfo, raw->activeTouchSlots)) {
      auto err = GetLastError();
      BOOST_LOG(warning) << "Failed to inject virtual touch input: "sv << err;
      return;
    }

    // Clear pointer flags that should only remain set for one frame
    touchInfo.pointerInfo.pointerFlags &= ~EDGE_TRIGGERED_POINTER_FLAGS;

    // If we still have an active touch, refresh the touch state periodically
    if (raw->activeTouchSlots > 1 || touchInfo.pointerInfo.pointerFlags != POINTER_FLAG_NONE) {
      raw->touchRepeatTask = task_pool.pushDelayed(repeat_touch, ISPI_REPEAT_INTERVAL, raw).task_id;
    }
  }

  /**
   * @brief Sends a pen event to the OS.
   * @param input The client-specific input context.
   * @param touch_port The current viewport for translating to screen coordinates.
   * @param pen The pen event.
   */
  void pen_update(client_input_t *input, const touch_port_t &touch_port, const pen_input_t &pen) {
    auto raw = (client_input_raw_t *) input;

    // Bail if we're not running on an OS that supports virtual pen input
    if (!raw->global->fnCreateSyntheticPointerDevice ||
        !raw->global->fnInjectSyntheticPointerInput ||
        !raw->global->fnDestroySyntheticPointerDevice) {
      BOOST_LOG(warning) << "Pen input requires Windows 10 1809 or later"sv;
      return;
    }

    // If there's not already a virtual pen device, create one now
    if (!raw->pen) {
      if (pen.eventType != LI_TOUCH_EVENT_CANCEL_ALL) {
        BOOST_LOG(info) << "Creating virtual pen input device"sv;
        raw->pen = raw->global->fnCreateSyntheticPointerDevice(PT_PEN, 1, POINTER_FEEDBACK_DEFAULT);
        if (!raw->pen) {
          auto err = GetLastError();
          BOOST_LOG(warning) << "Failed to create virtual pen device: "sv << err;
          return;
        }
      } else {
        // No need to cancel anything if we had no pen input device
        return;
      }
    }

    // Cancel pen repeat callbacks
    if (raw->penRepeatTask) {
      task_pool.cancel(raw->penRepeatTask);
      raw->penRepeatTask = nullptr;
    }

    raw->penInfo.type = PT_PEN;

    auto &penInfo = raw->penInfo.penInfo;
    penInfo.pointerInfo.pointerType = PT_PEN;
    penInfo.pointerInfo.pointerId = 0;

    // Populate shared pointer info fields
    populate_common_pointer_info(penInfo.pointerInfo, touch_port, pen.eventType, pen.x, pen.y);

    // Windows only supports a single pen button, so send all buttons as the barrel button
    if (pen.penButtons) {
      penInfo.penFlags |= PEN_FLAG_BARREL;
    } else {
      penInfo.penFlags &= ~PEN_FLAG_BARREL;
    }

    switch (pen.toolType) {
      default:
      case LI_TOOL_TYPE_PEN:
        penInfo.penFlags &= ~PEN_FLAG_ERASER;
        break;
      case LI_TOOL_TYPE_ERASER:
        penInfo.penFlags |= PEN_FLAG_ERASER;
        break;
      case LI_TOOL_TYPE_UNKNOWN:
        // Leave tool flags alone
        break;
    }

    penInfo.penMask = PEN_MASK_NONE;

    // Windows doesn't support hover distance, so only pass pressure/distance when the pointer is in contact
    if ((penInfo.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) && pen.pressureOrDistance != 0.0f) {
      penInfo.penMask |= PEN_MASK_PRESSURE;

      // Convert the 0.0f..1.0f float to the 0..1024 range that Windows uses
      penInfo.pressure = (UINT32) (pen.pressureOrDistance * 1024);
    } else {
      // The default pen pressure is 0
      penInfo.pressure = 0;
    }

    if (pen.rotation != LI_ROT_UNKNOWN) {
      penInfo.penMask |= PEN_MASK_ROTATION;
      penInfo.rotation = pen.rotation;
    } else {
      penInfo.rotation = 0;
    }

    // We require rotation and tilt to perform the conversion to X and Y tilt angles
    if (pen.tilt != LI_TILT_UNKNOWN && pen.rotation != LI_ROT_UNKNOWN) {
      auto rotationRads = pen.rotation * (M_PI / 180.f);
      auto tiltRads = pen.tilt * (M_PI / 180.f);
      auto r = std::sin(tiltRads);
      auto z = std::cos(tiltRads);

      // Convert polar coordinates into X and Y tilt angles
      penInfo.penMask |= PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
      penInfo.tiltX = (INT32) (std::atan2(std::sin(-rotationRads) * r, z) * 180.f / M_PI);
      penInfo.tiltY = (INT32) (std::atan2(std::cos(-rotationRads) * r, z) * 180.f / M_PI);
    } else {
      penInfo.tiltX = 0;
      penInfo.tiltY = 0;
    }

    if (!inject_synthetic_pointer_input(raw->global, raw->pen, &raw->penInfo, 1)) {
      auto err = GetLastError();
      BOOST_LOG(warning) << "Failed to inject virtual pen input: "sv << err;
      return;
    }

    // Clear pointer flags that should only remain set for one frame
    penInfo.pointerInfo.pointerFlags &= ~EDGE_TRIGGERED_POINTER_FLAGS;

    // If we still have an active pen interaction, refresh the pen state periodically
    if (penInfo.pointerInfo.pointerFlags != POINTER_FLAG_NONE) {
      raw->penRepeatTask = task_pool.pushDelayed(repeat_pen, ISPI_REPEAT_INTERVAL, raw).task_id;
    }
  }

  void unicode(input_t &input, char *utf8, int size) {
    // We can do no worse than one UTF-16 character per byte of UTF-8
    WCHAR wide[size];

    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, size, wide, size);
    if (chars <= 0) {
      return;
    }

    // Send all key down events
    for (int i = 0; i < chars; i++) {
      INPUT input {};
      input.type = INPUT_KEYBOARD;
      input.ki.wScan = wide[i];
      input.ki.dwFlags = KEYEVENTF_UNICODE;
      send_input(input);
    }

    // Send all key up events
    for (int i = 0; i < chars; i++) {
      INPUT input {};
      input.type = INPUT_KEYBOARD;
      input.ki.wScan = wide[i];
      input.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
      send_input(input);
    }
  }

  int alloc_gamepad(input_t &input, const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue) {
    auto raw = (input_raw_t *) input.get();

    if (!raw->vigem) {
      return 0;
    }

    VIGEM_TARGET_TYPE selectedGamepadType;

    if (config::input.gamepad == "x360"sv) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox 360 controller (manual selection)"sv;
      selectedGamepadType = Xbox360Wired;
    } else if (config::input.gamepad == "ds4"sv) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (manual selection)"sv;
      selectedGamepadType = DualShock4Wired;
    } else if (metadata.type == LI_CTYPE_PS) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (auto-selected by client-reported type)"sv;
      selectedGamepadType = DualShock4Wired;
    } else if (metadata.type == LI_CTYPE_XBOX) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox 360 controller (auto-selected by client-reported type)"sv;
      selectedGamepadType = Xbox360Wired;
    } else if (config::input.motion_as_ds4 && (metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO))) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (auto-selected by motion sensor presence)"sv;
      selectedGamepadType = DualShock4Wired;
    } else if (config::input.touchpad_as_ds4 && (metadata.capabilities & LI_CCAP_TOUCHPAD)) {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be DualShock 4 controller (auto-selected by touchpad presence)"sv;
      selectedGamepadType = DualShock4Wired;
    } else {
      BOOST_LOG(info) << "Gamepad " << id.globalIndex << " will be Xbox 360 controller (default)"sv;
      selectedGamepadType = Xbox360Wired;
    }

    if (selectedGamepadType == Xbox360Wired) {
      if (metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO)) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has motion sensors, but they are not usable when emulating an Xbox 360 controller"sv;
      }
      if (metadata.capabilities & LI_CCAP_TOUCHPAD) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has a touchpad, but it is not usable when emulating an Xbox 360 controller"sv;
      }
      if (metadata.capabilities & LI_CCAP_RGB_LED) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " has an RGB LED, but it is not usable when emulating an Xbox 360 controller"sv;
      }
    } else if (selectedGamepadType == DualShock4Wired) {
      if (!(metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO))) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " is emulating a DualShock 4 controller, but the client gamepad doesn't have motion sensors active"sv;
      }
      if (!(metadata.capabilities & LI_CCAP_TOUCHPAD)) {
        BOOST_LOG(warning) << "Gamepad " << id.globalIndex << " is emulating a DualShock 4 controller, but the client gamepad doesn't have a touchpad"sv;
      }
    }

    return raw->vigem->alloc_gamepad_internal(id, feedback_queue, selectedGamepadType);
  }

  void free_gamepad(input_t &input, int nr) {
    auto raw = (input_raw_t *) input.get();

    if (!raw->vigem) {
      return;
    }

    raw->vigem->free_target(nr);
  }

  /**
   * @brief Converts the standard button flags into X360 format.
   * @param gamepad_state The gamepad button/axis state sent from the client.
   * @return XUSB_BUTTON flags.
   */
  static XUSB_BUTTON x360_buttons(const gamepad_state_t &gamepad_state) {
    int buttons {};

    auto flags = gamepad_state.buttonFlags;
    if (flags & DPAD_UP) {
      buttons |= XUSB_GAMEPAD_DPAD_UP;
    }
    if (flags & DPAD_DOWN) {
      buttons |= XUSB_GAMEPAD_DPAD_DOWN;
    }
    if (flags & DPAD_LEFT) {
      buttons |= XUSB_GAMEPAD_DPAD_LEFT;
    }
    if (flags & DPAD_RIGHT) {
      buttons |= XUSB_GAMEPAD_DPAD_RIGHT;
    }
    if (flags & START) {
      buttons |= XUSB_GAMEPAD_START;
    }
    if (flags & BACK) {
      buttons |= XUSB_GAMEPAD_BACK;
    }
    if (flags & LEFT_STICK) {
      buttons |= XUSB_GAMEPAD_LEFT_THUMB;
    }
    if (flags & RIGHT_STICK) {
      buttons |= XUSB_GAMEPAD_RIGHT_THUMB;
    }
    if (flags & LEFT_BUTTON) {
      buttons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    }
    if (flags & RIGHT_BUTTON) {
      buttons |= XUSB_GAMEPAD_RIGHT_SHOULDER;
    }
    if (flags & (HOME | MISC_BUTTON)) {
      buttons |= XUSB_GAMEPAD_GUIDE;
    }
    if (flags & A) {
      buttons |= XUSB_GAMEPAD_A;
    }
    if (flags & B) {
      buttons |= XUSB_GAMEPAD_B;
    }
    if (flags & X) {
      buttons |= XUSB_GAMEPAD_X;
    }
    if (flags & Y) {
      buttons |= XUSB_GAMEPAD_Y;
    }

    return (XUSB_BUTTON) buttons;
  }

  /**
   * @brief Updates the X360 input report with the provided gamepad state.
   * @param gamepad The gamepad to update.
   * @param gamepad_state The gamepad button/axis state sent from the client.
   */
  static void x360_update_state(gamepad_context_t &gamepad, const gamepad_state_t &gamepad_state) {
    auto &report = gamepad.report.x360;

    report.wButtons = x360_buttons(gamepad_state);
    report.bLeftTrigger = gamepad_state.lt;
    report.bRightTrigger = gamepad_state.rt;
    report.sThumbLX = gamepad_state.lsX;
    report.sThumbLY = gamepad_state.lsY;
    report.sThumbRX = gamepad_state.rsX;
    report.sThumbRY = gamepad_state.rsY;
  }

  static DS4_DPAD_DIRECTIONS ds4_dpad(const gamepad_state_t &gamepad_state) {
    auto flags = gamepad_state.buttonFlags;
    if (flags & DPAD_UP) {
      if (flags & DPAD_RIGHT) {
        return DS4_BUTTON_DPAD_NORTHEAST;
      } else if (flags & DPAD_LEFT) {
        return DS4_BUTTON_DPAD_NORTHWEST;
      } else {
        return DS4_BUTTON_DPAD_NORTH;
      }
    }

    else if (flags & DPAD_DOWN) {
      if (flags & DPAD_RIGHT) {
        return DS4_BUTTON_DPAD_SOUTHEAST;
      } else if (flags & DPAD_LEFT) {
        return DS4_BUTTON_DPAD_SOUTHWEST;
      } else {
        return DS4_BUTTON_DPAD_SOUTH;
      }
    }

    else if (flags & DPAD_RIGHT) {
      return DS4_BUTTON_DPAD_EAST;
    }

    else if (flags & DPAD_LEFT) {
      return DS4_BUTTON_DPAD_WEST;
    }

    return DS4_BUTTON_DPAD_NONE;
  }

  /**
   * @brief Converts the standard button flags into DS4 format.
   * @param gamepad_state The gamepad button/axis state sent from the client.
   * @return DS4_BUTTONS flags.
   */
  static DS4_BUTTONS ds4_buttons(const gamepad_state_t &gamepad_state) {
    int buttons {};

    auto flags = gamepad_state.buttonFlags;
    if (flags & LEFT_STICK) {
      buttons |= DS4_BUTTON_THUMB_LEFT;
    }
    if (flags & RIGHT_STICK) {
      buttons |= DS4_BUTTON_THUMB_RIGHT;
    }
    if (flags & LEFT_BUTTON) {
      buttons |= DS4_BUTTON_SHOULDER_LEFT;
    }
    if (flags & RIGHT_BUTTON) {
      buttons |= DS4_BUTTON_SHOULDER_RIGHT;
    }
    if (flags & START) {
      buttons |= DS4_BUTTON_OPTIONS;
    }
    if (flags & BACK) {
      buttons |= DS4_BUTTON_SHARE;
    }
    if (flags & A) {
      buttons |= DS4_BUTTON_CROSS;
    }
    if (flags & B) {
      buttons |= DS4_BUTTON_CIRCLE;
    }
    if (flags & X) {
      buttons |= DS4_BUTTON_SQUARE;
    }
    if (flags & Y) {
      buttons |= DS4_BUTTON_TRIANGLE;
    }

    if (gamepad_state.lt > 0) {
      buttons |= DS4_BUTTON_TRIGGER_LEFT;
    }
    if (gamepad_state.rt > 0) {
      buttons |= DS4_BUTTON_TRIGGER_RIGHT;
    }

    return (DS4_BUTTONS) buttons;
  }

  static DS4_SPECIAL_BUTTONS ds4_special_buttons(const gamepad_state_t &gamepad_state) {
    int buttons {};

    if (gamepad_state.buttonFlags & HOME) {
      buttons |= DS4_SPECIAL_BUTTON_PS;
    }

    // Allow either PS4/PS5 clickpad button or Xbox Series X share button to activate DS4 clickpad
    if (gamepad_state.buttonFlags & (TOUCHPAD_BUTTON | MISC_BUTTON)) {
      buttons |= DS4_SPECIAL_BUTTON_TOUCHPAD;
    }

    // Manual DS4 emulation: check if BACK button should also trigger DS4 touchpad click
    if (config::input.gamepad == "ds4"sv && config::input.ds4_back_as_touchpad_click && (gamepad_state.buttonFlags & BACK)) {
      buttons |= DS4_SPECIAL_BUTTON_TOUCHPAD;
    }

    return (DS4_SPECIAL_BUTTONS) buttons;
  }

  static std::uint8_t to_ds4_triggerX(std::int16_t v) {
    return (v + std::numeric_limits<std::uint16_t>::max() / 2 + 1) / 257;
  }

  static std::uint8_t to_ds4_triggerY(std::int16_t v) {
    auto new_v = -((std::numeric_limits<std::uint16_t>::max() / 2 + v - 1)) / 257;

    return new_v == 0 ? 0xFF : (std::uint8_t) new_v;
  }

  /**
   * @brief Updates the DS4 input report with the provided gamepad state.
   * @param gamepad The gamepad to update.
   * @param gamepad_state The gamepad button/axis state sent from the client.
   */
  static void ds4_update_state(gamepad_context_t &gamepad, const gamepad_state_t &gamepad_state) {
    auto &report = gamepad.report.ds4.Report;

    report.wButtons = static_cast<uint16_t>(ds4_buttons(gamepad_state)) | static_cast<uint16_t>(ds4_dpad(gamepad_state));
    report.bSpecial = ds4_special_buttons(gamepad_state);

    report.bTriggerL = gamepad_state.lt;
    report.bTriggerR = gamepad_state.rt;

    report.bThumbLX = to_ds4_triggerX(gamepad_state.lsX);
    report.bThumbLY = to_ds4_triggerY(gamepad_state.lsY);

    report.bThumbRX = to_ds4_triggerX(gamepad_state.rsX);
    report.bThumbRY = to_ds4_triggerY(gamepad_state.rsY);
  }

  /**
   * @brief Sends DS4 input with updated timestamps and repeats to keep timestamp updated.
   * @details Some applications require updated timestamps values to register DS4 input.
   * @param vigem The global ViGEm context object.
   * @param nr The global gamepad index.
   */
  void ds4_update_ts_and_send(vigem_t *vigem, int nr) {
    auto &gamepad = vigem->gamepads[nr];

    // Cancel any pending updates. We will requeue one here when we're finished.
    if (gamepad.repeat_task) {
      task_pool.cancel(gamepad.repeat_task);
      gamepad.repeat_task = nullptr;
    }

    if (gamepad.gp && vigem_target_is_attached(gamepad.gp.get())) {
      auto now = std::chrono::steady_clock::now();
      auto delta_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - gamepad.last_report_ts);

      // Timestamp is reported in 5.333us units
      gamepad.report.ds4.Report.wTimestamp += (uint16_t) (delta_ns.count() / 5333);

      // Send the report to the virtual device
      auto status = vigem_target_ds4_update_ex(vigem->client.get(), gamepad.gp.get(), gamepad.report.ds4);
      if (!VIGEM_SUCCESS(status)) {
        BOOST_LOG(warning) << "Couldn't send gamepad input to ViGEm ["sv << util::hex(status).to_string_view() << ']';
        return;
      }

      // Repeat at least every 100ms to keep the 16-bit timestamp field from overflowing
      gamepad.last_report_ts = now;
      gamepad.repeat_task = task_pool.pushDelayed(ds4_update_ts_and_send, 100ms, vigem, nr).task_id;
    }
  }

  /**
   * @brief Updates virtual gamepad with the provided gamepad state.
   * @param input The input context.
   * @param nr The gamepad index to update.
   * @param gamepad_state The gamepad button/axis state sent from the client.
   */
  void gamepad_update(input_t &input, int nr, const gamepad_state_t &gamepad_state) {
    auto vigem = ((input_raw_t *) input.get())->vigem;

    // If there is no gamepad support
    if (!vigem) {
      return;
    }

    auto &gamepad = vigem->gamepads[nr];
    if (!gamepad.gp) {
      return;
    }

    VIGEM_ERROR status;

    if (vigem_target_get_type(gamepad.gp.get()) == Xbox360Wired) {
      x360_update_state(gamepad, gamepad_state);
      status = vigem_target_x360_update(vigem->client.get(), gamepad.gp.get(), gamepad.report.x360);
      if (!VIGEM_SUCCESS(status)) {
        BOOST_LOG(warning) << "Couldn't send gamepad input to ViGEm ["sv << util::hex(status).to_string_view() << ']';
      }
    } else {
      ds4_update_state(gamepad, gamepad_state);
      ds4_update_ts_and_send(vigem, nr);
    }
  }

  /**
   * @brief Sends a gamepad touch event to the OS.
   * @param input The global input context.
   * @param touch The touch event.
   */
  void gamepad_touch(input_t &input, const gamepad_touch_t &touch) {
    auto vigem = ((input_raw_t *) input.get())->vigem;

    // If there is no gamepad support
    if (!vigem) {
      return;
    }

    auto &gamepad = vigem->gamepads[touch.id.globalIndex];
    if (!gamepad.gp) {
      return;
    }

    // Touch is only supported on DualShock 4 controllers
    if (vigem_target_get_type(gamepad.gp.get()) != DualShock4Wired) {
      return;
    }

    auto &report = gamepad.report.ds4.Report;

    uint8_t pointerIndex;
    if (touch.eventType == LI_TOUCH_EVENT_DOWN) {
      if (gamepad.available_pointers & 0x1) {
        // Reserve pointer index 0 for this touch
        gamepad.pointer_id_map[touch.pointerId] = pointerIndex = 0;
        gamepad.available_pointers &= ~(1 << pointerIndex);

        // Set pointer 0 down
        report.sCurrentTouch.bIsUpTrackingNum1 &= ~0x80;
        report.sCurrentTouch.bIsUpTrackingNum1++;
      } else if (gamepad.available_pointers & 0x2) {
        // Reserve pointer index 1 for this touch
        gamepad.pointer_id_map[touch.pointerId] = pointerIndex = 1;
        gamepad.available_pointers &= ~(1 << pointerIndex);

        // Set pointer 1 down
        report.sCurrentTouch.bIsUpTrackingNum2 &= ~0x80;
        report.sCurrentTouch.bIsUpTrackingNum2++;
      } else {
        BOOST_LOG(warning) << "No more free pointer indices! Did the client miss an touch up event?"sv;
        return;
      }
    } else if (touch.eventType == LI_TOUCH_EVENT_CANCEL_ALL) {
      // Raise both pointers
      report.sCurrentTouch.bIsUpTrackingNum1 |= 0x80;
      report.sCurrentTouch.bIsUpTrackingNum2 |= 0x80;

      // Remove all pointer index mappings
      gamepad.pointer_id_map.clear();

      // All pointers are now available
      gamepad.available_pointers = 0x3;
    } else {
      auto i = gamepad.pointer_id_map.find(touch.pointerId);
      if (i == gamepad.pointer_id_map.end()) {
        BOOST_LOG(warning) << "Pointer ID not found! Did the client miss a touch down event?"sv;
        return;
      }

      pointerIndex = (*i).second;

      if (touch.eventType == LI_TOUCH_EVENT_UP || touch.eventType == LI_TOUCH_EVENT_CANCEL) {
        // Remove the pointer index mapping
        gamepad.pointer_id_map.erase(i);

        // Set pointer up
        if (pointerIndex == 0) {
          report.sCurrentTouch.bIsUpTrackingNum1 |= 0x80;
        } else {
          report.sCurrentTouch.bIsUpTrackingNum2 |= 0x80;
        }

        // Free the pointer index
        gamepad.available_pointers |= (1 << pointerIndex);
      } else if (touch.eventType != LI_TOUCH_EVENT_MOVE) {
        BOOST_LOG(warning) << "Unsupported touch event for gamepad: "sv << (uint32_t) touch.eventType;
        return;
      }
    }

    // Touchpad is 1920x943 according to ViGEm
    uint16_t x = touch.x * 1920;
    uint16_t y = touch.y * 943;
    uint8_t touchData[] = {
      (uint8_t) (x & 0xFF),  // Low 8 bits of X
      (uint8_t) ((x >> 8 & 0x0F) | (y & 0x0F) << 4),  // High 4 bits of X and low 4 bits of Y
      (uint8_t) (y >> 4 & 0xFF)  // High 8 bits of Y
    };

    report.sCurrentTouch.bPacketCounter++;
    if (touch.eventType != LI_TOUCH_EVENT_CANCEL_ALL) {
      if (pointerIndex == 0) {
        memcpy(report.sCurrentTouch.bTouchData1, touchData, sizeof(touchData));
      } else {
        memcpy(report.sCurrentTouch.bTouchData2, touchData, sizeof(touchData));
      }
    }

    ds4_update_ts_and_send(vigem, touch.id.globalIndex);
  }

  /**
   * @brief Sends a gamepad motion event to the OS.
   * @param input The global input context.
   * @param motion The motion event.
   */
  void gamepad_motion(input_t &input, const gamepad_motion_t &motion) {
    auto vigem = ((input_raw_t *) input.get())->vigem;

    // If there is no gamepad support
    if (!vigem) {
      return;
    }

    auto &gamepad = vigem->gamepads[motion.id.globalIndex];
    if (!gamepad.gp) {
      return;
    }

    // Motion is only supported on DualShock 4 controllers
    if (vigem_target_get_type(gamepad.gp.get()) != DualShock4Wired) {
      return;
    }

    ds4_update_motion(gamepad, motion.motionType, motion.x, motion.y, motion.z);
    ds4_update_ts_and_send(vigem, motion.id.globalIndex);
  }

  /**
   * @brief Sends a gamepad battery event to the OS.
   * @param input The global input context.
   * @param battery The battery event.
   */
  void gamepad_battery(input_t &input, const gamepad_battery_t &battery) {
    auto vigem = ((input_raw_t *) input.get())->vigem;

    // If there is no gamepad support
    if (!vigem) {
      return;
    }

    auto &gamepad = vigem->gamepads[battery.id.globalIndex];
    if (!gamepad.gp) {
      return;
    }

    // Battery is only supported on DualShock 4 controllers
    if (vigem_target_get_type(gamepad.gp.get()) != DualShock4Wired) {
      return;
    }

    // For details on the report format of these battery level fields, see:
    // https://github.com/torvalds/linux/blob/946c6b59c56dc6e7d8364a8959cb36bf6d10bc37/drivers/hid/hid-playstation.c#L2305-L2314

    auto &report = gamepad.report.ds4.Report;

    // Update the battery state if it is known
    switch (battery.state) {
      case LI_BATTERY_STATE_CHARGING:
      case LI_BATTERY_STATE_DISCHARGING:
        if (battery.state == LI_BATTERY_STATE_CHARGING) {
          report.bBatteryLvlSpecial |= 0x10;  // Connected via USB
        } else {
          report.bBatteryLvlSpecial &= ~0x10;  // Not connected via USB
        }

        // If there was a special battery status set before, clear that and
        // initialize the battery level to 50%. It will be overwritten below
        // if the actual percentage is known.
        if ((report.bBatteryLvlSpecial & 0xF) > 0xA) {
          report.bBatteryLvlSpecial = (report.bBatteryLvlSpecial & ~0xF) | 0x5;
        }
        break;

      case LI_BATTERY_STATE_FULL:
        report.bBatteryLvlSpecial = 0x1B;  // USB + Battery Full
        report.bBatteryLvl = 0xFF;
        break;

      case LI_BATTERY_STATE_NOT_PRESENT:
      case LI_BATTERY_STATE_NOT_CHARGING:
        report.bBatteryLvlSpecial = 0x1F;  // USB + Charging Error
        break;

      default:
        break;
    }

    // Update the battery level if it is known
    if (battery.percentage != LI_BATTERY_PERCENTAGE_UNKNOWN) {
      report.bBatteryLvl = battery.percentage * 255 / 100;

      // Don't overwrite low nibble if there's a special status there (see above)
      if ((report.bBatteryLvlSpecial & 0x10) && (report.bBatteryLvlSpecial & 0xF) <= 0xA) {
        report.bBatteryLvlSpecial = (report.bBatteryLvlSpecial & ~0xF) | ((battery.percentage + 5) / 10);
      }
    }

    ds4_update_ts_and_send(vigem, battery.id.globalIndex);
  }

  void freeInput(void *p) {
    auto input = (input_raw_t *) p;

    delete input;
  }

  std::vector<supported_gamepad_t> &supported_gamepads(input_t *input) {
    if (!input) {
      static std::vector gps {
        supported_gamepad_t {"auto", true, ""},
        supported_gamepad_t {"x360", false, ""},
        supported_gamepad_t {"ds4", false, ""},
      };

      return gps;
    }

    auto vigem = ((input_raw_t *) input)->vigem;
    auto enabled = vigem != nullptr;
    auto reason = enabled ? "" : "gamepads.vigem-not-available";

    // ds4 == ps4
    static std::vector gps {
      supported_gamepad_t {"auto", true, reason},
      supported_gamepad_t {"x360", enabled, reason},
      supported_gamepad_t {"ds4", enabled, reason}
    };

    for (auto &[name, is_enabled, reason_disabled] : gps) {
      if (!is_enabled) {
        BOOST_LOG(warning) << "Gamepad " << name << " is disabled due to " << reason_disabled;
      }
    }

    return gps;
  }

  /**
   * @brief Returns the supported platform capabilities to advertise to the client.
   * @return Capability flags.
   */
  platform_caps::caps_t get_capabilities() {
    platform_caps::caps_t caps = 0;

    // We support controller touchpad input as long as we're not emulating X360
    if (config::input.gamepad != "x360"sv) {
      caps |= platform_caps::controller_touch;
    }

    // We support pen and touch input on Win10 1809+
    if (GetProcAddress(GetModuleHandleA("user32.dll"), "CreateSyntheticPointerDevice") != nullptr) {
      if (config::input.native_pen_touch) {
        caps |= platform_caps::pen_touch;
      }
    } else {
      BOOST_LOG(warning) << "Touch input requires Windows 10 1809 or later"sv;
    }

    return caps;
  }
}  // namespace platf
