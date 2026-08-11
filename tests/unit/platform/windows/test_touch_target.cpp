/**
 * @file tests/unit/platform/windows/test_touch_target.cpp
 * @brief Test Windows touch target selection.
 */
#include "../../../tests_common.h"

#ifdef _WIN32

  // platform includes
  #include <Windows.h>

  // standard includes
  #include <array>
  #include <limits>

  // local includes
  #include "src/platform/windows/input_utils.h"

namespace {
  /**
   * @brief Verify that two touch ports contain identical bounds.
   *
   * @param actual Actual touch port.
   * @param expected Expected touch port.
   */
  void expect_touch_ports_equal(const platf::touch_port_t &actual, const platf::touch_port_t &expected) {
    EXPECT_EQ(actual.offset_x, expected.offset_x);
    EXPECT_EQ(actual.offset_y, expected.offset_y);
    EXPECT_EQ(actual.width, expected.width);
    EXPECT_EQ(actual.height, expected.height);
    EXPECT_EQ(actual.logical_width, expected.logical_width);
    EXPECT_EQ(actual.logical_height, expected.logical_height);
  }
}  // namespace

TEST(WindowsTouchTargetTest, DisabledUsesStreamedTouchPort) {
  const platf::touch_port_t streamed_touch_port {640, 360, 2560, 1440, 1280, 720};
  const std::array displays {
    platf::win_input::display_bounds_t {0, 0, 1920, 1080, true}
  };
  const auto primary_touch_port = platf::win_input::make_primary_display_touch_port(displays);

  const auto selected_touch_port = platf::win_input::select_touch_port(streamed_touch_port, false, primary_touch_port);

  expect_touch_ports_equal(selected_touch_port, streamed_touch_port);
}

TEST(WindowsTouchTargetTest, SingleDisplayUsesPrimaryDisplayBounds) {
  const platf::touch_port_t streamed_touch_port {0, 0, 2560, 1440, 0, 0};
  const std::array displays {
    platf::win_input::display_bounds_t {0, 0, 1920, 1080, true}
  };
  const auto primary_touch_port = platf::win_input::make_primary_display_touch_port(displays);
  ASSERT_TRUE(primary_touch_port);

  const auto selected_touch_port = platf::win_input::select_touch_port(streamed_touch_port, true, primary_touch_port);

  expect_touch_ports_equal(selected_touch_port, {0, 0, 1920, 1080, 0, 0});
}

TEST(WindowsTouchTargetTest, VirtualDesktopOriginOffsetsPrimaryDisplay) {
  const std::array displays {
    platf::win_input::display_bounds_t {-2560, -1440, 2560, 1440, false},
    platf::win_input::display_bounds_t {0, 0, 1920, 1080, true}
  };
  const auto primary_touch_port = platf::win_input::make_primary_display_touch_port(displays);
  ASSERT_TRUE(primary_touch_port);

  expect_touch_ports_equal(*primary_touch_port, {2560, 1440, 1920, 1080, 0, 0});
}

TEST(WindowsTouchTargetTest, DifferentResolutionPreservesRelativePosition) {
  constexpr float normalized_x = 0.5f;
  constexpr float normalized_y = 0.5f;
  const platf::touch_port_t streamed_touch_port {3840, 0, 1280, 768, 0, 0};
  const std::array displays {
    platf::win_input::display_bounds_t {0, 0, 3840, 2160, true},
    platf::win_input::display_bounds_t {3840, 0, 1280, 768, false}
  };
  const auto primary_touch_port = platf::win_input::make_primary_display_touch_port(displays);

  const auto selected_touch_port = platf::win_input::select_touch_port(streamed_touch_port, true, primary_touch_port);
  const auto [pixel_x, pixel_y] = platf::win_input::map_normalized_touch_position(
    selected_touch_port,
    normalized_x,
    normalized_y
  );

  EXPECT_EQ(pixel_x, 1920);
  EXPECT_EQ(pixel_y, 1080);
}

TEST(WindowsTouchTargetTest, BlackBarTouchStaysInsideSelectedDisplay) {
  constexpr platf::touch_port_t selected_touch_port {2560, 1440, 1920, 1080, 0, 0};

  const auto [left, top] = platf::win_input::map_normalized_touch_position(selected_touch_port, -0.25f, -0.25f);
  const auto [right, bottom] = platf::win_input::map_normalized_touch_position(selected_touch_port, 1.25f, 1.25f);

  EXPECT_EQ(left, 2560);
  EXPECT_EQ(top, 1440);
  EXPECT_EQ(right, 4479);
  EXPECT_EQ(bottom, 2519);
}

