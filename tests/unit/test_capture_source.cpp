/**
 * @file tests/unit/test_capture_source.cpp
 * @brief Tests for optional window and application capture selection.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <filesystem>
#include <fstream>

// local includes
#include "src/capture_source.h"
#include "src/process.h"

TEST(CaptureSourceTest, EmptyConfigStaysOnTheDisplay) {
  const auto target = capture_source::target_from_config({});

  EXPECT_EQ(target.mode, capture_source::mode_e::display);
  EXPECT_FALSE(target.window_id.has_value());
  EXPECT_TRUE(target.application_id.empty());
  EXPECT_TRUE(capture_source::mode_recognized(std::nullopt));
  EXPECT_FALSE(capture_source::mode_recognized(std::optional<std::string> {"desktop"}));
}

TEST(CaptureSourceTest, ParsesWindowAndApplicationFields) {
  const auto window = capture_source::target_from_config({
    std::optional<std::string> {"window"},
    std::optional<std::string> {"42"},
    std::optional<std::string> {"com.example.emulator"},
    std::optional<std::string> {"Emulator"},
    std::optional<std::string> {"false"},
  });
  const auto application = capture_source::target_from_config({
    std::optional<std::string> {"APPLICATION"},
    std::nullopt,
    std::optional<std::string> {"pid:77"},
    std::nullopt,
    std::optional<std::string> {"true"},
  });

  EXPECT_EQ(window.mode, capture_source::mode_e::window);
  ASSERT_TRUE(window.window_id.has_value());
  EXPECT_EQ(*window.window_id, 42U);
  EXPECT_EQ(window.application_id, "com.example.emulator");
  EXPECT_EQ(window.window_title, "Emulator");
  ASSERT_TRUE(window.show_cursor.has_value());
  EXPECT_FALSE(*window.show_cursor);
  EXPECT_EQ(application.mode, capture_source::mode_e::application);
  EXPECT_EQ(application.application_id, "pid:77");
  ASSERT_TRUE(application.show_cursor.has_value());
  EXPECT_TRUE(*application.show_cursor);
  EXPECT_EQ(capture_source::target_from_config({
                                                 std::optional<std::string> {"window"},
                                                 std::optional<std::string> {"nope"},
                                                 std::nullopt,
                                                 std::nullopt,
                                                 std::nullopt,
                                               })
              .window_id,
            std::nullopt);
}

TEST(CaptureSourceTest, MatchesExplicitWindowAndDelayedApplication) {
  const std::vector<capture_source::window_snapshot_t> windows {
    {10, "Other", "com.example.other", 4, 0, 0, 100, 100, true},
    {20, "Emulator (small)", "com.example.emulator", 8, 10, 20, 200, 100, false},
    {21, "Emulator", "com.example.emulator", 8, 30, 40, 640, 480, true},
    {30, "Helper", "com.example.emulator", 9, 800, 10, 50, 50, true},
  };

  capture_source::target_t by_id;
  by_id.mode = capture_source::mode_e::window;
  by_id.window_id = 20;
  const auto explicit_window = capture_source::match_target(by_id, windows);
  ASSERT_TRUE(explicit_window.has_value());
  EXPECT_EQ(explicit_window->window_id, 20U);
  EXPECT_FALSE(explicit_window->application);

  capture_source::target_t by_title;
  by_title.mode = capture_source::mode_e::window;
  by_title.application_id = "com.example.emulator";
  by_title.window_title = "emulator";
  const auto titled = capture_source::match_target(by_title, windows);
  ASSERT_TRUE(titled.has_value());
  EXPECT_EQ(titled->window_id, 21U);

  capture_source::target_t launched;
  launched.mode = capture_source::mode_e::window;
  launched.launched_pid = 9;
  const auto by_pid = capture_source::match_target(launched, windows);
  ASSERT_TRUE(by_pid.has_value());
  EXPECT_EQ(by_pid->window_id, 30U);

  capture_source::target_t application;
  application.mode = capture_source::mode_e::application;
  application.application_id = "pid:8";
  const auto bounds = capture_source::match_target(application, windows);
  ASSERT_TRUE(bounds.has_value());
  EXPECT_TRUE(bounds->application);
  EXPECT_EQ(bounds->window_id, 21U);
  EXPECT_EQ(bounds->x, 30);
  EXPECT_EQ(bounds->y, 40);
  EXPECT_EQ(bounds->width, 640);
  EXPECT_EQ(bounds->height, 480);

  capture_source::target_t missing;
  missing.mode = capture_source::mode_e::window;
  missing.window_id = 99;
  EXPECT_FALSE(capture_source::match_target(missing, windows).has_value());
  EXPECT_FALSE(capture_source::match_target(capture_source::target_t {}, windows).has_value());
}

TEST(CaptureSourceTest, ConvertsInterleavedAudioLayouts) {
  const float stereo[] {0.0F, 1.0F, 1.0F, -1.0F};
  float mono[2] {};
  capture_source::convert_interleaved(stereo, 2, 48000, 2, mono, 2, 48000, 1);
  EXPECT_FLOAT_EQ(mono[0], 0.5F);
  EXPECT_FLOAT_EQ(mono[1], 0.0F);

  float copied[4] {};
  capture_source::convert_interleaved(stereo, 2, 48000, 2, copied, 2, 48000, 2);
  EXPECT_FLOAT_EQ(copied[0], 0.0F);
  EXPECT_FLOAT_EQ(copied[1], 1.0F);
  EXPECT_FLOAT_EQ(copied[3], -1.0F);
}

TEST(CaptureSourceTest, AppsJsonSelectsWindowCapture) {
  const auto path = std::filesystem::temp_directory_path() / "sunshine_capture_source_apps.json";
  {
    std::ofstream file(path);
    file << R"({
      "env": {},
      "apps": [
        {
          "name": "Emulator",
          "capture": "window",
          "window-id": 15,
          "application-id": "com.example.emulator",
          "window-title": "Game",
          "capture-cursor": false
        }
      ]
    })";
  }

  const auto parsed = proc::parse(path.string());
  std::filesystem::remove(path);
  ASSERT_TRUE(parsed.has_value());
  const auto &apps = parsed->get_apps();
  ASSERT_EQ(apps.size(), 1U);
  EXPECT_EQ(apps[0].capture.mode, capture_source::mode_e::window);
  ASSERT_TRUE(apps[0].capture.window_id.has_value());
  EXPECT_EQ(*apps[0].capture.window_id, 15U);
  EXPECT_EQ(apps[0].capture.application_id, "com.example.emulator");
  EXPECT_EQ(apps[0].capture.window_title, "Game");
  ASSERT_TRUE(apps[0].capture.show_cursor.has_value());
  EXPECT_FALSE(*apps[0].capture.show_cursor);
}
