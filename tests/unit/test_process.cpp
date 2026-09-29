/**
 * @file tests/unit/test_process.cpp
 * @brief Test src/process.* functions.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <filesystem>
#include <fstream>
#include <thread>

// lib imports
#include <boost/process/v1.hpp>

// local includes
#include <src/process.h>

namespace fs = std::filesystem;

TEST(ProcessTest, PrepareCommand) {
#ifdef SUNSHINE_BUILD_FLATPAK
  EXPECT_EQ(proc::prepare_command("steam"), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command("flatpak-spawn --host steam"), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command("  flatpak-spawn --host steam  "), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command("  steam  "), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command(""), "");
  EXPECT_EQ(proc::prepare_command("  \t"), "");
#else
  EXPECT_EQ(proc::prepare_command("steam"), "steam");
  EXPECT_EQ(proc::prepare_command("  steam  "), "  steam  ");
  EXPECT_EQ(proc::prepare_command("flatpak-spawn --host steam"), "flatpak-spawn --host steam");
  EXPECT_EQ(proc::prepare_command(""), "");
#endif
}

class ProcessPNGTest: public BaseTest {  // NOSONAR(cpp:S3656): protected members are intentional for test fixture subclassing
protected:
  void SetUp() override {
    BaseTest::SetUp();
    // Create test directory
    test_dir = fs::temp_directory_path() / "sunshine_process_png_test";  // NOSONAR(cpp:S5443): safe for tests
    fs::create_directories(test_dir);
  }

  void TearDown() override {
    // Clean up test directory
    if (fs::exists(test_dir)) {
      fs::remove_all(test_dir);
    }
    BaseTest::TearDown();
  }

  // Helper function to create a file with specific content
  void createTestFile(const fs::path &path, const std::vector<unsigned char> &content) const {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(content.data()), content.size());
    file.close();
  }

  fs::path test_dir;
};

// Tests for check_valid_png function
TEST_F(ProcessPNGTest, CheckValidPNG_ValidSignature) {
  // Valid PNG signature
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,  // PNG signature
    // Add some dummy data to make it more realistic
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "valid.png";
  createTestFile(test_file, valid_png_data);

  EXPECT_TRUE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_WrongSignature) {
  // Invalid PNG signature (wrong magic bytes)
  const std::vector<unsigned char> invalid_png_data = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "invalid.png";
  createTestFile(test_file, invalid_png_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_TooShort) {
  // File too short (less than 8 bytes)
  const std::vector<unsigned char> short_data = {
    0x89,
    0x50,
    0x4E,
    0x47
  };

  const fs::path test_file = test_dir / "short.png";
  createTestFile(test_file, short_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_EmptyFile) {
  // Empty file
  const std::vector<unsigned char> empty_data = {};

  const fs::path test_file = test_dir / "empty.png";
  createTestFile(test_file, empty_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_NonExistentFile) {
  // File doesn't exist
  const fs::path test_file = test_dir / "nonexistent.png";

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_RealFile) {
  // Test with the actual sunshine.png from the project root

  // Only run this test if the file exists
  if (const fs::path sunshine_png = fs::path(SUNSHINE_SOURCE_DIR) / "sunshine.png"; fs::exists(sunshine_png)) {
    EXPECT_TRUE(proc::check_valid_png(sunshine_png));
  } else {
    GTEST_SKIP() << "sunshine.png not found in project root";
  }
}

TEST_F(ProcessPNGTest, CheckValidPNG_JPEGFile) {
  // JPEG signature (not PNG)
  const std::vector<unsigned char> jpeg_data = {
    0xFF,
    0xD8,
    0xFF,
    0xE0,
    0x00,
    0x10,
    0x4A,
    0x46
  };

  const fs::path test_file = test_dir / "fake.png";
  createTestFile(test_file, jpeg_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_PartialSignature) {
  // Partial PNG signature (first 4 bytes correct, rest wrong)
  const std::vector<unsigned char> partial_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "partial.png";
  createTestFile(test_file, partial_png_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

// Tests for validate_app_image_path function
TEST_F(ProcessPNGTest, ValidateAppImagePath_EmptyPath) {
  // Empty path should return default
  const std::string result = proc::validate_app_image_path("");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_NonPNGExtension) {
  // Non-PNG extension should return default
  const std::string result = proc::validate_app_image_path("image.jpg");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_CaseInsensitiveExtension) {
  // Test that .PNG (uppercase) is recognized
  // Create a valid PNG file
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "test.PNG";
  createTestFile(test_file, valid_png_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  // Should accept uppercase .PNG extension
  EXPECT_NE(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_NonExistentFile) {
  // Non-existent PNG file should return default
  const std::string result = proc::validate_app_image_path("/nonexistent/path/image.png");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_InvalidPNGSignature) {
  // File with .png extension but invalid signature should return default
  const std::vector<unsigned char> invalid_data = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "invalid.png";
  createTestFile(test_file, invalid_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_ValidPNG) {
  // Valid PNG file should return the path
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "valid.png";
  createTestFile(test_file, valid_png_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  EXPECT_EQ(result, test_file.string());
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_OldSteamDefault) {
  // Test the special case for old steam image path
  const std::string result = proc::validate_app_image_path("./assets/steam.png");
  EXPECT_EQ(result, SUNSHINE_ASSETS_DIR "/steam.png");
}

/**
 * @brief Test fixture for proc_t::update_apps_and_env and proc::refresh.
 */