TEST(WindowsTouchTargetTest, ExactVideoEdgeDoesNotReachAdjacentDesktop) {
  constexpr platf::touch_port_t selected_touch_port {0, 0, 3840, 2160, 0, 0};

  const auto [right, bottom] = platf::win_input::map_normalized_touch_position(selected_touch_port, 1.0f, 1.0f);

  EXPECT_EQ(right, 3839);
  EXPECT_EQ(bottom, 2159);
}

TEST(WindowsTouchRotationTest, ParsesSupportedClockwiseRotations) {
  EXPECT_EQ(platf::win_input::parse_touch_rotation("0"), platf::win_input::touch_rotation_e::none);
  EXPECT_EQ(platf::win_input::parse_touch_rotation("90"), platf::win_input::touch_rotation_e::clockwise_90);
  EXPECT_EQ(platf::win_input::parse_touch_rotation("180"), platf::win_input::touch_rotation_e::clockwise_180);
  EXPECT_EQ(platf::win_input::parse_touch_rotation("270"), platf::win_input::touch_rotation_e::clockwise_270);
  EXPECT_EQ(platf::win_input::parse_touch_rotation("invalid"), platf::win_input::touch_rotation_e::none);
}

TEST(WindowsTouchRotationTest, RotationRequiresASelectedPrimaryDisplay) {
  EXPECT_EQ(
    platf::win_input::select_touch_rotation(false, "90"),
    platf::win_input::touch_rotation_e::none
  );
  EXPECT_EQ(
    platf::win_input::select_touch_rotation(true, "90"),
    platf::win_input::touch_rotation_e::clockwise_90
  );
}

TEST(WindowsTouchRotationTest, Clockwise90MapsAllDisplayCorners) {
  constexpr platf::touch_port_t primary_touch_port {0, 0, 1920, 1080, 0, 0};
  constexpr auto rotation = platf::win_input::touch_rotation_e::clockwise_90;

  const auto map_rotated_corner = [&](float x, float y) {
    const auto [rotated_x, rotated_y] = platf::win_input::rotate_normalized_touch_position(x, y, rotation);
    return platf::win_input::map_normalized_touch_position(primary_touch_port, rotated_x, rotated_y);
  };

  EXPECT_EQ(map_rotated_corner(0.0f, 0.0f), std::pair(1919, 0));
  EXPECT_EQ(map_rotated_corner(0.0f, 1.0f), std::pair(0, 0));
  EXPECT_EQ(map_rotated_corner(1.0f, 0.0f), std::pair(1919, 1079));
  EXPECT_EQ(map_rotated_corner(1.0f, 1.0f), std::pair(0, 1079));
}

TEST(WindowsTouchRotationTest, Clockwise90CompensatesForCounterclockwiseApplicationTransform) {
  constexpr float client_x = 0.2f;
  constexpr float client_y = 0.7f;

  const auto [injected_x, injected_y] = platf::win_input::rotate_normalized_touch_position(
    client_x,
    client_y,
    platf::win_input::touch_rotation_e::clockwise_90
  );

  // An application applying (y, 1-x) receives the original point after Sunshine applies the inverse rotation.
  const auto application_x = injected_y;
  const auto application_y = 1.0f - injected_x;
  EXPECT_FLOAT_EQ(application_x, client_x);
  EXPECT_FLOAT_EQ(application_y, client_y);
}

TEST(WindowsTouchRotationTest, OtherRotationsTransformNormalizedCoordinates) {
  const auto unchanged = platf::win_input::rotate_normalized_touch_position(
    0.25f,
    0.75f,
    platf::win_input::touch_rotation_e::none
  );
  const auto clockwise_180 = platf::win_input::rotate_normalized_touch_position(
    0.25f,
    0.75f,
    platf::win_input::touch_rotation_e::clockwise_180
  );
  const auto clockwise_270 = platf::win_input::rotate_normalized_touch_position(
    0.25f,
    0.75f,
    platf::win_input::touch_rotation_e::clockwise_270
  );
  const auto unsupported = platf::win_input::rotate_normalized_touch_position(
    0.25f,
    0.75f,
    static_cast<platf::win_input::touch_rotation_e>(45)
  );

  EXPECT_EQ(unchanged, std::pair(0.25f, 0.75f));
  EXPECT_EQ(clockwise_180, std::pair(0.75f, 0.25f));
  EXPECT_EQ(clockwise_270, std::pair(0.75f, 0.75f));
  EXPECT_EQ(unsupported, unchanged);
}

