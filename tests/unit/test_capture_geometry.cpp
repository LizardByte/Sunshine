/**
 * @file tests/unit/test_capture_geometry.cpp
 * @brief Test discovery snapshots used by Linux capture resources.
 */
#include "../tests_common.h"

#include <src/platform/linux/capture_geometry.h>

TEST(CaptureGeometryTest, EmptyDiscoveryKeepsAllExtentsAtOrigin) {
  const platf::capture_geometry_t empty;
  EXPECT_EQ(empty.width, 0);
  EXPECT_EQ(empty.height, 0);
  EXPECT_EQ(empty.logical_width, 0);
  EXPECT_EQ(empty.logical_height, 0);
}

TEST(CaptureGeometryTest, IndependentSnapshotsKeepMixedScaleOutputBounds) {
  platf::capture_geometry_t first;
  first.include({.offset_x = 0, .offset_y = 0, .width = 3840, .height = 2160, .logical_width = 1920, .logical_height = 1080});
  first.include({.offset_x = 1920, .offset_y = 100, .width = 2560, .height = 1440, .logical_width = 2560, .logical_height = 1440});
  EXPECT_EQ(first.width, 4480);
  EXPECT_EQ(first.height, 2160);
  EXPECT_EQ(first.logical_width, 4480);
  EXPECT_EQ(first.logical_height, 1540);
  const auto capture_snapshot = first;
  platf::capture_geometry_t replacement;
  replacement.include({.width = 1280, .height = 720, .logical_width = 1280, .logical_height = 720});
  first = replacement;
  EXPECT_EQ(first.width, 1280);
  EXPECT_EQ(capture_snapshot.width, 4480);
  EXPECT_EQ(capture_snapshot.logical_height, 1540);
}

TEST(CaptureGeometryTest, OutputsLeftOfOriginDoNotShrinkExistingBounds) {
  platf::capture_geometry_t desktop;
  desktop.include({.offset_x = -1920, .offset_y = -1080, .width = 1920, .height = 1080, .logical_width = 1920, .logical_height = 1080});
  EXPECT_EQ(desktop.width, 0);
  EXPECT_EQ(desktop.height, 0);
  desktop.include({.width = 1920, .height = 1080, .logical_width = 1920, .logical_height = 1080});
  desktop.include({.offset_x = -500, .offset_y = -500, .width = 600, .height = 600, .logical_width = 600, .logical_height = 600});
  EXPECT_EQ(desktop.width, 1920);
  EXPECT_EQ(desktop.height, 1080);
  EXPECT_EQ(desktop.logical_width, 1920);
  EXPECT_EQ(desktop.logical_height, 1080);
}