class ProcessRefreshTest: public BaseTest {  // NOSONAR(cpp:S3656): protected members are intentional for test fixture subclassing
protected:
  void SetUp() override {
    BaseTest::SetUp();
    test_dir = fs::temp_directory_path() / "sunshine_process_refresh_test";  // NOSONAR(cpp:S5443): safe for tests
    fs::create_directories(test_dir);
  }

  void TearDown() override {
    if (fs::exists(test_dir)) {
      fs::remove_all(test_dir);
    }
    BaseTest::TearDown();
  }

  /**
   * @brief Write a minimal valid apps.json file.
   *
   * @param path Target path for the JSON file.
   * @param app_names List of application names to include.
   */
  void writeAppsJson(const fs::path &path, const std::vector<std::string> &app_names) const {
    std::ofstream file(path);
    file << "{\n  \"env\": {},\n  \"apps\": [\n";
    for (size_t i = 0; i < app_names.size(); ++i) {
      file << R"(    { "name": ")" << app_names[i] << R"(" })";
      if (i + 1 < app_names.size()) {
        file << ",";
      }
      file << "\n";
    }
    file << "  ]\n}\n";
    file.close();

    // Artificially advance the modification time to guarantee strict monotonicity
    // across tests. This prevents the static last_apps_file_update in refresh()
    // from skipping parses when tests execute rapidly in the same clock tick.
    static int test_time_offset = 1;
    auto new_time = fs::file_time_type::clock::now() + std::chrono::seconds(test_time_offset++);
    fs::last_write_time(path, new_time);
  }

  fs::path test_dir;
};

// -------------------------------------------------------------------
// Tests for proc_t::update_apps_and_env
// -------------------------------------------------------------------

TEST_F(ProcessRefreshTest, UpdateAppsAndEnv_UpdatesAppsList) {
  // Build an initial proc_t with one app
  boost::process::v1::environment env = boost::this_process::environment();
  std::vector<proc::ctx_t> apps_initial;
  proc::ctx_t ctx_a;
  ctx_a.name = "AppA";
  ctx_a.id = "100";
  apps_initial.push_back(std::move(ctx_a));

  proc::proc_t target(std::move(env), std::move(apps_initial));
  ASSERT_EQ(target.get_apps().size(), 1u);
  EXPECT_EQ(target.get_apps()[0].name, "AppA");

  // Build a replacement proc_t with two apps
  boost::process::v1::environment env2 = boost::this_process::environment();
  std::vector<proc::ctx_t> apps_new;
  proc::ctx_t ctx_b;
  ctx_b.name = "AppB";
  ctx_b.id = "200";
  apps_new.push_back(std::move(ctx_b));
  proc::ctx_t ctx_c;
  ctx_c.name = "AppC";
  ctx_c.id = "300";
  apps_new.push_back(std::move(ctx_c));

  proc::proc_t source(std::move(env2), std::move(apps_new));

  // Act
  target.update_apps_and_env(std::move(source));

  // Assert — apps list must now contain the new apps
  ASSERT_EQ(target.get_apps().size(), 2u);
  EXPECT_EQ(target.get_apps()[0].name, "AppB");
  EXPECT_EQ(target.get_apps()[1].name, "AppC");
}