TEST(WindowsTouchTargetTest, InvalidExtentAndNonFiniteCoordinatesStayAtDisplayOrigin) {
  constexpr platf::touch_port_t invalid_touch_port {640, 360, 0, -1, 0, 0};
  constexpr platf::touch_port_t valid_touch_port {640, 360, 1280, 768, 0, 0};

  const auto [invalid_x, invalid_y] = platf::win_input::map_normalized_touch_position(invalid_touch_port, 0.5f, 0.5f);
  const auto [non_finite_x, non_finite_y] = platf::win_input::map_normalized_touch_position(
    valid_touch_port,
    std::numeric_limits<float>::quiet_NaN(),
    std::numeric_limits<float>::infinity()
  );

  EXPECT_EQ(invalid_x, 640);
  EXPECT_EQ(invalid_y, 360);
  EXPECT_EQ(non_finite_x, 640);
  EXPECT_EQ(non_finite_y, 360);
}

TEST(WindowsTouchTargetTest, PrimarySelectionDoesNotMutateStreamedPortUsedByPenAndMouse) {
  platf::touch_port_t streamed_touch_port {640, 360, 2560, 1440, 1280, 720};
  const platf::touch_port_t original_streamed_touch_port = streamed_touch_port;
  const std::array displays {
    platf::win_input::display_bounds_t {-640, -360, 640, 360, false},
    platf::win_input::display_bounds_t {0, 0, 1920, 1080, true}
  };
  const auto primary_touch_port = platf::win_input::make_primary_display_touch_port(displays);

  static_cast<void>(platf::win_input::select_touch_port(streamed_touch_port, true, primary_touch_port));

  expect_touch_ports_equal(streamed_touch_port, original_streamed_touch_port);
}

TEST(WindowsTouchTargetTest, InvalidPrimaryDisplayWidthFallsBackToStreamedDisplay) {
  const platf::touch_port_t streamed_touch_port {640, 0, 2560, 1440, 0, 0};
  const std::array displays {
    platf::win_input::display_bounds_t {0, 0, 0, 1080, true}
  };
  const auto primary_touch_port = platf::win_input::make_primary_display_touch_port(displays);
  ASSERT_FALSE(primary_touch_port);

  const auto selected_touch_port = platf::win_input::select_touch_port(streamed_touch_port, true, primary_touch_port);

  expect_touch_ports_equal(selected_touch_port, streamed_touch_port);
}

TEST(WindowsTouchTargetTest, InvalidPrimaryDisplayHeightFallsBackToStreamedDisplay) {
  const platf::touch_port_t streamed_touch_port {0, 720, 1920, 1080, 0, 0};
  const std::array displays {
    platf::win_input::display_bounds_t {0, 0, 1920, -1, true}
  };
  const auto primary_touch_port = platf::win_input::make_primary_display_touch_port(displays);
  ASSERT_FALSE(primary_touch_port);

  const auto selected_touch_port = platf::win_input::select_touch_port(streamed_touch_port, true, primary_touch_port);

  expect_touch_ports_equal(selected_touch_port, streamed_touch_port);
}

TEST(WindowsTouchTargetTest, MissingPrimaryDisplayFallsBackToStreamedDisplay) {
  const platf::touch_port_t streamed_touch_port {0, 0, 1280, 768, 0, 0};
  const std::array displays {
    platf::win_input::display_bounds_t {0, 0, 1280, 768, false}
  };
  const auto primary_touch_port = platf::win_input::make_primary_display_touch_port(displays);
  ASSERT_FALSE(primary_touch_port);

  const auto selected_touch_port = platf::win_input::select_touch_port(streamed_touch_port, true, primary_touch_port);

  expect_touch_ports_equal(selected_touch_port, streamed_touch_port);
}

TEST(WindowsTouchTargetTest, EmptyDisplayLayoutIsInvalid) {
  const std::array<platf::win_input::display_bounds_t, 0> displays {};

  EXPECT_FALSE(platf::win_input::make_primary_display_touch_port(displays));
}

TEST(WindowsTouchTargetTest, MultiplePrimaryDisplaysAreInvalid) {
  const std::array displays {
    platf::win_input::display_bounds_t {0, 0, 3840, 2160, true},
    platf::win_input::display_bounds_t {3840, 0, 1280, 768, true}
  };

  EXPECT_FALSE(platf::win_input::make_primary_display_touch_port(displays));
}

TEST(WindowsTouchTargetTest, UnrepresentablePrimaryDisplayOffsetIsInvalid) {
  const std::array displays {
    platf::win_input::display_bounds_t {std::numeric_limits<int>::min(), 0, 1280, 768, false},
    platf::win_input::display_bounds_t {std::numeric_limits<int>::max(), 0, 3840, 2160, true}
  };

  EXPECT_FALSE(platf::win_input::make_primary_display_touch_port(displays));
}

