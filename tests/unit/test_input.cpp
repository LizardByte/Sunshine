/**
 * @file tests/unit/test_input.cpp
 * @brief Test input coordinate normalization helpers.
 */

// test includes
#include "../tests_common.h"

// local includes
#include "src/input.h"

TEST(InputTouchPortTest, EncodedPillarboxUsesStreamedDisplayResolution) {
  const input::touch_port_t touch_port {
    {3840, 0, 1920, 1080},
    5120,
    2160,
    60.0f,
    0.0f,
    32.0f / 45.0f,
    1.0f,
    0,
    0
  };
  std::pair coords {5120.0f, 768.0f};

  const auto normalized_port = input::monitor_touch_port(touch_port, coords, true);

  ASSERT_TRUE(normalized_port);
  EXPECT_EQ(normalized_port->width, 1280);
  EXPECT_EQ(normalized_port->height, 768);
  EXPECT_FLOAT_EQ(coords.first, 1.0f);
  EXPECT_FLOAT_EQ(coords.second, 1.0f);
}

TEST(InputTouchPortTest, EncodedLetterboxCoversBottomOfPrimaryDisplay) {
  const input::touch_port_t touch_port {
    {1280, 0, 1280, 1024},
    2560,
    1024,
    0.0f,
    128.0f,
    1.0f,
    1.0f,
    0,
    0
  };
  std::pair coords {640.0f + touch_port.offset_x, 768.0f + touch_port.offset_y};

  const auto normalized_port = input::monitor_touch_port(touch_port, coords, true);

  ASSERT_TRUE(normalized_port);
  EXPECT_EQ(normalized_port->width, 1280);
  EXPECT_EQ(normalized_port->height, 768);
  EXPECT_FLOAT_EQ(coords.first, 0.5f);
  EXPECT_FLOAT_EQ(coords.second, 1.0f);

  constexpr platf::touch_port_t primary_display {0, 0, 3840, 2160};
  EXPECT_FLOAT_EQ(coords.first * primary_display.width, 1920.0f);
  EXPECT_FLOAT_EQ(coords.second * primary_display.height, 2160.0f);
}

TEST(InputTouchPortTest, NormalizedCoordinatesAreClampedToDisplayBounds) {
  const input::touch_port_t touch_port {
    {0, 0, 1280, 768},
    1280,
    768,
    0.0f,
    0.0f,
    1.0f,
    1.0f,
    0,
    0
  };
  std::pair coords {-1.0f, 769.0f};

  ASSERT_TRUE(input::monitor_touch_port(touch_port, coords, true));
  EXPECT_FLOAT_EQ(coords.first, 0.0f);
  EXPECT_FLOAT_EQ(coords.second, 1.0f);
}

TEST(InputTouchPortTest, InvalidEffectiveContentDimensionsAreRejected) {
  const input::touch_port_t touch_port {
    {0, 0, 1280, 768},
    1280,
    768,
    640.0f,
    0.0f,
    1.0f,
    1.0f,
    0,
    0
  };
  std::pair coords {0.0f, 0.0f};

  EXPECT_FALSE(input::monitor_touch_port(touch_port, coords, true));
}

TEST(InputTouchPortTest, InvalidCoordinateScaleIsRejected) {
  const input::touch_port_t touch_port {
    {0, 0, 1280, 768},
    1280,
    768,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0,
    0
  };
  std::pair coords {0.0f, 0.0f};

  EXPECT_FALSE(input::monitor_touch_port(touch_port, coords, false));
}

TEST(InputTouchPortTest, EncodedFrameNormalizationIsPreservedWhenPrimaryMappingIsDisabled) {
  const input::touch_port_t touch_port {
    {1280, 0, 1280, 1024},
    2560,
    1024,
    0.0f,
    128.0f,
    1.0f,
    1.0f,
    0,
    0
  };
  std::pair coords {640.0f + touch_port.offset_x, 768.0f + touch_port.offset_y};

  const auto normalized_port = input::monitor_touch_port(touch_port, coords, false);

  ASSERT_TRUE(normalized_port);
  EXPECT_EQ(normalized_port->width, 1280);
  EXPECT_EQ(normalized_port->height, 1024);
  EXPECT_FLOAT_EQ(coords.first, 0.5f);
  EXPECT_FLOAT_EQ(coords.second, 0.75f);
}
