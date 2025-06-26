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

  // API Token scopes
  /**
   * @enum TokenScope
   * @brief Scopes for API tokens.
   */
  enum class TokenScope {
    /** @brief Read-only access. */
    Read,
    /** @brief Read and write access. */
    Write
    // Add more scopes as needed
  };

  // Persistence helpers
  void save_api_tokens();
  void load_api_tokens();
  
  // HTTPS server types
  using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
  using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;

  /**
   * @brief Result of authentication check for validation.
   */
  struct AuthResult {
    bool ok;
    SimpleWeb::StatusCode code;
    std::string body;
    SimpleWeb::CaseInsensitiveMultimap headers;
  };

  // Utility for scope string conversion
  TokenScope scope_from_string(std::string_view s);
  std::string scope_to_string(TokenScope scope);
  
  // Token management endpoints
  void listApiTokens(resp_https_t response, req_https_t request);
  void revokeApiToken(resp_https_t response, req_https_t request);
  void getTokenPage(resp_https_t response, req_https_t request);
  
  // Session token endpoints
  void loginUser(resp_https_t response, req_https_t request);
  void logoutUser(resp_https_t response, req_https_t request);
  void getLoginPage(resp_https_t response, req_https_t request);
  
  // Authentication functions (exposed for validation)
  bool authenticate_basic(const std::string_view rawAuth);
  AuthResult make_auth_error(SimpleWeb::StatusCode code, const std::string &error, bool add_www_auth = false, const std::string &location = {});
  AuthResult check_bearer_auth(const std::string &rawAuth, const std::string &path, const std::string &method);
  AuthResult check_basic_auth(const std::string &rawAuth);
  AuthResult check_session_auth(const std::string &rawAuth);
  bool is_html_request(const std::string &path);
  AuthResult check_auth(const std::string &remote_address, const std::string &auth_header, 
                       const std::string &path, const std::string &method);
  AuthResult check_auth(const req_https_t &request);
  bool authenticate(resp_https_t response, req_https_t request);
  
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
  {"woff2", "font/woff2"},  {"xml", "text/xml"},
};
