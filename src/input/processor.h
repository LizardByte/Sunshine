/**
 * @file src/input/processor.h
 * @brief Declarations for gamepad, keyboard, and mouse input handling.
 */
#pragma once

#include <functional>

#include "common.h"
#include "init.h"
#include "src/platform/common.h"
#include "src/thread_safe.h"

namespace input {

  void
  print(void *input);
  void
  reset(std::shared_ptr<input_t> &input);

  /**
   * @brief Called on the control stream thread to queue an input message.
   * @param input The input context pointer.
   * @param input_data The input message.
   */
  void
  passthrough(std::shared_ptr<input_t> &input, std::vector<std::uint8_t> &&input_data);

  [[nodiscard]] std::unique_ptr<platf::deinit_t>
  init();

  std::shared_ptr<input_t>
  alloc(safe::mail_t mail);

  bool
  probe_gamepads();

  struct input_t {
    enum shortkey_e {
      CTRL = 0x1,
      ALT = 0x2,
      SHIFT = 0x4,

      SHORTCUT = CTRL | ALT | SHIFT
    };

    input_t(
      safe::mail_raw_t::event_t<input::touch_port_t> touch_port_event,
      platf::feedback_queue_t feedback_queue):
        shortcutFlags {},
        gamepads_orchestrator(std::make_shared<gamepad_orchestrator>().get()),
        client_context { platf::allocate_client_input_context(PlatformInput::getInstance()) },
        touch_port_event { std::move(touch_port_event) },
        feedback_queue { std::move(feedback_queue) },
        mouse_left_button_timeout {},
        touch_port { { 0, 0, 0, 0 }, 0, 0, 1.0f },
        accumulated_vscroll_delta {},
        accumulated_hscroll_delta {} {}

    // Keep track of alt+ctrl+shift key combo
    int shortcutFlags;

    gamepad_orchestrator *gamepads_orchestrator;
    std::vector<gamepad::gamepad_t> *gamepads = &gamepads_orchestrator->gamepads;

    std::unique_ptr<platf::client_input_t> client_context;

    safe::mail_raw_t::event_t<input::touch_port_t> touch_port_event;
    platf::feedback_queue_t feedback_queue;

    std::list<std::vector<uint8_t>> input_queue;
    std::mutex input_queue_lock;

    thread_pool_util::ThreadPool::task_id_t mouse_left_button_timeout;

    input::touch_port_t touch_port;

    int32_t accumulated_vscroll_delta;
    int32_t accumulated_hscroll_delta;
  };

}  // namespace input
