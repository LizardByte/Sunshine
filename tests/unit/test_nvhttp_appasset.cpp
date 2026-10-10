#include "../tests_common.h"

#include <filesystem>
#include <fstream>
#include <future>
#include <Simple-Web-Server/client_http.hpp>
#include <Simple-Web-Server/server_http.hpp>
#include <src/nvhttp.h>
#include <src/process.h>
#include <thread>

class NvhttpAppassetTest: public BaseTest {
protected:
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

    server->resource["^/appasset$"]["GET"] = [](
                                               const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> &response,
                                               const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> &request
                                             ) {
      nvhttp::test_support::appasset_http(response, request);
    };

    std::promise<unsigned short> port_promise;
    auto port_future = port_promise.get_future();

    server_thread = std::jthread([this, &port_promise]() {
      server->start([&port_promise, this]() {
        port_promise.set_value(server->getLocalPort());
      });
    });

    port = port_future.get();
    client = std::make_unique<SimpleWeb::Client<SimpleWeb::HTTP>>("localhost:" + std::to_string(port));
  }

  void TearDown() override {
    if (server) {
      server->stop();
    }
    BaseTest::TearDown();
  }
};

TEST_F(NvhttpAppassetTest, MissingFallback) {
  // Ensure the default image (box.png) does not exist
  std::filesystem::remove(test_assets_dir / "box.png");

  auto response = client->request("GET", "/appasset?appid=9999");
  EXPECT_EQ(response->status_code, "404 Not Found");
  auto it = response->header.find("Content-Length");
  ASSERT_NE(it, response->header.end());
  EXPECT_EQ(it->second, "0");
  EXPECT_EQ(response->content.string(), "");
}

TEST_F(NvhttpAppassetTest, LockedFallback) {
#ifdef _WIN32
  GTEST_SKIP() << "Filesystem permission errors are handled differently on Windows";
#else
  // Create box.png but make its parent directory unreadable
  auto unreadable_dir = test_assets_dir / "unreadable";
  std::filesystem::create_directories(unreadable_dir);

  // Wait, DEFAULT_APP_IMAGE_PATH expects box.png inside test_assets_dir.
  // We can't change the path that get_app_image uses without changing the config or adding an app.
  // So we add an app to proc::proc pointing to our unreadable file!

  auto app_image_path = unreadable_dir / "test.png";
  std::ofstream(app_image_path) << "dummy image data";
  std::filesystem::permissions(unreadable_dir, std::filesystem::perms::none);

  proc::ctx_t test_app;
  test_app.name = "LockedApp";
  test_app.image_path = app_image_path.string();
  test_app.id = "1234";

  proc::proc.get_apps().push_back(test_app);

  auto response = client->request("GET", "/appasset?appid=1234");

  std::filesystem::permissions(unreadable_dir, std::filesystem::perms::all);
  proc::proc.get_apps().pop_back();

  EXPECT_EQ(response->status_code, "500 Internal Server Error");
  auto it = response->header.find("Content-Length");
  ASSERT_NE(it, response->header.end());
  EXPECT_EQ(it->second, "0");
  EXPECT_EQ(response->content.string(), "");
#endif
}

TEST_F(NvhttpAppassetTest, ReadableImage) {
  auto app_image_path = test_assets_dir / "test2.png";
  std::string image_data = "mock image content";
  std::ofstream(app_image_path, std::ios::binary) << image_data;

  proc::ctx_t test_app;
  test_app.name = "ReadableApp";
  test_app.image_path = app_image_path.string();
  test_app.id = "5678";

  proc::proc.get_apps().push_back(test_app);

  auto response = client->request("GET", "/appasset?appid=5678");

  proc::proc.get_apps().pop_back();

  EXPECT_EQ(response->status_code, "200 OK");
  EXPECT_EQ(response->content.string(), image_data);
}
