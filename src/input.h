/**
 * @file src/input.h
 * @brief Declarations for gamepad, keyboard, and mouse input handling.
 */
#pragma once

// standard includes
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

// local includes
#include "platform/common.h"
#include "thread_safe.h"

namespace thread_pool_util {
  class ThreadPool;
}

namespace input {
  struct input_t;

  /**
   * @brief Owns host input state shared by all clients of one application.
   */
  class service_t {
  public:
    struct impl_t;  ///< Private implementation retained by client input sessions.

    /**
     * @brief Bind the host scheduler, mailbox, and cursor preference.
     * @param settings Immutable host startup configuration.
     * @param scheduler Scheduler serializing input work.
     * @param mail Host mailbox carrying display selection.
     * @param cursor Host cursor visibility preference.
     */
    service_t(const config::settings_t &settings, thread_pool_util::ThreadPool &scheduler, safe::mail_t mail, bool &cursor);
    service_t(const service_t &) = delete;
    service_t &operator=(const service_t &) = delete;
    /**
     * @brief Release retained virtual devices and the platform input backend.
     */
    ~service_t();

    /**
     * @brief Initialize the platform input backend at the application startup stage.
     * @return Whether a platform input context was created.
     */
    bool initialize();

    /**
     * @brief Destroy all retained virtual gamepad sessions.
     */
    void terminate_gamepads();
    /**
     * @brief Destroy the retained virtual gamepads for one paired client.
     * @param session_id Stable paired-client identity.
     */
    void terminate_gamepads(std::string_view session_id);

    /**
     * @brief Check whether any configured gamepad backend is available.
     * @return True when no enabled backend was found.
     */
    bool probe_gamepads();

    /**
     * @brief Serialize keyboard and mouse backend refresh with streamed input.
     */
    void refresh_virtual_input();

    /**
     * @brief Persist a newly licensed user's backend choice on the input scheduler.
     * @param config Store providing the immutable configuration file path.
     * @param licensed Whether Virtual HID Driver has an active license.
     * @return Whether the preference was persisted and selected for this input service.
     */
    bool select_all_gamepad_drivers_if_licensed(config::store_t &config, bool licensed);

    /**
     * @brief Copy startup input settings with the service's current backend policy.
     * @return Input configuration safe for UI and tray inspection.
     */
    config::input_t configuration_snapshot() const;

    /**
     * @brief Allocate or resume input state for a paired client.
     * @param mail Mailbox for the active transport session.
     * @param session_id Stable paired-client identity.
     * @return Retained client input state bound to the transport mailbox.
     */
    std::shared_ptr<input_t> alloc(safe::mail_t mail, std::string session_id);

  private:
    friend struct service_access_t;
    std::shared_ptr<impl_t> impl_;  ///< Host input state retained by queued tasks and client sessions.
  };

  /**
   * @brief Write a debug log representation of the input packet.
   *
   * @param input Raw input packet to format for logging.
   */
  void print(void *input);
  /**
   * @brief Reset stream input state after a client disconnect or shutdown.
   *
   * @param input Shared stream input state to reset.
   */
  void reset(std::shared_ptr<input_t> &input);

  /**
   * @brief Queue a raw input message for platform passthrough.
   */
  void passthrough(std::shared_ptr<input_t> &input, std::vector<std::uint8_t> &&input_data);

#ifdef SUNSHINE_TESTS
  namespace testing {
    /**
     * @brief Replace the fixture's platform input backend for a unit test.
     *
     * @param input Test-owned platform input backend.
     * @param service Fixture-owned host input service.
     */
    void set_platform_input(service_t &service, platf::input_t input);

    /**
     * @brief Allocate a gamepad directly in retained input state for a unit test.
     *
     * @param input Retained input state.
     * @param client_index Client-relative controller index.
     * @param metadata Client-reported controller metadata.
     * @return Assigned global gamepad slot, or -1 on failure.
     */
    int alloc_gamepad(std::shared_ptr<input_t> &input, std::uint8_t client_index, const platf::gamepad_arrival_t &metadata);

    /**
     * @brief Return the global gamepad slot stored for a test controller.
     *
     * @param input Retained input state.
     * @param client_index Client-relative controller index.
     * @return Assigned global gamepad slot, or -1 when unallocated.
     */
    int gamepad_id(const std::shared_ptr<input_t> &input, std::uint8_t client_index);

    /**
     * @brief Install a callback called just before dispatching a queued input packet in a test.
     * @param hook Callback receiving the packet magic, or empty to remove it.
     * @param service Fixture-owned host input service.
     */
    void set_input_packet_hook(service_t &service, std::function<void(std::uint32_t)> hook);