TEST(WindowsTouchPointerFlagsTest, FirstContactIsPrimaryAndMouseCompatible) {
  constexpr std::uint32_t event_flags = POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_DOWN;

  const auto injected_flags = platf::win_input::apply_touch_pointer_event_flags(event_flags, LI_TOUCH_EVENT_DOWN, true);

  EXPECT_TRUE(injected_flags & POINTER_FLAG_PRIMARY);
  EXPECT_TRUE(injected_flags & POINTER_FLAG_FIRSTBUTTON);
  EXPECT_TRUE(platf::win_input::touch_pointer_blocks_primary_designation(injected_flags));

  const auto persistent_flags = platf::win_input::finish_touch_pointer_frame(injected_flags);
  EXPECT_FALSE(persistent_flags & POINTER_FLAG_DOWN);
  EXPECT_TRUE(persistent_flags & POINTER_FLAG_PRIMARY);
  EXPECT_TRUE(persistent_flags & POINTER_FLAG_FIRSTBUTTON);
}

TEST(WindowsTouchPointerFlagsTest, AdditionalContactIsNotPrimary) {
  constexpr std::uint32_t event_flags = POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_DOWN;

  const auto injected_flags = platf::win_input::apply_touch_pointer_event_flags(
    event_flags,
    LI_TOUCH_EVENT_DOWN,
    false
  );

  EXPECT_FALSE(injected_flags & POINTER_FLAG_PRIMARY);
  EXPECT_TRUE(injected_flags & POINTER_FLAG_FIRSTBUTTON);
}

TEST(WindowsTouchPointerFlagsTest, ContactMoveRetainsMouseCompatibility) {
  constexpr std::uint32_t event_flags =
    POINTER_FLAG_INRANGE |
    POINTER_FLAG_INCONTACT |
    POINTER_FLAG_UPDATE |
    POINTER_FLAG_PRIMARY;

  const auto injected_flags = platf::win_input::apply_touch_pointer_event_flags(
    event_flags,
    LI_TOUCH_EVENT_MOVE,
    false
  );
  const auto persistent_flags = platf::win_input::finish_touch_pointer_frame(injected_flags);

  EXPECT_FALSE(persistent_flags & POINTER_FLAG_UPDATE);
  EXPECT_TRUE(persistent_flags & POINTER_FLAG_PRIMARY);
  EXPECT_TRUE(persistent_flags & POINTER_FLAG_FIRSTBUTTON);
}

TEST(WindowsTouchPointerFlagsTest, PrimaryReleaseIsInjectedBeforeDesignationIsCleared) {
  constexpr std::uint32_t event_flags = POINTER_FLAG_UP | POINTER_FLAG_FIRSTBUTTON | POINTER_FLAG_PRIMARY;

  const auto injected_flags = platf::win_input::apply_touch_pointer_event_flags(event_flags, LI_TOUCH_EVENT_UP, false);

  EXPECT_TRUE(injected_flags & POINTER_FLAG_UP);
  EXPECT_TRUE(injected_flags & POINTER_FLAG_PRIMARY);
  EXPECT_FALSE(injected_flags & POINTER_FLAG_FIRSTBUTTON);
  EXPECT_TRUE(platf::win_input::touch_pointer_blocks_primary_designation(injected_flags));

  const auto persistent_flags = platf::win_input::finish_touch_pointer_frame(injected_flags);
  EXPECT_EQ(persistent_flags, POINTER_FLAG_NONE);
  EXPECT_FALSE(platf::win_input::touch_pointer_blocks_primary_designation(persistent_flags));
}

TEST(WindowsTouchPointerFlagsTest, NonContactEventsClearMouseButtonCompatibility) {
  constexpr std::array<std::uint8_t, 4> event_types {
    LI_TOUCH_EVENT_HOVER,
    LI_TOUCH_EVENT_CANCEL,
    LI_TOUCH_EVENT_CANCEL_ALL,
    LI_TOUCH_EVENT_HOVER_LEAVE
  };

  for (const auto event_type : event_types) {
    constexpr std::uint32_t event_flags = POINTER_FLAG_UPDATE | POINTER_FLAG_FIRSTBUTTON;
    const auto injected_flags = platf::win_input::apply_touch_pointer_event_flags(event_flags, event_type, false);
    EXPECT_FALSE(injected_flags & POINTER_FLAG_FIRSTBUTTON);
  }
}

TEST(WindowsTouchPointerFlagsTest, UnrelatedEventDoesNotChangeCompatibilityFlags) {
  constexpr std::uint32_t event_flags = POINTER_FLAG_INRANGE;

  EXPECT_EQ(
    platf::win_input::apply_touch_pointer_event_flags(event_flags, LI_TOUCH_EVENT_BUTTON_ONLY, true),
    event_flags
  );
  EXPECT_EQ(
    platf::win_input::apply_touch_pointer_event_flags(
      event_flags,
      std::numeric_limits<std::uint8_t>::max(),
      true
    ),
    event_flags
  );
}

#endif  // _WIN32
