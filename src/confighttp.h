/**
 * @file src/confighttp.h
 * @brief Declarations for the Web UI Config HTTP server.
 */
#pragma once

// standard includes
#include <string>
#include <unordered_map>
#include <set>
#include <chrono>
#include <map>

// local includes
#include "thread_safe.h"
#include <Simple-Web-Server/server_https.hpp>

#define WEB_DIR SUNSHINE_ASSETS_DIR "/web/"

namespace confighttp {
  constexpr auto PORT_HTTPS = 1;
  void start();

  // API Token scopes
  enum class TokenScope {
    Read,
    Write
    // Add more scopes as needed
  };

  struct ApiTokenInfo {
    std::string token_hash; // SHA-256 hex
    std::map<std::string, std::set<std::string>> path_methods; // path -> allowed HTTP methods
    std::string username;
    std::chrono::system_clock::time_point created_at;
  };

  // In-memory token store (hash -> info)
  extern std::unordered_map<std::string, ApiTokenInfo> api_tokens;

  // Persistence helpers
  void save_api_tokens();
  void load_api_tokens();

  // HTTPS server types
  using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
  using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;

  // Utility for scope string conversion
  TokenScope scope_from_string(const std::string& s);
  std::string scope_to_string(TokenScope scope);

  // Token management endpoints
  void listApiTokens(resp_https_t response, req_https_t request);
  void revokeApiToken(resp_https_t response, req_https_t request);
  void getTokenPage(resp_https_t response, req_https_t request);
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
