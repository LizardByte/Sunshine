#include "../tests_common.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <Simple-Web-Server/client_http.hpp>
#include <Simple-Web-Server/server_http.hpp>
#include <src/nvhttp.h>
#include <src/process.h>
#include <src/utility.h>
#include <thread>

#ifdef _WIN32
  #include <share.h>
#endif

class NvhttpAppassetTest: public BaseTest {
public:
  std::unique_ptr<SimpleWeb::Server<SimpleWeb::HTTP>> server;
  std::unique_ptr<SimpleWeb::Client<SimpleWeb::HTTP>> client;
  std::jthread server_thread;
  unsigned short port = 0;
  std::filesystem::path test_assets_dir;

  void SetUp() override {
    BaseTest::SetUp();

    // Setup assets directory
    test_assets_dir = SUNSHINE_ASSETS_DIR;
    std::filesystem::create_directories(test_assets_dir);

    // Set up server
    server = std::make_unique<SimpleWeb::Server<SimpleWeb::HTTP>>();
    server->config.port = 0;
    server->config.reuse_address = true;
    server->config.timeout_request = 5;
    server->config.timeout_content = 5;

    server->resource["^/appasset$"]["GET"] = [](
                                               const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> &response,
                                               const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> &request
                                             ) {
      nvhttp::test_support::appasset_http(response, request);
    };

    server_thread = std::jthread([this]() {
      server->start([this](const unsigned short assigned_port) {
        port = assigned_port;
      });
    });

    for (int attempt = 0; attempt < 100 && port == 0; ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds {10});
    }

    // In gtest, SetUp doesn't return value but we can use FAIL()
    if (port == 0) {
      FAIL() << "Server failed to start";
    }

    client = std::make_unique<SimpleWeb::Client<SimpleWeb::HTTP>>(std::format("localhost:{}", port));
    client->config.timeout = 5;
  }

  void TearDown() override {
    if (server) {
      server->stop();
    }
    if (server_thread.joinable()) {
      server_thread.join();
    }
    client.reset();
    server.reset();
    BaseTest::TearDown();
  }
};

TEST_F(NvhttpAppassetTest, MissingFallback) {
  // Ensure the default image (box.png) does not exist
  std::error_code ec;
  std::filesystem::remove(test_assets_dir / "box.png", ec);

  auto response = client->request("GET", "/appasset?appid=9999");
  EXPECT_EQ(response->status_code, "404 Not Found");
  auto it = response->header.find("Content-Length");
  ASSERT_NE(it, response->header.end());
  EXPECT_EQ(it->second, "0");
  EXPECT_EQ(response->content.string(), "");
}

TEST_F(NvhttpAppassetTest, LookupErrorFallback) {
  // Query an unconfigured app so get_app_image() returns the default path directly,
  // and remove search permission from test_assets_dir itself.
#ifdef _WIN32
  GTEST_SKIP() << "Filesystem permission errors for directories are handled differently on Windows";
#else
  const auto original_permissions = std::filesystem::status(test_assets_dir).permissions();
  auto restore_permissions = util::fail_guard([this, original_permissions]() {
    std::error_code ec;
    std::filesystem::permissions(test_assets_dir, original_permissions, ec);
    EXPECT_FALSE(ec) << ec.message();
  });
  std::filesystem::permissions(test_assets_dir, std::filesystem::perms::none);

  auto response = client->request("GET", "/appasset?appid=9999");

  std::error_code ec;
  std::filesystem::permissions(test_assets_dir, original_permissions, ec);
  restore_permissions.dismiss();

  EXPECT_EQ(response->status_code, "500 Internal Server Error");
  auto it = response->header.find("Content-Length");
  ASSERT_NE(it, response->header.end());
  EXPECT_EQ(it->second, "0");
  EXPECT_EQ(response->content.string(), "");
#endif
}

TEST_F(NvhttpAppassetTest, UnreadableFallback) {
  // Cover an existing unreadable fallback to exercise the exists == true branch
  auto fallback_path = test_assets_dir / "box.png";
  std::ofstream(fallback_path) << "dummy image data";

  auto restore_file = util::fail_guard([fallback_path]() {
    std::error_code ec;
    std::filesystem::permissions(fallback_path, std::filesystem::perms::all, ec);
    std::filesystem::remove(fallback_path, ec);
  });

#ifdef _WIN32
  FILE *exclusive_handle = _fsopen(fallback_path.string().c_str(), "w", _SH_DENYRW);
  ASSERT_NE(exclusive_handle, nullptr);
#else
  std::filesystem::permissions(fallback_path, std::filesystem::perms::none);
#endif

  auto response = client->request("GET", "/appasset?appid=9999");

#ifdef _WIN32
  fclose(exclusive_handle);
#else
  std::filesystem::permissions(fallback_path, std::filesystem::perms::all);
#endif

  std::error_code ec;
  std::filesystem::remove(fallback_path, ec);
  restore_file.dismiss();

  EXPECT_EQ(response->status_code, "500 Internal Server Error");
  auto it = response->header.find("Content-Length");
  ASSERT_NE(it, response->header.end());
  EXPECT_EQ(it->second, "0");
  EXPECT_EQ(response->content.string(), "");
}

TEST_F(NvhttpAppassetTest, ReadableImage) {
  auto app_image_path = test_assets_dir / "test2.png";
  // Must be a valid PNG to pass production PNG validation
  std::string image_data = "\x89PNG\x0D\x0A\x1A\x0A mock image content";
  std::ofstream(app_image_path, std::ios::binary) << image_data;

  proc::ctx_t test_app;
  test_app.name = "ReadableApp";
  test_app.image_path = app_image_path.string();
  test_app.id = "5678";

  auto restore_app = util::fail_guard([]() {
    proc::proc.get_apps().pop_back();
  });

  proc::proc.get_apps().push_back(test_app);

  auto response = client->request("GET", "/appasset?appid=5678");

  proc::proc.get_apps().pop_back();
  restore_app.dismiss();

  std::error_code ec;
  std::filesystem::remove(app_image_path, ec);

  EXPECT_EQ(response->status_code, "200 OK");
  EXPECT_EQ(response->content.string(), image_data);
}
