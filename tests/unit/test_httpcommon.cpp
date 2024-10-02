/**
 * @file tests/unit/test_httpcommon.cpp
 * @brief Test src/httpcommon.*.
 */
#include <src/httpcommon.h>

#include "../tests_common.h"

struct UrlEscapeTest: testing::TestWithParam<std::tuple<std::string, std::string>> {};

TEST_P(UrlEscapeTest, Run) {
  auto [input, expected] = GetParam();
  ASSERT_EQ(http::url_escape(input), expected);
}

INSTANTIATE_TEST_SUITE_P(
  UrlEscapeTests,
  UrlEscapeTest,
  testing::Values(
    std::make_tuple("igdb_0123456789", "igdb_0123456789"),
    std::make_tuple("../../../", "..%2F..%2F..%2F"),
    std::make_tuple("..*\\", "..%2A%5C")));

struct UrlGetHostTest: testing::TestWithParam<std::tuple<std::string, std::string>> {};

TEST_P(UrlGetHostTest, Run) {
  auto [input, expected] = GetParam();
  ASSERT_EQ(http::url_get_host(input), expected);
}

INSTANTIATE_TEST_SUITE_P(
  UrlGetHostTests,
  UrlGetHostTest,
  testing::Values(
    std::make_tuple("https://images.igdb.com/example.txt", "images.igdb.com"),
    std::make_tuple("http://localhost:8080", "localhost"),
    std::make_tuple("nonsense!!}{::", "")));

struct DownloadFileTest: testing::TestWithParam<std::tuple<std::string, std::string>> {};

TEST_P(DownloadFileTest, Run) {
  auto [url, filename] = GetParam();
  const std::string test_dir = platf::appdata().string() + "/tests/";
  std::basic_string path = test_dir + filename;
  ASSERT_TRUE(http::download_file(url, path));
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
    std::make_tuple(URL_2, "hello-redirect.txt")));