TEST_F(ProcessRefreshTest, UpdateAppsAndEnv_PreservesRunningState) {
  // Build a proc_t and simulate a running app using the execute path.
  // Since we cannot easily launch a real process in a unit test, we verify
  // that update_apps_and_env does NOT reset _app_id by checking that the
  // running() return value is preserved.
  //
  // We use a "placebo" app (empty cmd) which sets _app_id without spawning
  // a real process.
  boost::process::v1::environment env = boost::this_process::environment();
  std::vector<proc::ctx_t> apps_initial;
  proc::ctx_t ctx;
  ctx.name = "Desktop";
  ctx.id = "42";
  // Leave cmd empty so execute() uses placebo mode
  apps_initial.push_back(std::move(ctx));

  proc::proc_t target(std::move(env), std::move(apps_initial));

  // Stash into global proc so execute() can find the app by ID and
  // terminate() / running() work correctly.
  auto &global_proc = proc::proc;
  auto saved = std::move(global_proc);
  global_proc = std::move(target);

  // Execute the placebo app (empty cmd → placebo = true, _app_id = 42)
  auto launch_session = std::make_shared<rtsp_stream::launch_session_t>();
  launch_session->width = 1920;
  launch_session->height = 1080;
  launch_session->fps = 60;
  launch_session->gcmap = 0;
  launch_session->enable_hdr = false;
  launch_session->host_audio = false;
  launch_session->enable_sops = false;
  launch_session->surround_info = 2;
  int rc = global_proc.execute(42, launch_session);
  ASSERT_EQ(rc, 0);
  ASSERT_EQ(global_proc.running(), 42);

  // Build a replacement proc_t with a different app list
  boost::process::v1::environment env2 = boost::this_process::environment();
  std::vector<proc::ctx_t> apps_new;
  proc::ctx_t ctx_new;
  ctx_new.name = "NewApp";
  ctx_new.id = "99";
  apps_new.push_back(std::move(ctx_new));
  proc::proc_t source(std::move(env2), std::move(apps_new));

  // Act — update only apps/env, not running process state
  global_proc.update_apps_and_env(std::move(source));

  // Assert — app list updated, but the running state is preserved
  ASSERT_EQ(global_proc.get_apps().size(), 1u);
  EXPECT_EQ(global_proc.get_apps()[0].name, "NewApp");
  EXPECT_EQ(global_proc.running(), 42) << "update_apps_and_env must not reset _app_id or placebo";

  // Cleanup
  global_proc.terminate();
  global_proc = std::move(saved);
}

