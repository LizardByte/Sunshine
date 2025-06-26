/**
 * @file src/confighttp.h
 * @brief Declarations for the Web UI Config HTTP server.
 */
#pragma once

// standard includes
#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

// local includes
#include "http_auth.h"
#include "thread_safe.h"

#include <Simple-Web-Server/server_https.hpp>

#define WEB_DIR SUNSHINE_ASSETS_DIR "/web/"

namespace confighttp {
  constexpr auto PORT_HTTPS = 1;
  void start();

  enum class TokenScope {
    Read,
    Write
  };

  // Authentication functions
  AuthResult check_auth(const req_https_t &request);
  bool authenticate(resp_https_t response, req_https_t request);
  void save_api_tokens();
  void load_api_tokens();
  using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
  using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;
  TokenScope scope_from_string(std::string_view s);
  std::string scope_to_string(TokenScope scope);

  // Web UI endpoints
  void listApiTokens(resp_https_t response, req_https_t request);
  void revokeApiToken(resp_https_t response, req_https_t request);
  void getTokenPage(resp_https_t response, req_https_t request);
  void loginUser(resp_https_t response, req_https_t request);
  void logoutUser(resp_https_t response, req_https_t request);
  void getLoginPage(resp_https_t response, req_https_t request);

  // Session token management
  std::string generate_session_token(const std::string &username);
  bool validate_session_token(const std::string &token);
  void revoke_session_token(const std::string &token);
  void cleanup_expired_session_tokens();

}  // namespace confighttp

// mime types map
const std::map<std::string, std::string> mime_types = {
  {"css", "text/css"},
  {"gif", "image/gif"},
  {"htm", "text/html"},
  {"html", "text/html"},
  {"ico", "image/x-icon"},
  {"jpeg", "image/jpeg"},
  {"jpg", "image/jpeg"},
  {"js", "application/javascript"},
  {"json", "application/json"},
  {"png", "image/png"},
  {"svg", "image/svg+xml"},
  {"ttf", "font/ttf"},
  {"txt", "text/plain"},
  {"woff2", "font/woff2"},
  {"xml", "text/xml"},
};
