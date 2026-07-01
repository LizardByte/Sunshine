/**
 * @file src/platform/virtualhid_input.cpp
 * @brief Definitions for libvirtualhid-backed input helpers.
 */

// standard includes
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <string_view>
#include <utility>

// local includes
#include "src/config.h"
#include "src/logging.h"
#include "virtualhid_input.h"

using namespace std::literals;

namespace platf::virtualhid {
  /**
   * @brief Runtime state for one virtual gamepad.
   */
  struct gamepad_context_t {
    std::unique_ptr<lvh::GamepadStateAdapter> adapter;  ///< State adapter for the virtual gamepad.
    feedback_queue_t feedback_queue;  ///< Feedback queue for client output events.
    std::array<std::optional<std::uint32_t>, 2> touch_ids;  ///< Client touch IDs assigned to libvirtualhid slots.
    std::uint8_t client_relative_index = 0;  ///< Client-relative controller index.
    bool has_last_rumble = false;  ///< Whether last rumble values are valid.
    std::uint16_t last_low_frequency_rumble = 0;  ///< Last low-frequency rumble value.
    std::uint16_t last_high_frequency_rumble = 0;  ///< Last high-frequency rumble value.
    bool has_last_trigger_rumble = false;  ///< Whether last trigger rumble values are valid.
    std::uint16_t last_left_trigger_rumble = 0;  ///< Last left trigger rumble value.
    std::uint16_t last_right_trigger_rumble = 0;  ///< Last right trigger rumble value.
    bool has_last_rgb = false;  ///< Whether last RGB values are valid.
    std::uint8_t last_red = 0;  ///< Last red LED value.
    std::uint8_t last_green = 0;  ///< Last green LED value.
    std::uint8_t last_blue = 0;  ///< Last blue LED value.
  };

  namespace {

    /**
     * @brief Gamepad profile exposed through Sunshine config.
     */
    struct gamepad_profile_t {
      std::string_view name;  ///< Sunshine config value.
      lvh::GamepadProfileKind kind;  ///< libvirtualhid profile kind.
      lvh::DeviceProfile (*profile)();  ///< Profile factory.
    };

    /**
     * @brief Supported libvirtualhid gamepad profiles.
     */
    constexpr std::array gamepad_profiles {
      gamepad_profile_t {"generic", lvh::GamepadProfileKind::generic, lvh::profiles::generic_gamepad},
      gamepad_profile_t {"x360", lvh::GamepadProfileKind::xbox_360, lvh::profiles::xbox_360},
      gamepad_profile_t {"xone", lvh::GamepadProfileKind::xbox_one, lvh::profiles::xbox_one},
      gamepad_profile_t {"xseries", lvh::GamepadProfileKind::xbox_series, lvh::profiles::xbox_series},
      gamepad_profile_t {"ds4", lvh::GamepadProfileKind::dualshock4, lvh::profiles::dualshock4},
      gamepad_profile_t {"ds5", lvh::GamepadProfileKind::dualsense, lvh::profiles::dualsense},
      gamepad_profile_t {"switch", lvh::GamepadProfileKind::switch_pro, lvh::profiles::switch_pro},
    };

    void log_failure(std::string_view operation, const lvh::OperationStatus &status) {
      if (!status.ok()) {
        BOOST_LOG(warning) << operation << ": "sv << status.message();
      }
    }

    float normalize_axis(std::int16_t value) {
      if (value < 0) {
        return std::max(-1.0F, static_cast<float>(value) / 32768.0F);
      }

      return std::min(1.0F, static_cast<float>(value) / 32767.0F);
    }

    float normalize_trigger(std::uint8_t value) {
      return static_cast<float>(value) / static_cast<float>(std::numeric_limits<std::uint8_t>::max());
    }

    lvh::ClientControllerType client_controller_type(std::uint8_t type) {
      switch (type) {
        case LI_CTYPE_XBOX:
          return lvh::ClientControllerType::xbox;
        case LI_CTYPE_PS:
          return lvh::ClientControllerType::playstation;
        case LI_CTYPE_NINTENDO:
          return lvh::ClientControllerType::nintendo;
        case LI_CTYPE_UNKNOWN:
        default:
          return lvh::ClientControllerType::unknown;
      }
    }

