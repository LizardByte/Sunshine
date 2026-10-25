/**
 * @file tests/unit/platform/linux/test_desktop_bounds.cpp
 * @brief Tests for the bounding box absolute input maps across.
 */

#ifdef __linux__

  // test includes
  #include "../../../tests_common.h"

  // local includes
  #include "src/platform/linux/desktop_bounds.h"

namespace {
  platf::touch_port_t monitor(int x, int y, int width, int height, int logical_width, int logical_height) {
    return {x, y, width, height, logical_width, logical_height};
  }
}  // namespace

TEST(DesktopBoundsTest, NoMonitorsIsEmpty) {
  const auto bounds = platf::desktop_bounds({});
  EXPECT_EQ(bounds.offset_x, 0);
  EXPECT_EQ(bounds.width, 0);
  EXPECT_EQ(bounds.logical_width, 0);
}

TEST(DesktopBoundsTest, LayoutAtTheOriginIsUnchanged) {
  const auto bounds = platf::desktop_bounds({monitor(0, 0, 3840, 2160, 3840, 2160), monitor(3840, 0, 2560, 1440, 2560, 1440)});
  EXPECT_EQ(bounds.offset_x, 0);
  EXPECT_EQ(bounds.offset_y, 0);
  EXPECT_EQ(bounds.width, 6400);
  EXPECT_EQ(bounds.height, 2160);
  EXPECT_EQ(bounds.logical_width, 6400);
  EXPECT_EQ(bounds.logical_height, 2160);
}

TEST(DesktopBoundsTest, MonitorLeftOfTheOriginMovesTheOrigin) {
  // A 4K screen at the origin, a 1440p screen left of it, a scaled portrait screen right of it.
  const auto bounds = platf::desktop_bounds({
    monitor(0, 0, 3840, 2160, 3840, 2160),
    monitor(-2560, 360, 2560, 1440, 2560, 1440),
    monitor(3840, 0, 2160, 3840, 1440, 2560),
  });
  EXPECT_EQ(bounds.offset_x, -2560);
  EXPECT_EQ(bounds.offset_y, 0);
  EXPECT_EQ(bounds.logical_width, 7840);
  EXPECT_EQ(bounds.logical_height, 2560);
  EXPECT_EQ(bounds.width, 8560);
  EXPECT_EQ(bounds.height, 3840);
}

TEST(DesktopBoundsTest, MonitorAboveTheOriginMovesTheOrigin) {
  const auto bounds = platf::desktop_bounds({monitor(0, 0, 1920, 1080, 1920, 1080), monitor(0, -1080, 1920, 1080, 1920, 1080)});
  EXPECT_EQ(bounds.offset_y, -1080);
  EXPECT_EQ(bounds.height, 2160);
}

TEST(DesktopBoundsTest, NoLogicalSizeLeavesTheLogicalEnvironmentEmpty) {
  const auto bounds = platf::desktop_bounds({monitor(-1920, 0, 1920, 1080, 0, 0), monitor(0, 0, 1920, 1080, 0, 0)});
  EXPECT_EQ(bounds.width, 3840);
  EXPECT_EQ(bounds.logical_width, 0);
  EXPECT_EQ(bounds.logical_height, 0);
}

#endif