TEST_F(ProcessRefreshTest, UpdateAppsAndEnv_PreservesSessionEnvironment) {
  // Verify that session-specific environment variables (SUNSHINE_APP_NAME)
  // are preserved through an update and available to undo commands.
  boost::process::v1::environment env = boost::this_process::environment();
  std::vector<proc::ctx_t> apps_initial;
  proc::ctx_t ctx;
  ctx.name = "Desktop";
  ctx.id = "42";

  // Create a prep command with an empty do_cmd (skipped during launch)
  // but a valid undo_cmd that writes SUNSHINE_APP_NAME to a file.
  fs::path out_file = test_dir / "undo_env.txt";
#ifdef _WIN32
  proc::cmd_t cmd("", "cmd.exe /c echo %SUNSHINE_APP_NAME% > \"" + out_file.string() + "\"", true);
#else
  proc::cmd_t cmd("", "sh -c \"echo $SUNSHINE_APP_NAME > '" + out_file.string() + "'\"", true);
#endif
  ctx.prep_cmds.push_back(std::move(cmd));
  apps_initial.push_back(std::move(ctx));

  proc::proc_t target(std::move(env), std::move(apps_initial));

  auto &global_proc = proc::proc;
  auto saved = std::move(global_proc);
  global_proc = std::move(target);

  // Launch placebo app to populate session variables
  auto launch_session = std::make_shared<rtsp_stream::launch_session_t>();
  launch_session->width = 1920;
  launch_session->height = 1080;
  launch_session->fps = 60;
  int rc = global_proc.execute(42, launch_session);
  ASSERT_EQ(rc, 0);

  // Update apps and env
  boost::process::v1::environment env2 = boost::this_process::environment();
  std::vector<proc::ctx_t> apps_new;
  proc::ctx_t ctx_new;
  ctx_new.name = "NewApp";
  ctx_new.id = "99";
  apps_new.push_back(std::move(ctx_new));
  proc::proc_t source(std::move(env2), std::move(apps_new));

  global_proc.update_apps_and_env(std::move(source));

  // Terminate should run the undo_cmd using the preserved environment
  global_proc.terminate();

  // Verify the undo command wrote the app name to the file
  ASSERT_TRUE(fs::exists(out_file)) << "Undo command failed to execute or write file";
  std::ifstream ifs(out_file);
  std::string content;
  std::getline(ifs, content);
  
  // Trim trailing whitespace (like \r\n from echo)
  content.erase(content.find_last_not_of(" \n\r\t") + 1);
  EXPECT_EQ(content, "Desktop") << "SUNSHINE_APP_NAME was not preserved in the environment";

  global_proc = std::move(saved);
}

// -------------------------------------------------------------------
// Tests for proc::refresh
// -------------------------------------------------------------------

TEST_F(ProcessRefreshTest, Refresh_ParsesFileOnFirstCall) {
  // The first call to refresh() must always parse the file, regardless
  // of what file_time_type{} compares to relative to the file timestamp.
  //
  // NOTE: Because refresh() uses static local variables (has_parsed, last_apps_file_update),
  // this test exercises the very first call in this test process.  Subsequent
  // tests that call refresh() share the same static state.
  const fs::path apps_file = test_dir / "apps_initial.json";
  writeAppsJson(apps_file, {"TestApp1", "TestApp2"});

  // Save and clear global proc
  auto saved = std::move(proc::proc);
  proc::proc = proc::proc_t {};

  ASSERT_TRUE(proc::proc.get_apps().empty());

  // Act
  proc::refresh(apps_file.string());

  // Assert — must have been parsed
  ASSERT_EQ(proc::proc.get_apps().size(), 2u);
  EXPECT_EQ(proc::proc.get_apps()[0].name, "TestApp1");
  EXPECT_EQ(proc::proc.get_apps()[1].name, "TestApp2");

  // Cleanup
  proc::proc = std::move(saved);
}

TEST_F(ProcessRefreshTest, Refresh_SkipsUnchangedFile) {
  const fs::path apps_file = test_dir / "apps_skip.json";
  writeAppsJson(apps_file, {"OriginalApp"});

  auto saved = std::move(proc::proc);
  proc::proc = proc::proc_t {};

  // First refresh — should parse
  proc::refresh(apps_file.string());
  ASSERT_EQ(proc::proc.get_apps().size(), 1u);

  // Mutate apps in-memory to detect whether the second refresh re-parses
  proc::proc.get_apps()[0].name = "Mutated";

  // Second refresh with unchanged file — should be skipped
  proc::refresh(apps_file.string());
  EXPECT_EQ(proc::proc.get_apps()[0].name, "Mutated")
    << "refresh() should skip re-parse when the file timestamp has not changed";

  // Cleanup
  proc::proc = std::move(saved);
}