    const gamepad_profile_t &profile_for_name(std::string_view name) {
      const auto iter = std::ranges::find(gamepad_profiles, name, &gamepad_profile_t::name);
      if (iter != gamepad_profiles.end()) {
        return *iter;
      }

      return gamepad_profiles[3];  // Xbox Series.
    }

    const gamepad_profile_t &profile_for_metadata(const gamepad_arrival_t &metadata) {
      if (config::input.gamepad != "auto"sv) {
        return profile_for_name(config::input.gamepad);
      }

      if (metadata.type == LI_CTYPE_PS) {
        BOOST_LOG(info) << "Gamepad will be DualSense controller (auto-selected by client-reported type)"sv;
        return profile_for_name("ds5"sv);
      }
      if (metadata.type == LI_CTYPE_NINTENDO) {
        BOOST_LOG(info) << "Gamepad will be Nintendo Switch Pro controller (auto-selected by client-reported type)"sv;
        return profile_for_name("switch"sv);
      }
      if (metadata.type == LI_CTYPE_XBOX) {
        BOOST_LOG(info) << "Gamepad will be Xbox Series controller (auto-selected by client-reported type)"sv;
        return profile_for_name("xseries"sv);
      }
      if (config::input.motion_as_ds4 && (metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO))) {
        BOOST_LOG(info) << "Gamepad will be DualSense controller (auto-selected by motion sensor presence)"sv;
        return profile_for_name("ds5"sv);
      }
      if (config::input.touchpad_as_ds4 && (metadata.capabilities & LI_CCAP_TOUCHPAD)) {
        BOOST_LOG(info) << "Gamepad will be DualSense controller (auto-selected by touchpad presence)"sv;
        return profile_for_name("ds5"sv);
      }

      BOOST_LOG(info) << "Gamepad will be Xbox Series controller (default)"sv;
      return profile_for_name("xseries"sv);
    }

    std::string random_private_mac() {
      std::random_device random;
      return std::format(
        "02:00:{:02x}:{:02x}:{:02x}:{:02x}",
        random() & 0xFF,
        random() & 0xFF,
        random() & 0xFF,
        random() & 0xFF
      );
    }

    std::string gamepad_stable_id(const gamepad_id_t &id, const lvh::DeviceProfile &profile) {
      if (profile.gamepad_kind != lvh::GamepadProfileKind::dualshock4 &&
          profile.gamepad_kind != lvh::GamepadProfileKind::dualsense) {
        return std::format("sunshine-gamepad-{}", id.globalIndex);
      }

      if (config::input.virtualhid_randomize_mac || id.globalIndex < 0 || id.globalIndex > 255) {
        return random_private_mac();
      }

      return std::format("02:00:00:00:00:{:02x}", id.globalIndex);
    }