    /**
     * @brief Redirect input task scheduling to a test callback.
     *
     * @param sink Callback receiving the stream whose packet task was queued, or empty to restore the worker pool.
     * @param service Fixture-owned host input service.
     */
    void set_input_task_sink(service_t &service, std::function<void(std::shared_ptr<input_t>)> sink);

    /**
     * @brief Process queued input packets on the calling thread in a test.
     *
     * @param input Retained stream input state.
     */
    void process_queued_messages(std::shared_ptr<input_t> input);

    /**
     * @brief Keyboard event Sunshine emitted toward the platform backend.
     */
    struct keyboard_event_t {
      std::uint16_t key_code;  ///< Platform keycode after the configured keybinding remap.
      bool release;  ///< Whether the event releases the key.
      std::uint8_t flags;  ///< Bit flags carried by the client keyboard packet.
      bool extended = false;  ///< Whether the client positively identified an extended key.
    };

    /**
     * @brief Redirect keyboard output away from the host operating system.
     *
     * Tests must install a sink before emitting keys, otherwise the events are typed into the
     * machine running the test suite.
     *
     * @param sink Recorder invoked in place of platf::keyboard_update, or empty to restore
     *             delivery to the platform backend.
     */
    void set_keyboard_sink(service_t &service, std::function<void(const keyboard_event_t &)> sink);

    /**
     * @brief Process one client keyboard packet on the calling thread.
     *
     * @param input Retained input state.
     * @param key_code Windows virtual-key code sent by the client.
     * @param modifiers Client modifier bitmask carried by the packet.
     * @param flags Bit flags carried by the client keyboard packet.
     * @param release Whether the packet releases the key.
     */
    void send_keyboard_packet(std::shared_ptr<input_t> &input, std::uint16_t key_code, std::uint8_t modifiers, std::uint8_t flags, bool release);

    /**
     * @brief Process one client relative-mouse packet on the calling thread.
     *
     * @param input Retained input state.
     * @param delta_x Horizontal client mouse delta.
     * @param delta_y Vertical client mouse delta.
     */
    void send_relative_mouse_packet(std::shared_ptr<input_t> &input, std::int16_t delta_x, std::int16_t delta_y);

    /**
     * @brief Forget every key Sunshine tracks as pressed and cancel any pending key repeat.
     */
    void reset_keyboard_state(service_t &service);

    /**
     * @brief Release every key Sunshine tracks as pressed, as a disconnect does.
     */
    void release_held_keys(service_t &service);

    /**
     * @brief Validate raw protocol input bytes for a unit test.
     *
     * @param packet Raw packet bytes.
     * @return True when the packet is safe for typed processing.
     */
    bool is_valid_input_packet(std::span<const std::uint8_t> packet);

    /**
     * @brief Return the number of validated packets waiting in a test input queue.
     *
     * @param input Shared stream input state.
     * @return Number of queued packets, or zero for an empty input pointer.
     */
    std::size_t queued_input_packet_count(const std::shared_ptr<input_t> &input);
  }  // namespace testing
#endif

  /**
   * @brief Touchscreen coordinate bounds used to scale absolute input.
   */
  struct touch_port_t: public platf::touch_port_t {
    int env_width;  ///< Width of the full capture environment in physical pixels.
    int env_height;  ///< Height of the full capture environment in physical pixels.

    // Offset x and y coordinates of the client
    float client_offsetX;  ///< Horizontal client viewport offset used when scaling touch input.
    float client_offsetY;  ///< Vertical client viewport offset used when scaling touch input.

    float scalar_inv;  ///< Inverse scale factor from client coordinates to display coordinates.
    float scalar_tpcoords;  ///< Scale factor from client coordinates to touch-port coordinates.

    int env_logical_width;  ///< Width of the full capture environment after display scaling.
    int env_logical_height;  ///< Height of the full capture environment after display scaling.

    /**
     * @brief Check whether the touch-port bounds are initialized.
     */
    explicit operator bool() const {
      return width != 0 && height != 0 && env_width != 0 && env_height != 0;
    }
  };

  /**
   * @brief Scale the ellipse axes according to the provided size.
   * @param val The major and minor axis pair.
   * @param rotation The rotation value from the touch/pen event.
   * @param scalar The scalar cartesian coordinate pair.
   * @return The major and minor axis pair.
   */
  std::pair<float, float> scale_client_contact_area(const std::pair<float, float> &val, uint16_t rotation, const std::pair<float, float> &scalar);
}  // namespace input
