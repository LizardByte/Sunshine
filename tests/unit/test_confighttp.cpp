/**
 * @file tests/unit/test_confighttp.cpp
 * @brief Test src/confighttp.cpp
 *
 * These tests use a real HTTPS client/server to test the actual confighttp endpoints.
 * While this is more of an integration test approach, it's the most practical way to
 * verify that the confighttp functions work correctly end-to-end.
 */

// test imports
#include "../tests_common.h"

// standard includes
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <thread>

// lib imports
#include <Simple-Web-Server/client_https.hpp>
#include <Simple-Web-Server/crypto.hpp>
#include <Simple-Web-Server/server_https.hpp>

// local imports
#include <src/config.h>
#include <src/confighttp.h>
#include <src/crypto.h>
#include <src/httpcommon.h>
#include <src/network.h>
#include <src/utility.h>

using namespace std::literals;

namespace {
  // Test certificates
  const std::string TEST_PRIVATE_KEY = R"(-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDLePNlWN06FLlM
ujWzIX8UICO7SWfH5DXlafVjpxwi/WCkdO6FxixqRNGu71wMvJXFbDlNR8fqX2xo
+eq17J3uFKn+qdjmP3L38bkqxhoJ/nCrXkeGyCTQ+Daug63ZYSJeW2Mmf+LAR5/i
/fWYfXpSlbcf5XJQPEWvENpLqWu+NOU50dJXIEVYpUXRx2+x4ZbwkH7tVJm94L+C
OUyiJKQPyWgU2aFsyJGwHFfePfSUpfYHqbHZV/ILpY59VJairBwE99bx/mBvMI7a
hBmJTSDuDffJcPDhFF5kZa0UkQPrPvhXcQaSRti7v0VonEQj8pTSnGYr9ktWKk92
wxDyn9S3AgMBAAECggEAbEhQ14WELg2rUz7hpxPTaiV0fo4hEcrMN+u8sKzVF3Xa
QYsNCNoe9urq3/r39LtDxU3D7PGfXYYszmz50Jk8ruAGW8WN7XKkv3i/fxjv8JOc
6EYDMKJAnYkKqLLhCQddX/Oof2udg5BacVWPpvhX6a1NSEc2H6cDupfwZEWkVhMi
bCC3JcNmjFa8N7ow1/5VQiYVTjpxfV7GY1GRe7vMvBucdQKH3tUG5PYXKXytXw/j
KDLaECiYVT89KbApkI0zhy7I5g3LRq0Rs5fmYLCjVebbuAL1W5CJHFJeFOgMKvnO
QSl7MfHkTnzTzUqwkwXjgNMGsTosV4UloL9gXVF6GQKBgQD5fI771WETkpaKjWBe
6XUVSS98IOAPbTGpb8CIhSjzCuztNAJ+0ey1zklQHonMFbdmcWTkTJoF3ECqAos9
vxB4ROg+TdqGDcRrXa7Twtmhv66QvYxttkaK3CqoLX8CCTnjgXBCijo6sCpo6H1T
+y55bBDpxZjNFT5BV3+YPBfWQwKBgQDQyNt+saTqJqxGYV7zWQtOqKORRHAjaJpy
m5035pky5wORsaxQY8HxbsTIQp9jBSw3SQHLHN/NAXDl2k7VAw/axMc+lj9eW+3z
2Hv5LVgj37jnJYEpYwehvtR0B4jZnXLyLwShoBdRPkGlC5fs9+oWjQZoDwMLZfTg
eZVOJm6SfQKBgQDfxYcB/kuKIKsCLvhHaSJpKzF6JoqRi6FFlkScrsMh66TCxSmP
0n58O0Cqqhlyge/z5LVXyBVGOF2Pn6SAh4UgOr4MVAwyvNp2aprKuTQ2zhSnIjx4
k0sGdZ+VJOmMS/YuRwUHya+cwDHp0s3Gq77tja5F38PD/s/OD8sUIqJGvQKBgBfI
6ghy4GC0ayfRa+m5GSqq14dzDntaLU4lIDIAGS/NVYDBhunZk3yXq99Mh6/WJQVf
Uc77yRsnsN7ekeB+as33YONmZm2vd1oyLV1jpwjfMcdTZHV8jKAGh1l4ikSQRUoF
xTdMb5uXxg6xVWtvisFq63HrU+N2iAESmMnAYxRZAoGAVEFJRRjPrSIUTCCKRiTE
br+cHqy6S5iYRxGl9riKySBKeU16fqUACIvUqmqlx4Secj3/Hn/VzYEzkxcSPwGi
qMgdS0R+tacca7NopUYaaluneKYdS++DNlT/m+KVHqLynQr54z1qBlThg9KGrpmM
LGZkXtQpx6sX7v3Kq56PkNk=
-----END PRIVATE KEY-----)";

  const std::string TEST_PUBLIC_CERT = R"(-----BEGIN CERTIFICATE-----
