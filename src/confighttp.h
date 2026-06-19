/**
 * @file src/confighttp.h
 * @brief Declarations for the Web UI Config HTTP server.
 */
#pragma once

// standard includes
#include <filesystem>
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
  std::string generate_csrf_token(const std::string &client_id);
  bool validate_csrf_token(const resp_https_t &response, const req_https_t &request, const std::string &client_id);
  std::string get_client_id(const req_https_t &request);
  bool check_app_index(const resp_https_t &response, const req_https_t &request, int index);
  void getPage(const resp_https_t &response, const req_https_t &request, const char *html_file, bool require_auth = true, bool redirect_if_username = false);
  void getAsset(const resp_https_t &response, const req_https_t &request);
  void browseDirectory(const resp_https_t &response, const req_https_t &request);
  void getLocale(const resp_https_t &response, const req_https_t &request);
  void getCSRFToken(const resp_https_t &response, const req_https_t &request);

  // Browse helper functions (also exposed for unit testing)
  /**
   * @brief Checks whether a directory entry qualifies as an executable file.
   * @param entry The directory entry to check.
   * @param status The cached file status for the entry.
   * @return True if the file should be included in an executable-type listing.
   */
  bool is_browsable_executable(const std::filesystem::directory_entry &entry, const std::filesystem::file_status &status);

  /**
   * @brief Lists, filters, and sorts the entries of a directory for the browse API.
   * @param dir_path The directory to list.
   * @param type_str Filter type: "directory", "executable", "file", or "any".
   * @return Sorted JSON array of entry objects with name/type/path fields.
   */
  nlohmann::json build_browse_entries(const std::filesystem::path &dir_path, const std::string &type_str);

#ifdef _WIN32
  /**
   * @brief Builds a JSON array of available Windows drive letters.
   * @return JSON array of drive-letter entries.
   */
  nlohmann::json get_windows_drives();
#endif
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