    lvh::GamepadMetadata gamepad_metadata(const gamepad_id_t &id, const gamepad_arrival_t &metadata, const lvh::DeviceProfile &profile) {
      lvh::GamepadMetadata result;
      result.global_index = id.globalIndex;
      result.client_relative_index = id.clientRelativeIndex;
      result.client_type = client_controller_type(metadata.type);
      result.has_motion_sensors = metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO);
      result.has_touchpad = metadata.capabilities & LI_CCAP_TOUCHPAD;
      result.has_rgb_led = metadata.capabilities & LI_CCAP_RGB_LED;
      result.has_battery = metadata.capabilities & LI_CCAP_BATTERY_STATE;
      result.stable_id = gamepad_stable_id(id, profile);
      return result;
    }

    void warn_unsupported_client_features(int global_index, const gamepad_arrival_t &metadata, const lvh::GamepadProfileSupport &support) {
      if ((metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO)) && !support.supports_motion) {
        BOOST_LOG(warning) << "Gamepad "sv << global_index << " has motion sensors, but the selected virtual profile cannot expose them"sv;
      }
      if ((metadata.capabilities & LI_CCAP_TOUCHPAD) && !support.supports_touchpad) {
        BOOST_LOG(warning) << "Gamepad "sv << global_index << " has a touchpad, but the selected virtual profile cannot expose it"sv;
      }
      if ((metadata.capabilities & LI_CCAP_RGB_LED) && !support.supports_rgb_led) {
        BOOST_LOG(warning) << "Gamepad "sv << global_index << " has an RGB LED, but the selected virtual profile cannot expose it"sv;
      }
    }

    void warn_missing_client_features(int global_index, const gamepad_arrival_t &metadata, const lvh::GamepadProfileSupport &support) {
      if (support.supports_motion && !(metadata.capabilities & (LI_CCAP_ACCEL | LI_CCAP_GYRO))) {
        BOOST_LOG(warning) << "Gamepad "sv << global_index << " is emulating a motion-capable controller, but the client gamepad does not have motion sensors active"sv;
      }
      if (support.supports_touchpad && !(metadata.capabilities & LI_CCAP_TOUCHPAD)) {
        BOOST_LOG(warning) << "Gamepad "sv << global_index << " is emulating a touchpad-capable controller, but the client gamepad does not have a touchpad"sv;
      }
    }

    lvh::GamepadState make_gamepad_state(const gamepad_state_t &state, const lvh::GamepadProfileSupport &support) {
      lvh::GamepadState result;
      const auto flags = state.buttonFlags;

      result.buttons.set(lvh::GamepadButton::dpad_up, flags & DPAD_UP);
      result.buttons.set(lvh::GamepadButton::dpad_down, flags & DPAD_DOWN);
      result.buttons.set(lvh::GamepadButton::dpad_left, flags & DPAD_LEFT);
      result.buttons.set(lvh::GamepadButton::dpad_right, flags & DPAD_RIGHT);
      result.buttons.set(lvh::GamepadButton::start, flags & START);
      result.buttons.set(lvh::GamepadButton::back, flags & BACK);
      result.buttons.set(lvh::GamepadButton::left_stick, flags & LEFT_STICK);
      result.buttons.set(lvh::GamepadButton::right_stick, flags & RIGHT_STICK);
      result.buttons.set(lvh::GamepadButton::left_shoulder, flags & LEFT_BUTTON);
      result.buttons.set(lvh::GamepadButton::right_shoulder, flags & RIGHT_BUTTON);
      result.buttons.set(lvh::GamepadButton::guide, flags & HOME);
      result.buttons.set(lvh::GamepadButton::a, flags & A);
      result.buttons.set(lvh::GamepadButton::b, flags & B);
      result.buttons.set(lvh::GamepadButton::x, flags & X);
      result.buttons.set(lvh::GamepadButton::y, flags & Y);
      result.buttons.set(lvh::GamepadButton::misc1, support.supports_misc1_button && (flags & MISC_BUTTON));
      result.buttons.set(lvh::GamepadButton::touchpad, support.supports_touchpad_button && (flags & TOUCHPAD_BUTTON));

      if (support.supports_touchpad_button &&
          config::input.ds4_back_as_touchpad_click &&
          (config::input.gamepad == "ds4"sv || config::input.gamepad == "ds5"sv) &&
          (flags & BACK)) {
        result.buttons.set(lvh::GamepadButton::touchpad);
      }

      result.left_stick = {.x = normalize_axis(state.lsX), .y = normalize_axis(state.lsY)};
      result.right_stick = {.x = normalize_axis(state.rsX), .y = normalize_axis(state.rsY)};
      result.left_trigger = normalize_trigger(state.lt);
      result.right_trigger = normalize_trigger(state.rt);
      return result;
    }

    std::optional<lvh::MouseButton> mouse_button(int button) {
      switch (button) {
        case BUTTON_LEFT:
          return lvh::MouseButton::left;
        case BUTTON_MIDDLE:
          return lvh::MouseButton::middle;
        case BUTTON_RIGHT:
          return lvh::MouseButton::right;
        case BUTTON_X1:
          return lvh::MouseButton::side;
        case BUTTON_X2:
          return lvh::MouseButton::extra;
        default:
          BOOST_LOG(warning) << "Unknown mouse button: "sv << button;
          return std::nullopt;
      }
    }

    lvh::GamepadBatteryState battery_state(std::uint8_t state) {
      switch (state) {
        case LI_BATTERY_STATE_DISCHARGING:
          return lvh::GamepadBatteryState::discharging;
        case LI_BATTERY_STATE_CHARGING:
          return lvh::GamepadBatteryState::charging;
        case LI_BATTERY_STATE_FULL:
          return lvh::GamepadBatteryState::full;
        case LI_BATTERY_STATE_NOT_PRESENT:
        case LI_BATTERY_STATE_NOT_CHARGING:
          return lvh::GamepadBatteryState::charging_error;
        case LI_BATTERY_STATE_UNKNOWN:
        default:
          return lvh::GamepadBatteryState::unknown;
      }
    }

    std::int32_t touch_orientation(std::uint16_t rotation) {
      if (rotation == LI_ROT_UNKNOWN) {
        return 0;
      }

      auto adjusted = static_cast<int>(rotation);
      if (adjusted > 90 && adjusted < 270) {
        adjusted = 180 - adjusted;
      }
      if (adjusted > 90) {
        adjusted -= 360;
      } else if (adjusted < -90) {
        adjusted += 360;
      }

      return adjusted;
    }

    lvh::PenToolType pen_tool(std::uint8_t tool) {
      switch (tool) {
        case LI_TOOL_TYPE_PEN:
          return lvh::PenToolType::pen;
        case LI_TOOL_TYPE_ERASER:
          return lvh::PenToolType::eraser;
        case LI_TOOL_TYPE_UNKNOWN:
        default:
          return lvh::PenToolType::unchanged;
      }
    }

    void raise_feedback(const std::shared_ptr<gamepad_context_t> &gamepad, const gamepad_feedback_msg_t &message) {
      if (gamepad->feedback_queue) {
        gamepad->feedback_queue->raise(message);
      }
    }

    void handle_output(const std::shared_ptr<gamepad_context_t> &gamepad, const lvh::GamepadOutput &output) {
      switch (output.kind) {
        case lvh::GamepadOutputKind::rumble:
          if (gamepad->has_last_rumble &&
              gamepad->last_low_frequency_rumble == output.low_frequency_rumble &&
              gamepad->last_high_frequency_rumble == output.high_frequency_rumble) {
            return;
          }
          gamepad->has_last_rumble = true;
          gamepad->last_low_frequency_rumble = output.low_frequency_rumble;
          gamepad->last_high_frequency_rumble = output.high_frequency_rumble;
          raise_feedback(gamepad, gamepad_feedback_msg_t::make_rumble(gamepad->client_relative_index, output.low_frequency_rumble, output.high_frequency_rumble));
          break;
        case lvh::GamepadOutputKind::trigger_rumble:
          if (gamepad->has_last_trigger_rumble &&
              gamepad->last_left_trigger_rumble == output.left_trigger_rumble &&
              gamepad->last_right_trigger_rumble == output.right_trigger_rumble) {
            return;
          }
          gamepad->has_last_trigger_rumble = true;
          gamepad->last_left_trigger_rumble = output.left_trigger_rumble;
          gamepad->last_right_trigger_rumble = output.right_trigger_rumble;
          raise_feedback(gamepad, gamepad_feedback_msg_t::make_rumble_triggers(gamepad->client_relative_index, output.left_trigger_rumble, output.right_trigger_rumble));
          break;
        case lvh::GamepadOutputKind::rgb_led:
          if (gamepad->has_last_rgb &&
              gamepad->last_red == output.red &&
              gamepad->last_green == output.green &&
              gamepad->last_blue == output.blue) {
            return;
          }
          gamepad->has_last_rgb = true;
          gamepad->last_red = output.red;
          gamepad->last_green = output.green;
          gamepad->last_blue = output.blue;
          raise_feedback(gamepad, gamepad_feedback_msg_t::make_rgb_led(gamepad->client_relative_index, output.red, output.green, output.blue));
          break;
        case lvh::GamepadOutputKind::adaptive_triggers:
          raise_feedback(gamepad, gamepad_feedback_msg_t::make_adaptive_triggers(gamepad->client_relative_index, output.adaptive_trigger_flags, output.left_trigger_effect_type, output.right_trigger_effect_type, output.left_trigger_effect, output.right_trigger_effect));
          break;
        case lvh::GamepadOutputKind::raw_report:
          break;
      }
    }

    void release_all_touches(client_context_t &context) {
      if (!context.touch) {
        return;
      }

      for (const auto id : context.active_touches) {
        log_failure("release libvirtualhid touch contact"sv, context.touch->release_contact(id));
      }
      context.active_touches.clear();
    }

  }  // namespace

  input_context_t::input_context_t():
      runtime {create_runtime()},
      gamepads(MAX_GAMEPADS) {
    if (!runtime) {
      BOOST_LOG(warning) << "Unable to create libvirtualhid runtime"sv;
      return;
    }

    const auto &capabilities = runtime->capabilities();
    if (capabilities.supports_keyboard) {
      lvh::CreateKeyboardOptions options;
      options.profile = lvh::profiles::keyboard();
      options.stable_id = "sunshine-keyboard";
      auto created = runtime->create_keyboard(options);
      if (created) {
        keyboard = std::move(created.keyboard);
      } else {
        log_failure("create libvirtualhid keyboard"sv, created.status);
      }
    }
    if (capabilities.supports_mouse) {
      lvh::CreateMouseOptions options;
      options.profile = lvh::profiles::mouse();
      options.stable_id = "sunshine-mouse";
      auto created = runtime->create_mouse(options);
      if (created) {
        mouse = std::move(created.mouse);
      } else {
        log_failure("create libvirtualhid mouse"sv, created.status);
      }
    }
  }

  client_context_t::client_context_t(input_context_t &input):
      global {&input} {
    if (!global->runtime) {
      return;
    }

    const auto &capabilities = global->runtime->capabilities();
    if (capabilities.supports_touchscreen) {
      lvh::CreateTouchscreenOptions options;
      options.profile = lvh::profiles::touchscreen();
      options.stable_id = "sunshine-touchscreen";
      auto created = global->runtime->create_touchscreen(options);
      if (created) {
        touch = std::move(created.touchscreen);
      } else {
        log_failure("create libvirtualhid touchscreen"sv, created.status);
      }
    }
    if (capabilities.supports_pen_tablet) {
      lvh::CreatePenTabletOptions options;
      options.profile = lvh::profiles::pen_tablet();
      options.stable_id = "sunshine-pen-tablet";
      auto created = global->runtime->create_pen_tablet(options);
      if (created) {
        pen = std::move(created.pen_tablet);
      } else {
        log_failure("create libvirtualhid pen tablet"sv, created.status);
      }
    }
  }

  std::unique_ptr<lvh::Runtime> create_runtime() {
    lvh::RuntimeOptions options;
    options.backend = lvh::BackendKind::platform_default;
    return lvh::Runtime::create(options);
  }

  std::vector<supported_gamepad_t> static_supported_gamepads() {
    std::vector<supported_gamepad_t> gamepads {
      supported_gamepad_t {"auto", true, ""},
    };
    for (const auto &profile : gamepad_profiles) {
      gamepads.push_back({std::string {profile.name}, false, ""});
    }

    return gamepads;
  }

  std::vector<supported_gamepad_t> supported_gamepads(lvh::Runtime *runtime, bool fallback_vigem_available) {
    if (!runtime) {
      return static_supported_gamepads();
    }

    const auto libvirtualhid_available = runtime->capabilities().supports_gamepad;
    const auto reason = libvirtualhid_available ? "" : "gamepads.virtualhid-not-available";
    const auto auto_enabled = libvirtualhid_available || fallback_vigem_available;
    std::vector<supported_gamepad_t> gamepads {
      supported_gamepad_t {"auto", auto_enabled, auto_enabled ? "" : reason},
    };

    for (const auto &profile : gamepad_profiles) {
      const auto fallback_supported = fallback_vigem_available && (profile.name == "x360"sv || profile.name == "ds4"sv);
      const auto enabled = libvirtualhid_available || fallback_supported;
      gamepads.push_back({std::string {profile.name}, enabled, enabled ? "" : reason});
    }

    for (auto &[name, is_enabled, reason_disabled] : gamepads) {
      if (!is_enabled) {
        BOOST_LOG(warning) << "Gamepad "sv << name << " is disabled due to "sv << reason_disabled;
      }
    }

    return gamepads;
  }

  int alloc_gamepad(input_context_t &context, const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue) {
    if (!context.runtime || !context.runtime->capabilities().supports_gamepad) {
      return -1;
    }
    if (id.globalIndex < 0 || id.globalIndex >= static_cast<int>(context.gamepads.size())) {
      BOOST_LOG(warning) << "Invalid libvirtualhid gamepad index: "sv << id.globalIndex;
      return -1;
    }

    const auto &selection = profile_for_metadata(metadata);
    auto profile = selection.profile();
    if (config::input.gamepad != "auto"sv) {
      BOOST_LOG(info) << "Gamepad "sv << id.globalIndex << " will be "sv << profile.name << " (manual selection)"sv;
    } else {
      BOOST_LOG(info) << "Gamepad "sv << id.globalIndex << " will be "sv << profile.name;
    }

    lvh::CreateGamepadOptions options;
    options.profile = profile;
    options.metadata = gamepad_metadata(id, metadata, profile);
    auto created = lvh::GamepadStateAdapter::create(*context.runtime, options);
    if (!created) {
      log_failure("create libvirtualhid gamepad"sv, created.status);
      return -1;
    }

    auto gamepad = std::make_shared<gamepad_context_t>();
    gamepad->adapter = std::move(created.adapter);
    gamepad->feedback_queue = std::move(feedback_queue);
    gamepad->client_relative_index = id.clientRelativeIndex;
    gamepad->adapter->set_output_callback([gamepad](const lvh::GamepadOutput &output) {
      handle_output(gamepad, output);
    });

    const auto &support = gamepad->adapter->support();
    warn_unsupported_client_features(id.globalIndex, metadata, support);
    warn_missing_client_features(id.globalIndex, metadata, support);
    if (support.supports_motion) {
      raise_feedback(gamepad, gamepad_feedback_msg_t::make_motion_event_state(id.clientRelativeIndex, LI_MOTION_TYPE_ACCEL, 100));
      raise_feedback(gamepad, gamepad_feedback_msg_t::make_motion_event_state(id.clientRelativeIndex, LI_MOTION_TYPE_GYRO, 100));
    }

    context.gamepads[id.globalIndex] = std::move(gamepad);
    return 0;
  }

  bool has_gamepad(const input_context_t &context, int nr) {
    return nr >= 0 && nr < context.gamepads.size() && context.gamepads[nr] && context.gamepads[nr]->adapter;
  }

  void free_gamepad(input_context_t &context, int nr) {
    if (has_gamepad(context, nr)) {
      context.gamepads[nr].reset();
    }
  }

  void gamepad_update(input_context_t &context, int nr, const gamepad_state_t &state) {
    if (!has_gamepad(context, nr)) {
      return;
    }

    auto &gamepad = context.gamepads[nr];
    log_failure("submit libvirtualhid gamepad state"sv, gamepad->adapter->set_state(make_gamepad_state(state, gamepad->adapter->support())));
  }

  void gamepad_touch(input_context_t &context, const gamepad_touch_t &touch) {
    if (!has_gamepad(context, touch.id.globalIndex)) {
      return;
    }

    auto &gamepad = context.gamepads[touch.id.globalIndex];
    if (!gamepad->adapter->support().supports_touchpad) {
      return;
    }

    if (touch.eventType == LI_TOUCH_EVENT_CANCEL_ALL) {
      for (std::size_t index = 0; index < gamepad->touch_ids.size(); ++index) {
        if (gamepad->touch_ids[index]) {
          log_failure("release libvirtualhid gamepad touch"sv, gamepad->adapter->clear_touchpad_contact(index));
          gamepad->touch_ids[index].reset();
        }
      }
      return;
    }

    auto slot = std::ranges::find(gamepad->touch_ids, touch.pointerId);
    if (touch.eventType == LI_TOUCH_EVENT_DOWN && slot == gamepad->touch_ids.end()) {
      slot = std::ranges::find_if(gamepad->touch_ids, [](const auto &id) {
        return !id.has_value();
      });
      if (slot == gamepad->touch_ids.end()) {
        BOOST_LOG(warning) << "No free libvirtualhid gamepad touch slots"sv;
        return;
      }
      *slot = touch.pointerId;
    }

    if (slot == gamepad->touch_ids.end()) {
      return;
    }

    const auto index = static_cast<std::size_t>(std::distance(gamepad->touch_ids.begin(), slot));
    if (touch.eventType == LI_TOUCH_EVENT_UP || touch.eventType == LI_TOUCH_EVENT_CANCEL) {
      log_failure("release libvirtualhid gamepad touch"sv, gamepad->adapter->clear_touchpad_contact(index));
      slot->reset();
      return;
    }
    if (touch.eventType != LI_TOUCH_EVENT_DOWN && touch.eventType != LI_TOUCH_EVENT_MOVE) {
      return;
    }

    lvh::GamepadTouchContact contact;
    contact.id = static_cast<std::uint8_t>(index);
    contact.active = touch.pressure > 0.5F;
    contact.x = std::clamp(touch.x, 0.0F, 1.0F);
    contact.y = std::clamp(touch.y, 0.0F, 1.0F);
    log_failure("submit libvirtualhid gamepad touch"sv, gamepad->adapter->set_touchpad_contact(index, contact));
  }

  void gamepad_motion(input_context_t &context, const gamepad_motion_t &motion) {
    if (!has_gamepad(context, motion.id.globalIndex)) {
      return;
    }

    auto &gamepad = context.gamepads[motion.id.globalIndex];
    switch (motion.motionType) {
      case LI_MOTION_TYPE_ACCEL:
        log_failure("submit libvirtualhid gamepad acceleration"sv, gamepad->adapter->set_acceleration(lvh::Vector3 {motion.x, motion.y, motion.z}));
        break;
      case LI_MOTION_TYPE_GYRO:
        log_failure("submit libvirtualhid gamepad gyroscope"sv, gamepad->adapter->set_gyroscope(lvh::Vector3 {motion.x, motion.y, motion.z}));
        break;
      default:
        break;
    }
  }

  void gamepad_battery(input_context_t &context, const gamepad_battery_t &battery) {
    if (!has_gamepad(context, battery.id.globalIndex)) {
      return;
    }

    auto &gamepad = context.gamepads[battery.id.globalIndex];
    if (battery.state == LI_BATTERY_STATE_UNKNOWN || battery.state == LI_BATTERY_STATE_NOT_PRESENT) {
      log_failure("clear libvirtualhid gamepad battery"sv, gamepad->adapter->clear_battery());
      return;
    }

    lvh::GamepadBattery value;
    value.state = battery_state(battery.state);
    value.percentage = battery.percentage == LI_BATTERY_PERCENTAGE_UNKNOWN ? 100 : std::min<std::uint8_t>(battery.percentage, 100);
    log_failure("submit libvirtualhid gamepad battery"sv, gamepad->adapter->set_battery(value));
  }

  void move_mouse(input_context_t &context, int delta_x, int delta_y) {
    if (context.mouse) {
      log_failure("submit libvirtualhid mouse movement"sv, context.mouse->move_relative(delta_x, delta_y));
    }
  }

  void abs_mouse(input_context_t &context, const touch_port_t &touch_port, float x, float y) {
    if (context.mouse) {
      log_failure(
        "submit libvirtualhid absolute mouse movement"sv,
        context.mouse->move_absolute(
          static_cast<std::int32_t>(std::lround(x)),
          static_cast<std::int32_t>(std::lround(y)),
          touch_port.width,
          touch_port.height
        )
      );
    }
  }

  void button_mouse(input_context_t &context, int button, bool release) {
    if (context.mouse) {
      const auto converted = mouse_button(button);
      if (!converted) {
        return;
      }

      log_failure("submit libvirtualhid mouse button"sv, context.mouse->button(*converted, !release));
    }
  }

  void scroll(input_context_t &context, int high_res_distance) {
    if (context.mouse) {
      log_failure("submit libvirtualhid vertical scroll"sv, context.mouse->vertical_scroll(high_res_distance));
    }
  }

  void hscroll(input_context_t &context, int high_res_distance) {
    if (context.mouse) {
      log_failure("submit libvirtualhid horizontal scroll"sv, context.mouse->horizontal_scroll(high_res_distance));
    }
  }

  void keyboard_update(input_context_t &context, std::uint16_t modcode, bool release) {
    if (context.keyboard) {
      log_failure("submit libvirtualhid keyboard input"sv, context.keyboard->submit({.key_code = modcode, .pressed = !release}));
    }
  }

  void unicode(input_context_t &context, const char *utf8, int size) {
    if (context.keyboard && utf8 && size > 0) {
      log_failure("submit libvirtualhid text input"sv, context.keyboard->type_text({.text = std::string {utf8, static_cast<std::size_t>(size)}}));
    }
  }

  void touch_update(client_context_t &context, const touch_input_t &touch) {
    if (!context.touch) {
      return;
    }

    switch (touch.eventType) {
      case LI_TOUCH_EVENT_CANCEL_ALL:
        release_all_touches(context);
        return;
      case LI_TOUCH_EVENT_UP:
      case LI_TOUCH_EVENT_CANCEL:
      case LI_TOUCH_EVENT_HOVER_LEAVE:
        log_failure("release libvirtualhid touch contact"sv, context.touch->release_contact(static_cast<std::int32_t>(touch.pointerId)));
        context.active_touches.erase(static_cast<std::int32_t>(touch.pointerId));
        return;
      case LI_TOUCH_EVENT_HOVER:
      case LI_TOUCH_EVENT_DOWN:
      case LI_TOUCH_EVENT_MOVE:
        {
          lvh::TouchContact contact;
          contact.id = static_cast<std::int32_t>(touch.pointerId);
          contact.x = std::clamp(touch.x, 0.0F, 1.0F);
          contact.y = std::clamp(touch.y, 0.0F, 1.0F);
          contact.pressure = std::clamp(touch.pressureOrDistance, 0.0F, 1.0F);
          contact.orientation = touch_orientation(touch.rotation);
          log_failure("submit libvirtualhid touch contact"sv, context.touch->place_contact(contact));
          context.active_touches.insert(contact.id);
          return;
        }
      default:
        return;
    }
  }

  void pen_update(client_context_t &context, const pen_input_t &pen) {
    if (!context.pen) {
      return;
    }

    const std::array button_states {
      std::pair {lvh::PenButton::primary, (pen.penButtons & LI_PEN_BUTTON_PRIMARY) != 0},
      std::pair {lvh::PenButton::secondary, (pen.penButtons & LI_PEN_BUTTON_SECONDARY) != 0},
      std::pair {lvh::PenButton::tertiary, (pen.penButtons & LI_PEN_BUTTON_TERTIARY) != 0},
    };
    for (const auto &[button, pressed] : button_states) {
      const auto was_pressed = context.pressed_pen_buttons.contains(button);
      if (pressed == was_pressed) {
        continue;
      }

      log_failure("submit libvirtualhid pen button"sv, context.pen->button(button, pressed));
      if (pressed) {
        context.pressed_pen_buttons.insert(button);
      } else {
        context.pressed_pen_buttons.erase(button);
      }
    }

    if (pen.eventType == LI_TOUCH_EVENT_CANCEL_ALL) {
      for (const auto button : context.pressed_pen_buttons) {
        log_failure("release libvirtualhid pen button"sv, context.pen->button(button, false));
      }
      context.pressed_pen_buttons.clear();
      return;
    }

    auto rotation = pen.rotation;
    if (rotation != LI_ROT_UNKNOWN) {
      rotation %= 360;
    }

    float tilt_x = 0.0F;
    float tilt_y = 0.0F;
    if (pen.tilt != LI_TILT_UNKNOWN && rotation != LI_ROT_UNKNOWN) {
      const auto rotation_rads = static_cast<float>(rotation) * std::numbers::pi_v<float> / 180.0F;
      const auto tilt_rads = static_cast<float>(pen.tilt) * std::numbers::pi_v<float> / 180.0F;
      const auto r = std::sin(tilt_rads);
      const auto z = std::cos(tilt_rads);

      tilt_x = std::atan2(std::sin(-rotation_rads) * r, z) * 180.0F / std::numbers::pi_v<float>;
      tilt_y = std::atan2(std::cos(-rotation_rads) * r, z) * 180.0F / std::numbers::pi_v<float>;
    }

    const auto is_touching = pen.eventType == LI_TOUCH_EVENT_DOWN || pen.eventType == LI_TOUCH_EVENT_MOVE;
    lvh::PenToolState state;
    state.tool = pen_tool(pen.toolType);
    state.x = std::clamp(pen.x, 0.0F, 1.0F);
    state.y = std::clamp(pen.y, 0.0F, 1.0F);
    state.pressure = is_touching ? std::clamp(pen.pressureOrDistance, 0.0F, 1.0F) : -1.0F;
    state.distance = is_touching ? -1.0F : std::clamp(pen.pressureOrDistance, 0.0F, 1.0F);
    state.tilt_x = tilt_x;
    state.tilt_y = tilt_y;
    log_failure("submit libvirtualhid pen state"sv, context.pen->place_tool(state));
  }

  bool configured_gamepad_supports_touchpad() {
    if (config::input.gamepad == "auto"sv) {
      return true;
    }

    const auto profile = profile_for_name(config::input.gamepad).profile();
    return lvh::gamepad_profile_support(profile).supports_touchpad;
  }

}  // namespace platf::virtualhid