MIIC6zCCAdOgAwIBAgIBATANBgkqhkiG9w0BAQsFADA5MQswCQYDVQQGEwJJVDEW
MBQGA1UECgwNR2FtZXNPbldoYWxlczESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTIy
MDQwOTA5MTYwNVoXDTQyMDQwNDA5MTYwNVowOTELMAkGA1UEBhMCSVQxFjAUBgNV
BAoMDUdhbWVzT25XaGFsZXMxEjAQBgNVBAMMCWxvY2FsaG9zdDCCASIwDQYJKoZI
hvcNAQEBBQADggEPADCCAQoCggEBAMt482VY3ToUuUy6NbMhfxQgI7tJZ8fkNeVp
9WOnHCL9YKR07oXGLGpE0a7vXAy8lcVsOU1Hx+pfbGj56rXsne4Uqf6p2OY/cvfx
uSrGGgn+cKteR4bIJND4Nq6DrdlhIl5bYyZ/4sBHn+L99Zh9elKVtx/lclA8Ra8Q
2kupa7405TnR0lcgRVilRdHHb7HhlvCQfu1Umb3gv4I5TKIkpA/JaBTZoWzIkbAc
V9499JSl9gepsdlX8guljn1UlqKsHAT31vH+YG8wjtqEGYlNIO4N98lw8OEUXmRl
rRSRA+s++FdxBpJG2Lu/RWicRCPylNKcZiv2S1YqT3bDEPKf1LcCAwEAATANBgkq
hkiG9w0BAQsFAAOCAQEAqPBqzvDjl89pZMll3Ge8RS7HeDuzgocrhOcT2jnk4ag7
/TROZuISjDp6+SnL3gPEt7E2OcFAczTg3l/wbT5PFb6vM96saLm4EP0zmLfK1FnM
JDRahKutP9rx6RO5OHqsUB+b4jA4W0L9UnXUoLKbjig501AUix0p52FBxu+HJ90r
HlLs3Vo6nj4Z/PZXrzaz8dtQ/KJMpd/g/9xlo6BKAnRk5SI8KLhO4hW6zG0QA56j
X4wnh1bwdiidqpcgyuKossLOPxbS786WmsesaAWPnpoY6M8aija+ALwNNuWWmyMg
9SVDV76xJzM36Uq7Kg3QJYTlY04WmPIdJHkCtXWf9g==
-----END CERTIFICATE-----)";
}  // namespace

/**
 * @brief Test fixture that sets up a minimal HTTPS server with confighttp-style routes
 *
 * This fixture creates a real server to test the actual confighttp functions.
 */
class ConfigHttpTest: public ::testing::Test {
protected:
  std::unique_ptr<SimpleWeb::Server<SimpleWeb::HTTPS>> server;
  std::unique_ptr<SimpleWeb::Client<SimpleWeb::HTTPS>> client;
  std::thread server_thread;
  unsigned short port = 0;

  std::string saved_username;
  std::string saved_password;
  std::string saved_salt;
  std::string saved_locale;
  std::filesystem::path test_web_dir;
  std::filesystem::path cert_file;
  std::filesystem::path key_file;
  std::filesystem::path web_dir_test_file;

