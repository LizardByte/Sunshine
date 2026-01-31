/**
 * @file src/confighttp.h
 * @brief Declarations for the Web UI Config HTTP server.
 */
#pragma once

// standard includes
#include <memory>
#include <string>

// lib includes
#include <nlohmann/json.hpp>
#include <Simple-Web-Server/server_https.hpp>

// local includes
#include "thread_safe.h"

#define WEB_DIR SUNSHINE_ASSETS_DIR "/web/"

namespace confighttp {
  constexpr auto PORT_HTTPS = 1;

  // Type aliases for HTTPS server components
  using https_server_t = SimpleWeb::Server<SimpleWeb::HTTPS>;
  using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
  using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;

  // Main server start function
  void start();

  void print_req(const req_https_t &request);
  void send_response(const resp_https_t &response, const nlohmann::json &output_tree);
  void send_unauthorized(const resp_https_t &response, const req_https_t &request);
  void send_redirect(const resp_https_t &response, const req_https_t &request, const char *path);
  bool authenticate(const resp_https_t &response, const req_https_t &request);
  void not_found(const resp_https_t &response, const req_https_t &request, const std::string &error_message = "Not Found");
  void bad_request(const resp_https_t &response, const req_https_t &request, const std::string &error_message = "Bad Request");
  bool check_content_type(const resp_https_t &response, const req_https_t &request, const std::string_view &contentType);
  bool check_request_body_empty(const resp_https_t &response, const req_https_t &request);
  bool check_app_index(const resp_https_t &response, const req_https_t &request, int index);
  void getPage(const resp_https_t &response, const req_https_t &request, const char *html_file, bool require_auth = true, bool redirect_if_username = false);
  void getAsset(const resp_https_t &response, const req_https_t &request);
  void getLocale(const resp_https_t &response, const req_https_t &request);
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
