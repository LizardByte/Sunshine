/**
 * @file tests/unit/platform/macos/test_input.cpp
 * @brief Unit tests for src/platform/macos/input.cpp
 */

// Only compile these tests on macOS
#ifdef __APPLE__

  #include "../../../tests_common.h"

  #include <src/platform/common.h>
  #include <src/platform/macos/input_gamepad.h>

/**
 * @brief Verify that each Moonlight button constant has the value defined by
 *        the Moonlight protocol (Limelight-internal.h). These constants are
 *        referenced directly by gamepad_update() and a wrong value would
 *        silently mis-map an entire button.
 */
TEST(MacosGamepadButtonConstants, ProtocolValues) {
  EXPECT_EQ(platf::DPAD_UP, 0x0001u);
  EXPECT_EQ(platf::DPAD_DOWN, 0x0002u);
  EXPECT_EQ(platf::DPAD_LEFT, 0x0004u);
  EXPECT_EQ(platf::DPAD_RIGHT, 0x0008u);
  EXPECT_EQ(platf::START, 0x0010u);
  EXPECT_EQ(platf::BACK, 0x0020u);
  EXPECT_EQ(platf::LEFT_STICK, 0x0040u);
  EXPECT_EQ(platf::RIGHT_STICK, 0x0080u);
  EXPECT_EQ(platf::LEFT_BUTTON, 0x0100u);
  EXPECT_EQ(platf::RIGHT_BUTTON, 0x0200u);
  EXPECT_EQ(platf::HOME, 0x0400u);
  EXPECT_EQ(platf::A, 0x1000u);
  EXPECT_EQ(platf::B, 0x2000u);
  EXPECT_EQ(platf::X, 0x4000u);
  EXPECT_EQ(platf::Y, 0x8000u);
}

/**
 * @brief Verify that all mapped button constants are distinct (no two buttons
 *        share a bit), which is a prerequisite for the bitwise mapping in
 *        gamepad_update() to be lossless.
 */
TEST(MacosGamepadButtonConstants, AllDistinct) {
  constexpr std::uint32_t buttons[] = {
    platf::DPAD_UP,
    platf::DPAD_DOWN,
    platf::DPAD_LEFT,
    platf::DPAD_RIGHT,
    platf::START,
    platf::BACK,
    platf::LEFT_STICK,
    platf::RIGHT_STICK,
    platf::LEFT_BUTTON,
    platf::RIGHT_BUTTON,
    platf::HOME,
    platf::A,
    platf::B,
    platf::X,
    platf::Y,
  };

  std::uint32_t seen = 0;
  for (auto btn : buttons) {
    EXPECT_EQ(btn & seen, 0u) << "Button 0x" << std::hex << btn << " overlaps with a previously seen button";
    seen |= btn;
  }
}

/**
 * @brief Verify trigger scaling on the real mapping: a 0–255 byte value must
 *        map onto the full signed 16-bit range -32768..32767 so SDL reads the
 *        analog trigger as a full-range axis (rest = -32768, full = 32767).
 *        This is the actual formula used by map_gamepad_state_to_hid_report();
 *        a regression here would surface as half-pressed or inverted triggers.
 */
TEST(MacosGamepadTriggerScaling, RestProducesMin) {
  platf::gamepad_state_t state {};
  state.lt = 0;
  state.rt = 0;
  const auto report = platf::map_gamepad_state_to_hid_report(state);
  EXPECT_EQ(report.l2, -32768);
  EXPECT_EQ(report.r2, -32768);
}

TEST(MacosGamepadTriggerScaling, FullProducesMax) {
  platf::gamepad_state_t state {};
  state.lt = 255;
  state.rt = 255;
  const auto report = platf::map_gamepad_state_to_hid_report(state);
  EXPECT_EQ(report.l2, 32767);
  EXPECT_EQ(report.r2, 32767);
}

TEST(MacosGamepadTriggerScaling, MidpointIsNearZero) {
  platf::gamepad_state_t state {};
  state.lt = 127;
  // 127/255 * 65535 - 32768 ≈ -131; allow generous tolerance for rounding
  const auto report = platf::map_gamepad_state_to_hid_report(state);
  EXPECT_NEAR(report.l2, -131, 64);
}

/**
 * @brief Verify each Moonlight button flag maps onto exactly the expected HID
 *        button bit (and only that bit), matching the Razer Serval SDL entry.
 */
TEST(MacosGamepadMapping, ButtonsMapToExpectedBits) {
  const struct {
    std::uint32_t flag;
    int bit;
    const char *name;
  } cases[] = {
    {platf::A, 0, "A"},
    {platf::B, 1, "B"},
    {platf::X, 2, "X"},
    {platf::Y, 3, "Y"},
    {platf::LEFT_BUTTON, 4, "LB"},
    {platf::RIGHT_BUTTON, 5, "RB"},
    {platf::BACK, 6, "Back"},
    {platf::START, 7, "Start"},
    {platf::HOME, 8, "Guide"},
    {platf::LEFT_STICK, 9, "LS"},
    {platf::RIGHT_STICK, 10, "RS"},
  };

  for (const auto &c : cases) {
    platf::gamepad_state_t state {};
    state.buttonFlags = c.flag;
    const auto report = platf::map_gamepad_state_to_hid_report(state);
    EXPECT_EQ(report.buttons, static_cast<std::uint16_t>(1u << c.bit))
      << "button " << c.name << " mapped to the wrong bit(s)";
  }
}