  void SetUp() override {
    // Save current config
    saved_username = config::sunshine.username;
    saved_password = config::sunshine.password;
    saved_salt = config::sunshine.salt;
    saved_locale = config::sunshine.locale;

    // Set up test credentials
    config::sunshine.username = "testuser";
    config::sunshine.salt = "testsalt";
    config::sunshine.password = util::hex(crypto::hash("testpass" + config::sunshine.salt)).to_string();

    // Set test locale
    config::sunshine.locale = "en";

    // Create test web directory
    test_web_dir = std::filesystem::temp_directory_path() / "sunshine_test_confighttp";
    std::filesystem::create_directories(test_web_dir / "web");

    // Create test HTML file in the actual WEB_DIR for getPage to read
    // Note: WEB_DIR might not exist yet, so create it
    try {
      std::filesystem::path web_dir_path(WEB_DIR);
      if (!std::filesystem::exists(web_dir_path)) {
        std::filesystem::create_directories(web_dir_path);
      }
      web_dir_test_file = web_dir_path / "test_page.html";

      std::ofstream test_html(web_dir_test_file);
      if (test_html.is_open()) {
        test_html << "<html><head><title>Test Page</title></head><body><h1>Test Page Content</h1></body></html>";
        test_html.close();
      }
    } catch (const std::exception &e) {
      // If we can't create the file, getPage tests will be skipped
      // Log but don't fail setup
      std::cerr << "Warning: Could not create test file in WEB_DIR: " << e.what() << std::endl;
    }

    // Write certificates to temp files (Simple-Web-Server expects file paths)
    cert_file = test_web_dir / "test_cert.pem";
    key_file = test_web_dir / "test_key.pem";

    std::ofstream cert_out(cert_file);
    cert_out << TEST_PUBLIC_CERT;
    cert_out.close();

    std::ofstream key_out(key_file);
    key_out << TEST_PRIVATE_KEY;
    key_out.close();

    // Set up server
    server = std::make_unique<SimpleWeb::Server<SimpleWeb::HTTPS>>(cert_file.string(), key_file.string());
    server->config.port = 0;  // OS assigns port
    server->config.reuse_address = true;
    server->config.timeout_request = 5;
    server->config.timeout_content = 300;

    // Add a route to test authentication directly
    server->resource["^/auth-test$"]["GET"] = [](
                                                const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response> &response,
                                                const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request
                                              ) {
      // Call the actual confighttp::authenticate function
      const bool authenticated = confighttp::authenticate(response, request);

      if (authenticated) {
        SimpleWeb::CaseInsensitiveMultimap headers;
        headers.emplace("Content-Type", "text/plain");
        response->write("authenticated", headers);
      }
      // If not authenticated, authenticate() already sent the response
    };

    // Add a route to test send_unauthorized
    server->resource["^/unauthorized-test$"]["GET"] = [](
                                                        const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response> &response,
                                                        const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request
                                                      ) {
      // Call the actual confighttp::send_unauthorized function
      confighttp::send_unauthorized(response, request);
    };

    // Add a route to test not_found
    server->resource["^/notfound-test$"]["GET"] = [](
                                                    const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response> &response,
                                                    const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request
                                                  ) {
      // Call the actual confighttp::not_found function
      confighttp::not_found(response, request, "Test not found");
    };

    // Add a route to test bad_request
    server->resource["^/badrequest-test$"]["GET"] = [](
                                                      const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response> &response,
                                                      const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request
                                                    ) {
      // Call the actual confighttp::bad_request function
      confighttp::bad_request(response, request, "Test bad request");
    };

    // Add a route to test send_response with JSON
    server->resource["^/json-test$"]["GET"] = [](
                                                const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response> &response,
                                                [[maybe_unused]] const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request
                                              ) {
      // Call the actual confighttp::send_response function
      nlohmann::json test_json;
      test_json["status"] = "success";
      test_json["message"] = "Test JSON response";
      test_json["code"] = 200;
      confighttp::send_response(response, test_json);
    };

    // Add a route to test send_redirect
    server->resource["^/redirect-test$"]["GET"] = [](
                                                    const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response> &response,
                                                    const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request
                                                  ) {
      // Call the actual confighttp::send_redirect function
      confighttp::send_redirect(response, request, "/redirected-location");
    };

    // Add a route to test check_content_type
    server->resource["^/content-type-test$"]["POST"] = [](
                                                         const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response> &response,
                                                         const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request
                                                       ) {
      // Call the actual confighttp::check_content_type function
      if (confighttp::check_content_type(response, request, "application/json")) {
        SimpleWeb::CaseInsensitiveMultimap headers;
        headers.emplace("Content-Type", "text/plain");
        response->write("content-type-valid", headers);
      }
      // If check fails, check_content_type already sent an error response
    };

    // Add a route to test check_request_body_empty
    server->resource["^/empty-body-test$"]["POST"] = [](
                                                       const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response> &response,
                                                       const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request
                                                     ) {
      // Call the actual confighttp::check_request_body_empty function
      if (confighttp::check_request_body_empty(response, request)) {
        SimpleWeb::CaseInsensitiveMultimap headers;
        headers.emplace("Content-Type", "text/plain");
        response->write("body-is-empty", headers);
      }
      // If check fails, check_request_body_empty already sent an error response
    };

    // Add a route to test getPage (requires auth)
    server->resource["^/page-test$"]["GET"] = [this](
                                                const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response> &response,
                                                const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request
                                              ) {
      // Call the actual confighttp::getPage function
      // Note: This will read from WEB_DIR, so we need to ensure the file exists there
      confighttp::getPage(response, request, "test_page.html", true, false);
    };

    // Add a route to test getPage without auth requirement
    server->resource["^/page-noauth-test$"]["GET"] = [this](
                                                       const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response> &response,
                                                       const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request
                                                     ) {
      confighttp::getPage(response, request, "test_page.html", false, false);
    };

    // Add a route to test getPage with redirect_if_username
    server->resource["^/page-redirect-test$"]["GET"] = [this](
                                                         const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response> &response,
                                                         const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request
                                                       ) {
      confighttp::getPage(response, request, "test_page.html", false, true);
    };

    // Add a route to test getLocale
    server->resource["^/locale-test$"]["GET"] = [](
                                                  const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response> &response,
                                                  const std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request> &request
                                                ) {
      // Call the actual confighttp::getLocale function
      confighttp::getLocale(response, request);
    };

    // Start server
    server_thread = std::thread([this]() {
      server->start([this](unsigned short assigned_port) {
        port = assigned_port;
      });
    });

    // Wait for port assignment
    for (int i = 0; i < 100 && port == 0; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_NE(port, 0) << "Server failed to start";

    // Set up client
    client = std::make_unique<SimpleWeb::Client<SimpleWeb::HTTPS>>(std::format("localhost:{}", port), false);
    client->config.timeout = 5;
  }

  void TearDown() override {
    if (server) {
      server->stop();
    }
    if (server_thread.joinable()) {
      server_thread.join();
    }

    config::sunshine.username = saved_username;
    config::sunshine.password = saved_password;
    config::sunshine.salt = saved_salt;
    config::sunshine.locale = saved_locale;

    // Clean up test HTML file from WEB_DIR
    if (std::filesystem::exists(web_dir_test_file)) {
      std::filesystem::remove(web_dir_test_file);
    }

    if (std::filesystem::exists(test_web_dir)) {
      std::filesystem::remove_all(test_web_dir);
    }
  }

  static std::string create_auth_header(const std::string &username, const std::string &password) {
    return "Basic " + SimpleWeb::Crypto::Base64::encode(username + ":" + password);
  }
};

// Test: confighttp::authenticate() rejects requests without auth header
TEST_F(ConfigHttpTest, AuthenticateRejectsNoAuth) {
  const auto response = client->request("GET", "/auth-test");
  ASSERT_EQ(response->status_code, "401 Unauthorized");

  // Check for WWW-Authenticate header
  const auto www_auth = response->header.find("WWW-Authenticate");
  ASSERT_NE(www_auth, response->header.end());
}

// Test: confighttp::authenticate() accepts valid credentials
TEST_F(ConfigHttpTest, AuthenticateAcceptsValidCredentials) {
  SimpleWeb::CaseInsensitiveMultimap headers;
  headers.emplace("Authorization", create_auth_header("testuser", "testpass"));

  const auto response = client->request("GET", "/auth-test", "", headers);
  ASSERT_EQ(response->status_code, "200 OK");

  const std::string body = response->content.string();
  ASSERT_EQ(body, "authenticated");
}

// Test: confighttp::authenticate() rejects invalid password
TEST_F(ConfigHttpTest, AuthenticateRejectsInvalidPassword) {
  SimpleWeb::CaseInsensitiveMultimap headers;
  headers.emplace("Authorization", create_auth_header("testuser", "wrongpass"));

  const auto response = client->request("GET", "/auth-test", "", headers);
  ASSERT_EQ(response->status_code, "401 Unauthorized");
}

// Test: confighttp::authenticate() is case-insensitive for username
TEST_F(ConfigHttpTest, AuthenticateCaseInsensitiveUsername) {
  SimpleWeb::CaseInsensitiveMultimap headers;
  headers.emplace("Authorization", create_auth_header("TESTUSER", "testpass"));

  const auto response = client->request("GET", "/auth-test", "", headers);
  ASSERT_EQ(response->status_code, "200 OK");
}

// Test: confighttp::send_unauthorized() sends proper 401 response
TEST_F(ConfigHttpTest, SendUnauthorizedResponse) {
  const auto response = client->request("GET", "/unauthorized-test");
  ASSERT_EQ(response->status_code, "401 Unauthorized");

  // Check for WWW-Authenticate header
  const auto www_auth = response->header.find("WWW-Authenticate");
  ASSERT_NE(www_auth, response->header.end());
  ASSERT_TRUE(www_auth->second.find("Basic realm") != std::string::npos);

  // Check security headers
  const auto x_frame = response->header.find("X-Frame-Options");
  ASSERT_NE(x_frame, response->header.end());
  ASSERT_EQ(x_frame->second, "DENY");

  const auto csp = response->header.find("Content-Security-Policy");
  ASSERT_NE(csp, response->header.end());
  ASSERT_EQ(csp->second, "frame-ancestors 'none';");

  // Check JSON response
  const std::string body = response->content.string();
  ASSERT_TRUE(body.find("Unauthorized") != std::string::npos);
  ASSERT_TRUE(body.find("401") != std::string::npos);
}

// Test: confighttp::not_found() sends proper 404 response
TEST_F(ConfigHttpTest, NotFoundResponse) {
  const auto response = client->request("GET", "/notfound-test");
  ASSERT_EQ(response->status_code, "404 Not Found");

  // Check Content-Type
  const auto content_type = response->header.find("Content-Type");
  ASSERT_NE(content_type, response->header.end());
  ASSERT_TRUE(content_type->second.find("application/json") != std::string::npos);

  // Check security headers
  const auto x_frame = response->header.find("X-Frame-Options");
  ASSERT_NE(x_frame, response->header.end());
  ASSERT_EQ(x_frame->second, "DENY");

  // Check JSON response
  const std::string body = response->content.string();
  ASSERT_TRUE(body.find("Test not found") != std::string::npos);
  ASSERT_TRUE(body.find("404") != std::string::npos);
}

// Test: confighttp::bad_request() sends proper 400 response
TEST_F(ConfigHttpTest, BadRequestResponse) {
  const auto response = client->request("GET", "/badrequest-test");
  ASSERT_EQ(response->status_code, "400 Bad Request");

  // Check Content-Type
  const auto content_type = response->header.find("Content-Type");
  ASSERT_NE(content_type, response->header.end());
  ASSERT_TRUE(content_type->second.find("application/json") != std::string::npos);

  // Check security headers
  const auto x_frame = response->header.find("X-Frame-Options");
  ASSERT_NE(x_frame, response->header.end());
  ASSERT_EQ(x_frame->second, "DENY");

  const auto csp = response->header.find("Content-Security-Policy");
  ASSERT_NE(csp, response->header.end());
  ASSERT_EQ(csp->second, "frame-ancestors 'none';");

  // Check JSON response
  const std::string body = response->content.string();
  ASSERT_TRUE(body.find("Test bad request") != std::string::npos);
  ASSERT_TRUE(body.find("400") != std::string::npos);
}

// Test: confighttp::send_response() sends proper JSON response
TEST_F(ConfigHttpTest, SendResponseJson) {
  const auto response = client->request("GET", "/json-test");
  ASSERT_EQ(response->status_code, "200 OK");

  // Check Content-Type
  const auto content_type = response->header.find("Content-Type");
  ASSERT_NE(content_type, response->header.end());
  ASSERT_TRUE(content_type->second.find("application/json") != std::string::npos);

  // Check security headers
  const auto x_frame = response->header.find("X-Frame-Options");
  ASSERT_NE(x_frame, response->header.end());
  ASSERT_EQ(x_frame->second, "DENY");

  const auto csp = response->header.find("Content-Security-Policy");
  ASSERT_NE(csp, response->header.end());
  ASSERT_EQ(csp->second, "frame-ancestors 'none';");

  // Check JSON content
  const std::string body = response->content.string();
  ASSERT_TRUE(body.find("\"status\":\"success\"") != std::string::npos || body.find("\"status\": \"success\"") != std::string::npos);
  ASSERT_TRUE(body.find("Test JSON response") != std::string::npos);
  ASSERT_TRUE(body.find("200") != std::string::npos);
}

// Test: confighttp::send_redirect() sends proper redirect response
TEST_F(ConfigHttpTest, SendRedirectResponse) {
  const auto response = client->request("GET", "/redirect-test");
  ASSERT_EQ(response->status_code, "307 Temporary Redirect");

  // Check Location header
  const auto location = response->header.find("Location");
  ASSERT_NE(location, response->header.end());
  ASSERT_EQ(location->second, "/redirected-location");

  // Check security headers
  const auto x_frame = response->header.find("X-Frame-Options");
  ASSERT_NE(x_frame, response->header.end());
  ASSERT_EQ(x_frame->second, "DENY");

  const auto csp = response->header.find("Content-Security-Policy");
  ASSERT_NE(csp, response->header.end());
  ASSERT_EQ(csp->second, "frame-ancestors 'none';");
}

// Test: confighttp::check_content_type() accepts valid content type
TEST_F(ConfigHttpTest, CheckContentTypeValid) {
  SimpleWeb::CaseInsensitiveMultimap headers;
  headers.emplace("Content-Type", "application/json");

  const auto response = client->request("POST", "/content-type-test", "", headers);
  ASSERT_EQ(response->status_code, "200 OK");

  const std::string body = response->content.string();
  ASSERT_EQ(body, "content-type-valid");
}

// Test: confighttp::check_content_type() rejects missing content type
TEST_F(ConfigHttpTest, CheckContentTypeMissing) {
  const auto response = client->request("POST", "/content-type-test");
  ASSERT_EQ(response->status_code, "400 Bad Request");

  const std::string body = response->content.string();
  ASSERT_TRUE(body.find("Content type not provided") != std::string::npos);
}

// Test: confighttp::check_content_type() rejects wrong content type
TEST_F(ConfigHttpTest, CheckContentTypeWrong) {
  SimpleWeb::CaseInsensitiveMultimap headers;
  headers.emplace("Content-Type", "text/plain");

  const auto response = client->request("POST", "/content-type-test", "", headers);
  ASSERT_EQ(response->status_code, "400 Bad Request");

  const std::string body = response->content.string();
  ASSERT_TRUE(body.find("Content type mismatch") != std::string::npos);
}

// Test: confighttp::check_content_type() handles content type with charset
TEST_F(ConfigHttpTest, CheckContentTypeWithCharset) {
  SimpleWeb::CaseInsensitiveMultimap headers;
  headers.emplace("Content-Type", "application/json; charset=utf-8");

  const auto response = client->request("POST", "/content-type-test", "", headers);
  ASSERT_EQ(response->status_code, "200 OK");

  const std::string body = response->content.string();
  ASSERT_EQ(body, "content-type-valid");
}

// Test: confighttp::check_request_body_empty() accepts empty body
TEST_F(ConfigHttpTest, CheckRequestBodyEmpty) {
  const auto response = client->request("POST", "/empty-body-test");
  ASSERT_EQ(response->status_code, "200 OK");

  const std::string body = response->content.string();
  ASSERT_EQ(body, "body-is-empty");
}

// Test: confighttp::check_request_body_empty() rejects non-empty body
TEST_F(ConfigHttpTest, CheckRequestBodyNotEmpty) {
  const auto response = client->request("POST", "/empty-body-test", "some data");
  ASSERT_EQ(response->status_code, "400 Bad Request");

  const std::string body = response->content.string();
  ASSERT_TRUE(body.find("Request body must be empty") != std::string::npos);
}

// Test: confighttp::getPage() serves HTML with authentication
TEST_F(ConfigHttpTest, GetPageWithAuth) {
  SimpleWeb::CaseInsensitiveMultimap headers;
  headers.emplace("Authorization", create_auth_header("testuser", "testpass"));

  const auto response = client->request("GET", "/page-test", "", headers);
  ASSERT_EQ(response->status_code, "200 OK");

  // Check Content-Type
  const auto content_type = response->header.find("Content-Type");
  ASSERT_NE(content_type, response->header.end());
  ASSERT_TRUE(content_type->second.find("text/html") != std::string::npos);
  ASSERT_TRUE(content_type->second.find("charset=utf-8") != std::string::npos);

  // Check security headers
  const auto x_frame = response->header.find("X-Frame-Options");
  ASSERT_NE(x_frame, response->header.end());
  ASSERT_EQ(x_frame->second, "DENY");

  const auto csp = response->header.find("Content-Security-Policy");
  ASSERT_NE(csp, response->header.end());
  ASSERT_EQ(csp->second, "frame-ancestors 'none';");

  // Check HTML content
  const std::string body = response->content.string();
  ASSERT_TRUE(body.find("<html>") != std::string::npos);
  ASSERT_TRUE(body.find("Test Page Content") != std::string::npos);
  ASSERT_TRUE(body.find("</html>") != std::string::npos);
}

// Test: confighttp::getPage() requires authentication when require_auth=true
TEST_F(ConfigHttpTest, GetPageRequiresAuth) {
  const auto response = client->request("GET", "/page-test");
  ASSERT_EQ(response->status_code, "401 Unauthorized");

  // Should have WWW-Authenticate header since auth is required
  const auto www_auth = response->header.find("WWW-Authenticate");
  ASSERT_NE(www_auth, response->header.end());
}

// Test: confighttp::getPage() works without authentication when require_auth=false
TEST_F(ConfigHttpTest, GetPageWithoutAuthRequired) {
  const auto response = client->request("GET", "/page-noauth-test");
  ASSERT_EQ(response->status_code, "200 OK");

  // Check HTML content is served
  const std::string body = response->content.string();
  ASSERT_TRUE(body.find("Test Page Content") != std::string::npos);
}

// Test: confighttp::getPage() redirects when redirect_if_username=true and username is set
TEST_F(ConfigHttpTest, GetPageRedirectsWhenUsernameSet) {
  // Username is set in SetUp(), so redirect_if_username should trigger redirect
  const auto response = client->request("GET", "/page-redirect-test");
  ASSERT_EQ(response->status_code, "307 Temporary Redirect");

  // Check redirect location
  const auto location = response->header.find("Location");
  ASSERT_NE(location, response->header.end());
  ASSERT_EQ(location->second, "/");
}

// Test: confighttp::getPage() doesn't redirect when username is empty
TEST_F(ConfigHttpTest, GetPageNoRedirectWhenUsernameEmpty) {
  // Temporarily clear username
  const std::string saved = config::sunshine.username;
  config::sunshine.username = "";

  const auto response = client->request("GET", "/page-redirect-test");
  ASSERT_EQ(response->status_code, "200 OK");

  // Restore username
  config::sunshine.username = saved;
}

// Test: confighttp::getLocale() returns locale JSON
TEST_F(ConfigHttpTest, GetLocaleReturnsJson) {
  const auto response = client->request("GET", "/locale-test");
  ASSERT_EQ(response->status_code, "200 OK");

  // Check Content-Type
  const auto content_type = response->header.find("Content-Type");
  ASSERT_NE(content_type, response->header.end());
  ASSERT_TRUE(content_type->second.find("application/json") != std::string::npos);

  // Check security headers
  const auto x_frame = response->header.find("X-Frame-Options");
  ASSERT_NE(x_frame, response->header.end());
  ASSERT_EQ(x_frame->second, "DENY");

  const auto csp = response->header.find("Content-Security-Policy");
  ASSERT_NE(csp, response->header.end());
  ASSERT_EQ(csp->second, "frame-ancestors 'none';");

  // Check JSON content
  const std::string body = response->content.string();
  ASSERT_TRUE(body.find("\"status\":true") != std::string::npos || body.find("\"status\": true") != std::string::npos);
  ASSERT_TRUE(body.find("\"locale\":\"en\"") != std::string::npos || body.find("\"locale\": \"en\"") != std::string::npos);
}

// Test: confighttp::getLocale() rejects non-empty body
TEST_F(ConfigHttpTest, GetLocaleRejectsNonEmptyBody) {
  const auto response = client->request("GET", "/locale-test", "some data");
  ASSERT_EQ(response->status_code, "400 Bad Request");

  const std::string body = response->content.string();
  ASSERT_TRUE(body.find("Request body must be empty") != std::string::npos);
}
