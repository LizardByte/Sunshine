/**
 * @file src/platform/windows/input_utils.h
 * @brief Helpers for selecting Windows touch input targets.
 */
#pragma once

// standard includes
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

// local includes
#include "src/platform/common.h"

namespace platf::win_input {
  /**
   * @brief Clockwise rotation applied to normalized native-touch coordinates.
   */
  enum class touch_rotation_e : std::uint16_t {
    none = 0,  ///< Preserve client touch coordinates.
    clockwise_90 = 90,  ///< Rotate coordinates 90 degrees clockwise.
    clockwise_180 = 180,  ///< Rotate coordinates 180 degrees clockwise.
    clockwise_270 = 270  ///< Rotate coordinates 270 degrees clockwise.
  };

  /**
   * @brief Physical bounds of an attached Windows display.
   */
  struct display_bounds_t {
    int offset_x;  ///< Horizontal display offset in physical virtual-desktop coordinates.
    int offset_y;  ///< Vertical display offset in physical virtual-desktop coordinates.
    int width;  ///< Display width in physical pixels.
    int height;  ///< Display height in physical pixels.
    bool is_primary;  ///< Whether Windows marks this display as the primary display.
  };

  /**
   * @brief Build a touch port targeting the Windows primary display.
   * @details The returned offset is relative to the virtual desktop's top-left corner, matching Sunshine's
   * nonnegative touch-port coordinate system even when Windows reports displays at negative coordinates.
   *
   * @param displays Physical bounds of the currently attached displays.
   * @return Primary-display touch port, or `std::nullopt` when the display topology is invalid.
   */
  std::optional<touch_port_t> make_primary_display_touch_port(std::span<const display_bounds_t> displays);

  /**
   * @brief Select the touch port used for native Windows touch injection.
   *
   * @param streamed_touch_port Touch port associated with the streamed display.
   * @param send_to_primary_display Whether native touch should target the primary display.
   * @param primary_touch_port Available primary-display touch port, if valid.
   * @return The selected touch port.
   */
  touch_port_t select_touch_port(
    const touch_port_t &streamed_touch_port,
    bool send_to_primary_display,
    const std::optional<touch_port_t> &primary_touch_port
  );

  /**
   * @brief Parse a configured clockwise native-touch rotation.
   *
   * @param rotation Rotation expressed as `0`, `90`, `180`, or `270` degrees.
   * @return Parsed rotation, or `touch_rotation_e::none` for an unsupported value.
   */
  touch_rotation_e parse_touch_rotation(std::string_view rotation);

  /**
   * @brief Select the native-touch rotation for the active target.
   *
   * @param primary_display_selected Whether a valid primary-display touch port was selected.
   * @param configured_rotation Configured clockwise rotation in degrees.
   * @return Configured rotation for a primary-display target, otherwise `touch_rotation_e::none`.
   */
  touch_rotation_e select_touch_rotation(bool primary_display_selected, std::string_view configured_rotation);

  /**
   * @brief Rotate normalized native-touch coordinates around the center of their target display.
   *
   * @param normalized_x Horizontal coordinate in normalized video coordinates.
   * @param normalized_y Vertical coordinate in normalized video coordinates.
   * @param rotation Clockwise rotation to apply before mapping to display pixels.
   * @return Rotated normalized coordinates.
   */
  std::pair<float, float> rotate_normalized_touch_position(
    float normalized_x,
    float normalized_y,
    touch_rotation_e rotation
  );

  /**
   * @brief Map normalized native-touch coordinates to a pixel inside a Windows display.
   * @details Coordinates outside the normalized content, including client-side black bars, are clamped to the
   * nearest pixel of the selected display.
   *
   * @param touch_port Physical bounds of the selected touch target.
   * @param normalized_x Horizontal coordinate in normalized video coordinates.
   * @param normalized_y Vertical coordinate in normalized video coordinates.
   * @return Pixel coordinates clamped to the selected display's inclusive bounds.
   */
  std::pair<int, int> map_normalized_touch_position(
    const touch_port_t &touch_port,
    float normalized_x,
    float normalized_y
  );

  /**
   * @brief Add Windows compatibility flags for a native touch event.
   * @details Active contacts receive the mouse-compatible first-button flag. The first contact in an interaction
   * also receives the Windows primary-pointer flag.
   *
   * @param pointer_flags Existing Windows pointer flags after applying the event state transition.
   * @param event_type Moonlight touch event type.
   * @param designate_primary Whether this contact starts a new primary touch interaction.
   * @return Pointer flags to inject for the event.
   */
  std::uint32_t apply_touch_pointer_event_flags(
    std::uint32_t pointer_flags,
    std::uint8_t event_type,
    bool designate_primary
  );

  /**
   * @brief Remove transient Windows touch flags after a frame is injected.
   *
   * @param pointer_flags Pointer flags used for the injected frame.
   * @return Persistent flags to retain for subsequent touch frames.
   */
  std::uint32_t finish_touch_pointer_frame(std::uint32_t pointer_flags);

  /**
   * @brief Determine whether a touch pointer prevents a new primary contact from being designated.
   *
   * @param pointer_flags Current Windows pointer flags for a touch pointer.
   * @return `true` when the pointer belongs to the current contact interaction.
   */
  bool touch_pointer_blocks_primary_designation(std::uint32_t pointer_flags);
}  // namespace platf::win_input