TEST(MacosGamepadMapping, NoButtonsProducesZeroAndNullHat) {
  const auto report = platf::map_gamepad_state_to_hid_report(platf::gamepad_state_t {});
  EXPECT_EQ(report.buttons, 0u);
  EXPECT_EQ(report.hat, 8);  // null (no D-pad direction)
  EXPECT_EQ(report.report_id, 1);
}

/**
 * @brief Verify the D-pad button flags encode into the correct HAT direction,
 *        including diagonals, the null state, and ambiguous opposite-direction
 *        (SOCD) combinations.
 */
TEST(MacosGamepadMapping, DpadEncodesHatDirections) {
  const struct {
    std::uint32_t flags;
    int hat;
    const char *name;
  } cases[] = {
    {platf::DPAD_UP, 0, "N"},
    {platf::DPAD_UP | platf::DPAD_RIGHT, 1, "NE"},
    {platf::DPAD_RIGHT, 2, "E"},
    {platf::DPAD_DOWN | platf::DPAD_RIGHT, 3, "SE"},
    {platf::DPAD_DOWN, 4, "S"},
    {platf::DPAD_DOWN | platf::DPAD_LEFT, 5, "SW"},
    {platf::DPAD_LEFT, 6, "W"},
    {platf::DPAD_UP | platf::DPAD_LEFT, 7, "NW"},
    {0, 8, "null"},
    // Opposite directions cancel to null rather than picking a side.
    {platf::DPAD_UP | platf::DPAD_DOWN, 8, "U+D"},
    {platf::DPAD_LEFT | platf::DPAD_RIGHT, 8, "L+R"},
  };

  for (const auto &c : cases) {
    platf::gamepad_state_t state {};
    state.buttonFlags = c.flags;
    const auto report = platf::map_gamepad_state_to_hid_report(state);
    EXPECT_EQ(report.hat, c.hat) << "D-pad combo " << c.name << " produced the wrong HAT value";
  }
}

/**
 * @brief Verify stick axes: X passes through, Y is negated to the HID
 *        convention (up = negative), and the INT16_MIN extreme negates to
 *        INT16_MAX instead of wrapping back to INT16_MIN.
 */
TEST(MacosGamepadMapping, SticksPassThroughAndYNegated) {
  platf::gamepad_state_t state {};
  state.lsX = 1000;
  state.lsY = 2000;
  state.rsX = -1500;
  state.rsY = 3000;
  const auto report = platf::map_gamepad_state_to_hid_report(state);
  EXPECT_EQ(report.left_x, 1000);
  EXPECT_EQ(report.left_y, -2000);
  EXPECT_EQ(report.right_x, -1500);
  EXPECT_EQ(report.right_y, -3000);
}

TEST(MacosGamepadMapping, YAxisExtremeDoesNotOverflow) {
  platf::gamepad_state_t state {};
  state.lsY = INT16_MIN;  // -32768; plain `-v` would wrap back to -32768
  state.rsY = INT16_MAX;  // 32767
  const auto report = platf::map_gamepad_state_to_hid_report(state);
  EXPECT_EQ(report.left_y, INT16_MAX);  // clamped, not wrapped
  EXPECT_EQ(report.right_y, -32767);
}

/**
 * @brief Test that alloc_gamepad rejects out-of-range slot indices without
 *        crashing, before any IOHIDUserDeviceCreate call is made.
 */
class MacosAllocGamepadTest: public testing::Test {};

TEST_F(MacosAllocGamepadTest, NegativeSlotReturnsError) {
  auto input = platf::input();
  platf::gamepad_id_t id {-1, 0};
  EXPECT_NE(platf::alloc_gamepad(input, id, {}, {}), 0);
}

TEST_F(MacosAllocGamepadTest, OversizedSlotReturnsError) {
  auto input = platf::input();
  // platf::MAX_GAMEPADS is the ceiling the macOS implementation also enforces
  platf::gamepad_id_t id {platf::MAX_GAMEPADS, 0};
  EXPECT_NE(platf::alloc_gamepad(input, id, {}, {}), 0);
}

/**
 * @brief Test that free_gamepad on an unallocated slot is a no-op (no crash,
 *        no assertion failure).
 */
TEST_F(MacosAllocGamepadTest, FreeUnallocatedSlotIsNoOp) {
  auto input = platf::input();
  EXPECT_NO_FATAL_FAILURE(platf::free_gamepad(input, 0));
}

TEST_F(MacosAllocGamepadTest, FreeNegativeSlotIsNoOp) {
  auto input = platf::input();
  EXPECT_NO_FATAL_FAILURE(platf::free_gamepad(input, -1));
}

/**
 * @brief Test that gamepad_update on an unallocated slot is a no-op (no crash,
 *        no assertion failure). This mirrors the behaviour in src/input.cpp
 *        which may call gamepad_update after alloc_gamepad fails.
 */
TEST_F(MacosAllocGamepadTest, UpdateUnallocatedSlotIsNoOp) {
  auto input = platf::input();
  platf::gamepad_state_t state {};
  EXPECT_NO_FATAL_FAILURE(platf::gamepad_update(input, 0, state));
}

TEST_F(MacosAllocGamepadTest, UpdateNegativeSlotIsNoOp) {
  auto input = platf::input();
  platf::gamepad_state_t state {};
  EXPECT_NO_FATAL_FAILURE(platf::gamepad_update(input, -1, state));
}

/**
 * @brief Test that get_capabilities does not advertise pen/touch features,
 *        which are unimplemented on macOS.
 */
TEST(MacosCapabilities, NoPenTouch) {
  const auto caps = platf::get_capabilities();
  EXPECT_EQ(caps & platf::platform_caps::pen_touch, 0u);
  EXPECT_EQ(caps & platf::platform_caps::controller_touch, 0u);
}

#endif  // __APPLE__
