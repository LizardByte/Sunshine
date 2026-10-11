/**
 * @file tests/unit/test_httpcommon.cpp
 * @brief Test src/httpcommon.*.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <filesystem>
#include <fstream>

// lib includes
#include <curl/curl.h>

// local includes
#include <src/httpcommon.h>

struct UrlEscapeTest: BaseTest, testing::WithParamInterface<std::tuple<std::string, std::string>> {};

TEST_P(UrlEscapeTest, Run) {
  const auto &[input, expected] = GetParam();
  ASSERT_EQ(http::url_escape(input), expected);
}

INSTANTIATE_TEST_SUITE_P(
  UrlEscapeTests,
  UrlEscapeTest,
  testing::Values(
    std::make_tuple("igdb_0123456789", "igdb_0123456789"),
    std::make_tuple("../../../", "..%2F..%2F..%2F"),
    std::make_tuple("..*\\", "..%2A%5C")
  )
);

struct UrlGetHostTest: BaseTest, testing::WithParamInterface<std::tuple<std::string, std::string>> {};

TEST_P(UrlGetHostTest, Run) {
  const auto &[input, expected] = GetParam();
  ASSERT_EQ(http::url_get_host(input), expected);
}

INSTANTIATE_TEST_SUITE_P(
  UrlGetHostTests,
  UrlGetHostTest,
  testing::Values(
    std::make_tuple("https://images.igdb.com/example.txt", "images.igdb.com"),
    std::make_tuple("http://localhost:8080", "localhost"),
    std::make_tuple("nonsense!!}{::", "")
  )
);

struct DownloadFileTest: BaseTest, testing::WithParamInterface<std::tuple<std::string, std::string>> {};

TEST_P(DownloadFileTest, Run) {
  const auto &[url, filename] = GetParam();
  const std::string test_dir = platf::appdata().string() + "/tests/";
  std::string path = test_dir + filename;
  ASSERT_TRUE(http::download_file(url, path, CURL_SSLVERSION_TLSv1_0));
}

#ifdef SUNSHINE_BUILD_FLATPAK
// requires running `npm run serve` prior to running the tests
constexpr const char *URL_1 = "http://0.0.0.0:3000/hello.txt";
constexpr const char *URL_2 = "http://0.0.0.0:3000/hello-redirect.txt";
#else
constexpr const char *URL_1 = "https://httpbin.org/base64/aGVsbG8h";
constexpr const char *URL_2 = "https://httpbin.org/redirect-to?url=/base64/aGVsbG8h";
#endif

INSTANTIATE_TEST_SUITE_P(
  DownloadFileTests,
  DownloadFileTest,
  testing::Values(
    std::make_tuple(URL_1, "hello.txt"),
    std::make_tuple(URL_2, "hello-redirect.txt")
  )
);

/**
 * @brief Credential reload publishes all fields together and preserves them after an incomplete file.
 */
TEST(HttpCredentialStoreTest, CompleteReloadAndFailuresPreserveIndependentStores) {
  config::store_t first;
  config::store_t second;
  first.update_credentials({"previous", "previous hash", "previous salt"});
  second.update_credentials({"other", "other hash", "other salt"});
  const auto file = std::filesystem::path {SUNSHINE_TEST_BIN_DIR} / "http_credentials_owner.json";
  {
    std::ofstream output {file};
    output << R"({"username":"new","password":"new hash","salt":"new salt"})";
  }
  ASSERT_EQ(http::reload_user_creds(file.string(), first), 0);
  EXPECT_EQ(first.credentials_snapshot().username, "new");
  EXPECT_EQ(first.credentials_snapshot().password, "new hash");
  EXPECT_EQ(first.credentials_snapshot().salt, "new salt");
  EXPECT_EQ(second.credentials_snapshot().username, "other");
  {
    std::ofstream output {file};
    output << R"({"username":"partial","password":"partial hash"})";
  }
  EXPECT_NE(http::reload_user_creds(file.string(), first), 0);
  EXPECT_EQ(first.credentials_snapshot().username, "new");
  EXPECT_EQ(first.credentials_snapshot().password, "new hash");
  EXPECT_EQ(first.credentials_snapshot().salt, "new salt");
  std::filesystem::remove(file);
  EXPECT_NE(http::reload_user_creds(file.string(), first), 0);
  EXPECT_EQ(first.credentials_snapshot().username, "new");
}