TEST_F(ProcessRefreshTest, Refresh_ReparseAfterFileModified) {
  const fs::path apps_file = test_dir / "apps_modified.json";
  writeAppsJson(apps_file, {"BeforeEdit"});

  auto saved = std::move(proc::proc);
  proc::proc = proc::proc_t {};

  proc::refresh(apps_file.string());
  ASSERT_EQ(proc::proc.get_apps().size(), 1u);
  EXPECT_EQ(proc::proc.get_apps()[0].name, "BeforeEdit");

  // Rewrite to ensure a new timestamp
  writeAppsJson(apps_file, {"AfterEdit"});

  proc::refresh(apps_file.string());
  ASSERT_EQ(proc::proc.get_apps().size(), 1u);
  EXPECT_EQ(proc::proc.get_apps()[0].name, "AfterEdit");

  // Cleanup
  proc::proc = std::move(saved);
}

TEST_F(ProcessRefreshTest, Refresh_PreservesRunningAppDuringReparse) {
  // This is the critical regression test: refreshing the app list while an
  // app is running must not lose the active process state.
  const fs::path apps_file = test_dir / "apps_running.json";
  writeAppsJson(apps_file, {"Desktop"});

  auto saved = std::move(proc::proc);
  proc::proc = proc::proc_t {};

  // Initial parse
  proc::refresh(apps_file.string());
  ASSERT_FALSE(proc::proc.get_apps().empty());

  // Find the ID of "Desktop" as assigned by parse
  auto desktop_id = std::stoi(proc::proc.get_apps()[0].id);

  // Execute it in placebo mode (empty cmd)
  auto launch_session = std::make_shared<rtsp_stream::launch_session_t>();
  launch_session->width = 1920;
  launch_session->height = 1080;
  launch_session->fps = 60;
  launch_session->gcmap = 0;
  launch_session->enable_hdr = false;
  launch_session->host_audio = false;
  launch_session->enable_sops = false;
  launch_session->surround_info = 2;
  int rc = proc::proc.execute(desktop_id, launch_session);
  ASSERT_EQ(rc, 0);
  ASSERT_EQ(proc::proc.running(), desktop_id);

  // Simulate an external edit to the apps file
  writeAppsJson(apps_file, {"Desktop", "NewApp"});

  // Act — refresh while the app is "running"
  proc::refresh(apps_file.string());

  // Assert — app list is updated but the running state is preserved
  ASSERT_EQ(proc::proc.get_apps().size(), 2u);
  EXPECT_EQ(proc::proc.running(), desktop_id)
    << "refresh() during a running session must preserve active process state";

  // Cleanup — terminate before restoring
  proc::proc.terminate();
  proc::proc = std::move(saved);
}

// -------------------------------------------------------------------
// Test for file_time_type{} comparison (documents the UCRT64 issue)
// -------------------------------------------------------------------

TEST_F(ProcessRefreshTest, FileTimeType_DefaultValueComparison) {
  // This test documents the behavior that triggered the timestamp guard
  // regression. On some toolchains (MSYS2 UCRT64 GCC 16.2), file timestamps
  // and clock::now() can compare less-or-equal to file_time_type{}.
  //
  // The fix uses std::optional<file_time_type> instead of relying on
  // file_time_type{} as a "never parsed" indicator.
  const auto default_time = std::filesystem::file_time_type {};
  const auto now_time = std::filesystem::file_time_type::clock::now();

  // We cannot assert the comparison direction since it is platform-dependent.
  // Instead, just log and document both cases.
  if (now_time <= default_time) {
    BOOST_LOG(info) << "file_time_type::clock::now() <= file_time_type{} is TRUE on this platform "
                    << "(this is the UCRT64 case that requires the optional sentinel)";
  } else {
    BOOST_LOG(info) << "file_time_type::clock::now() > file_time_type{} on this platform "
                    << "(default-init guard would have worked, but optional is still safer)";
  }

  // The actual correctness is verified by Refresh_ParsesFileOnFirstCall above —
  // if that test passes, the optional fix works regardless of platform behavior.
  SUCCEED();
}

