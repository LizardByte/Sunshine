#include <gtest/gtest.h>

#include "src/httpcommon.h"

using namespace http;

TEST(HttpCommonTest, UrlEscape) {
  ASSERT_EQ(url_escape("igdb_0123456789"), "igdb_0123456789");
  ASSERT_EQ(url_escape("../../../"), "..%2F..%2F..%2F");
  ASSERT_EQ(url_escape("..*\\"), "..%2A%5C");
}

TEST(HttpCommonTest, UrlGetHost) {
  ASSERT_EQ(url_get_host("https://images.igdb.com/example.txt"), "images.igdb.com");
  ASSERT_EQ(url_get_host("http://localhost:8080"), "localhost");
  ASSERT_EQ(url_get_host("nonsense!!}{::"), "");
}

TEST(HttpCommonTest, DownloadFile) {
  ASSERT_TRUE(download_file("https://httpbin.org/base64/aGVsbG8h", "hello.txt"));
  ASSERT_TRUE(download_file("https://httpbin.org/redirect-to?url=/base64/aGVsbG8h", "hello-redirect.txt"));
}