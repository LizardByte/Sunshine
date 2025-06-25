/**
 * @file src/confighttp.cpp
 * @brief Definitions for the Web UI Config HTTP server.
 *
 * @todo Authentication, better handling of routes common to nvhttp, cleanup
 */
#define BOOST_BIND_GLOBAL_PLACEHOLDERS

// standard includes
#include <boost/regex.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

// lib includes
#include <boost/algorithm/string.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <nlohmann/json.hpp>
#include <Simple-Web-Server/crypto.hpp>
#include <Simple-Web-Server/server_https.hpp>

// local includes
#include "config.h"
#include "confighttp.h"
#include "crypto.h"
#include "display_device.h"
#include "file_handler.h"
#include "globals.h"
#include "http_auth.h"
#include "httpcommon.h"
#include "logging.h"
#include "network.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "process.h"
#include "utility.h"
#include "uuid.h"
#include "version.h"

using namespace std::literals;
namespace pt = boost::property_tree;

namespace confighttp {
  namespace fs = std::filesystem;
  using enum SimpleWeb::StatusCode;

  static ApiTokenManager apiTokenManager;

  using https_server_t = SimpleWeb::Server<SimpleWeb::HTTPS>;

  using args_t = SimpleWeb::CaseInsensitiveMultimap;
  using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
  using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;

  // Replace static session token storage with SessionTokenManager
  static SessionTokenManager sessionTokenManager(SessionTokenManager::make_default_dependencies());
  static constexpr std::chrono::hours SESSION_TOKEN_DURATION{24}; // for API compatibility

  enum class op_e {
    ADD,  ///< Add client
    REMOVE  ///< Remove client
  };

  /**
   * @brief Log the request details.
   * @param request The HTTP request object.
   */
  void print_req(const req_https_t &request) {
    BOOST_LOG(debug) << "METHOD :: "sv << request->method;
    BOOST_LOG(debug) << "DESTINATION :: "sv << request->path;

    for (auto &[name, val] : request->header) {
      BOOST_LOG(debug) << name << " -- " << (name == "Authorization" ? "CREDENTIALS REDACTED" : val);
    }

    BOOST_LOG(debug) << " [--] "sv;

    for (auto &[name, val] : request->parse_query_string()) {
      BOOST_LOG(debug) << name << " -- " << val;
    }

    BOOST_LOG(debug) << " [--] "sv;
  }

  /**
   * @brief Send a response.
   * @param response The HTTP response object.
   * @param output_tree The JSON tree to send.
   */
  void send_response(resp_https_t response, const nlohmann::json &output_tree) {
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");

    response->write(output_tree.dump(), headers);
  }

  /**
   * @brief Send a 401 Unauthorized response.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void send_unauthorized(resp_https_t response, req_https_t request) {
    auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
    BOOST_LOG(info) << "Web UI: ["sv << address << "] -- not authorized"sv;

    constexpr auto code = client_error_unauthorized;

    nlohmann::json tree;
    tree["status_code"] = code;
    tree["status"] = false;
    tree["error"] = "Unauthorized";

    const SimpleWeb::CaseInsensitiveMultimap headers {
      {"Content-Type", "application/json"},
      {"WWW-Authenticate", R"(Basic realm="Sunshine Gamestream Host", charset="UTF-8")"}
    };

    response->write(code, tree.dump(), headers);
  }

  /**
   * @brief Send a redirect response.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @param path The path to redirect to.
   */
  void send_redirect(resp_https_t response, req_https_t request, const char *path) {
    auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
    BOOST_LOG(info) << "Web UI: ["sv << address << "] -- not authorized"sv;
    const SimpleWeb::CaseInsensitiveMultimap headers {
      {"Location", path}
    };
    response->write(redirection_temporary_redirect, headers);
  }

  /**
   * @brief Authenticates a user using HTTP Basic Authentication.
   *
   * This function decodes the provided Base64-encoded HTTP Basic Auth string,
   * extracts the username and password, hashes the password with a configured salt,
   * and compares the credentials against the configured username and password hash.
   *
   * @param rawAuth The raw "Authorization" header value (expected to start with "Basic ").
   * @return true if authentication succeeds, false otherwise.
   */
  bool authenticate_basic(const std::string_view rawAuth) {
    auto base64 = std::string(rawAuth.substr(6));
    auto authData = SimpleWeb::Crypto::Base64::decode(base64);
    std::string::size_type index = authData.find(':');
    if (index == std::string::npos || index >= authData.size() - 1) {
      return false;
    }
    auto username = authData.substr(0, index);
    auto password = authData.substr(index + 1);
    if (auto hash = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();
        !boost::iequals(username, config::sunshine.username) || hash != config::sunshine.password) {
      return false;
    }
    return true;
  }

  /**
   * @brief Helper to build an AuthResult for error responses.
   */
  AuthResult make_auth_error(SimpleWeb::StatusCode code, const std::string &error, bool add_www_auth, const std::string &location) {
    AuthResult result {false, code, {}, {}};
    if (!location.empty()) {
      result.headers.emplace("Location", location);
      return result;
    }
    nlohmann::json tree;
    tree["status_code"] = code;
    tree["status"] = false;
    tree["error"] = error;
    result.body = tree.dump();
    result.headers.emplace("Content-Type", "application/json");
    if (add_www_auth) {
      result.headers.emplace("WWW-Authenticate", R"(Basic realm=\"Sunshine Gamestream Host\", charset=\"UTF-8\")");
    }
    return result;
  }
  
  /**
   * @brief Helper to check Bearer authentication.
   * @param rawAuth The raw authorization header value.
   * @param path The requested path.
   * @param method The HTTP method.
   * @return AuthResult with outcome and response details if not authorized.
   */

  AuthResult check_bearer_auth(const std::string &rawAuth, const std::string &path, const std::string &method) {
    if (!apiTokenManager.authenticate_bearer(rawAuth, path, method)) {
      return make_auth_error(client_error_forbidden, "Forbidden: Token does not have permission for this path/method.");
    }
    return {true, success_ok, {}, {}};
  }

  /**
   * @brief Helper to check Basic authentication.
   * @param rawAuth The raw authorization header value.
   * @return AuthResult with outcome and response details if not authorized.
   */
  AuthResult check_basic_auth(const std::string &rawAuth) {
    if (!authenticate_basic(rawAuth)) {
      return make_auth_error(client_error_unauthorized, "Unauthorized", true);
    }
    return {true, success_ok, {}, {}};
  }
  
  /**
   * @brief Check authentication and authorization with raw parameters.
   * @param remote_address The normalized remote address string.
   * @param auth_header The authorization header value (empty if not present).
   * @param path The requested path.
   * @param method The HTTP method.
   * @return AuthResult with outcome and response details if not authorized.
   */

  AuthResult check_auth(const std::string &remote_address, const std::string &auth_header, const std::string &path, const std::string &method) {
    if (auto ip_type = net::from_address(remote_address); ip_type > http::origin_web_ui_allowed) {
      BOOST_LOG(info) << "Web UI: ["sv << remote_address << "] -- denied"sv;
      return make_auth_error(client_error_forbidden, "Forbidden");
    }

    if (config::sunshine.username.empty()) {
      return make_auth_error(redirection_temporary_redirect, {}, false, "/welcome");
    }

    // For login page, don't require authentication
    if (path == "/login" || path == "/login/") {
      return {true, success_ok, {}, {}};
    }

    if (auth_header.empty()) {
      // For HTML page requests, redirect to login instead of showing 401
      if (is_html_request(path)) {
        std::string login_url = "/login?redirect=" + path;
        return make_auth_error(redirection_temporary_redirect, {}, false, login_url);
      }
      // For API requests, send 401 with WWW-Authenticate header for basic auth
      return make_auth_error(client_error_unauthorized, "Unauthorized", true);
    }

    if (auth_header.rfind("Bearer ", 0) == 0) {
      return check_bearer_auth(auth_header, path, method);
    }

    if (auth_header.rfind("Basic ", 0) == 0) {
      return check_basic_auth(auth_header);
    }

    if (auth_header.rfind("Session ", 0) == 0) {
      return check_session_auth(auth_header);
    }

    // For HTML page requests, redirect to login instead of showing 401
    if (is_html_request(path)) {
      std::string login_url = "/login?redirect=" + path;
      return make_auth_error(redirection_temporary_redirect, {}, false, login_url);
    }

    return make_auth_error(client_error_unauthorized, "Unauthorized", true);
  }

  /**
   * @brief Extract session token from Cookie header if present.
   * @param headers The HTTP headers map.
   * @return Session token string if found, empty string otherwise.
   */
  std::string extract_session_token_from_cookie(const SimpleWeb::CaseInsensitiveMultimap &headers) {
    auto cookie_it = headers.find("Cookie");
    if (cookie_it != headers.end()) {
      const std::string &cookies = cookie_it->second;
      const std::string prefix = "session_token=";
      auto pos = cookies.find(prefix);
      if (pos != std::string::npos) {
        pos += prefix.size();
        auto end = cookies.find(';', pos);
        return cookies.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
      }
    }
    return {};
  }

  /**
   * @brief Check authentication and authorization for an HTTP request.
   * @param request The HTTP request object.
   * @return AuthResult with outcome and response details if not authorized.
   */
  AuthResult check_auth(const req_https_t &request) {
    auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
    std::string auth_header;
    // Try Authorization header
    if (auto auth_it = request->header.find("authorization"); auth_it != request->header.end()) {
      auth_header = auth_it->second;
    } else {
      std::string token = extract_session_token_from_cookie(request->header);
      if (!token.empty()) {
        auth_header = "Session " + token;
      }
    }
    return check_auth(address, auth_header, request->path, request->method);
  }

  /**
   * @brief Authenticate the user or API token for a specific path/method.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @return True if authenticated and authorized, false otherwise.
   */
  bool authenticate(resp_https_t response, req_https_t request) {
    if (auto result = check_auth(request); !result.ok) {
      if (result.code == redirection_temporary_redirect) {
        response->write(result.code, result.headers);
      } else if (!result.body.empty()) {
        response->write(result.code, result.body, result.headers);
      } else {
        response->write(result.code);
      }
      return false;
    }
    return true;
  }

  /**
   * @brief Send a 404 Not Found response.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void not_found(resp_https_t response, [[maybe_unused]] req_https_t request) {
    constexpr auto code = client_error_not_found;

    nlohmann::json tree;
    tree["status_code"] = code;
    tree["error"] = "Not Found";

    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");

    response->write(code, tree.dump(), headers);
  }

  /**
   * @brief Send a 400 Bad Request response.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @param error_message The error message to include in the response.
   */
  void bad_request(resp_https_t response, [[maybe_unused]] req_https_t request, const std::string &error_message = "Bad Request") {
    constexpr auto code = client_error_bad_request;

    nlohmann::json tree;
    tree["status_code"] = code;
    tree["status"] = false;
    tree["error"] = error_message;

    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");

    response->write(code, tree.dump(), headers);
  }

  /**
   * @brief Get the index page.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @todo combine these functions into a single function that accepts the page, i.e "index", "pin", "apps"
   */
  void getIndexPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::string content = file_handler::read_file(WEB_DIR "index.html");
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/html; charset=utf-8");
    response->write(content, headers);
  }

  /**
   * @brief Get the PIN page.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void getPinPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::string content = file_handler::read_file(WEB_DIR "pin.html");
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/html; charset=utf-8");
    response->write(content, headers);
  }

  /**
   * @brief Get the apps page.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void getAppsPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::string content = file_handler::read_file(WEB_DIR "apps.html");
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/html; charset=utf-8");
    headers.emplace("Access-Control-Allow-Origin", "https://images.igdb.com/");
    response->write(content, headers);
  }

  /**
   * @brief Get the clients page.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void getClientsPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::string content = file_handler::read_file(WEB_DIR "clients.html");
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/html; charset=utf-8");
    response->write(content, headers);
  }

  /**
   * @brief Get the configuration page.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void getConfigPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::string content = file_handler::read_file(WEB_DIR "config.html");
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/html; charset=utf-8");
    response->write(content, headers);
  }

  /**
   * @brief Get the password page.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void getPasswordPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::string content = file_handler::read_file(WEB_DIR "password.html");
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/html; charset=utf-8");
    response->write(content, headers);
  }

  /**
   * @brief Get the welcome page.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void getWelcomePage(resp_https_t response, req_https_t request) {
    print_req(request);
    if (!config::sunshine.username.empty()) {
      send_redirect(response, request, "/");
      return;
    }
    std::string content = file_handler::read_file(WEB_DIR "welcome.html");
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/html; charset=utf-8");
    response->write(content, headers);
  }

  /**
   * @brief Get the login page.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void getLoginPage(resp_https_t response, req_https_t request) {
    print_req(request);
    
    std::string content = file_handler::read_file(WEB_DIR "login.html");
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/html; charset=utf-8");
    response->write(content, headers);
  }

  /**
   * @brief Get the troubleshooting page.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void getTroubleshootingPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::string content = file_handler::read_file(WEB_DIR "troubleshooting.html");
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/html; charset=utf-8");
    response->write(content, headers);
  }

  /**
   * @brief Get the favicon image.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @todo combine function with getSunshineLogoImage and possibly getNodeModules
   * @todo use mime_types map
   */
  void getFaviconImage(resp_https_t response, req_https_t request) {
    print_req(request);

    std::ifstream in(WEB_DIR "images/sunshine.ico", std::ios::binary);
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "image/x-icon");
    response->write(success_ok, in, headers);
  }

  /**
   * @brief Get the Sunshine logo image.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @todo combine function with getFaviconImage and possibly getNodeModules
   * @todo use mime_types map
   */
  void getSunshineLogoImage(resp_https_t response, req_https_t request) {
    print_req(request);

    std::ifstream in(WEB_DIR "images/logo-sunshine-45.png", std::ios::binary);
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "image/png");
    response->write(success_ok, in, headers);
  }

  /**
   * @brief Check if a path is a child of another path.
   * @param base The base path.
   * @param query The path to check.
   * @return True if the path is a child of the base path, false otherwise.
   */
  bool isChildPath(fs::path const &base, fs::path const &query) {
    auto relPath = fs::relative(base, query);
    return *(relPath.begin()) != fs::path("..");
  }

  /**
   * @brief Get an asset from the node_modules directory.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void getNodeModules(resp_https_t response, req_https_t request) {
    print_req(request);
    fs::path webDirPath(WEB_DIR);
    fs::path nodeModulesPath(webDirPath / "assets");

    // .relative_path is needed to shed any leading slash that might exist in the request path
    auto filePath = fs::weakly_canonical(webDirPath / fs::path(request->path).relative_path());

    // Don't do anything if file does not exist or is outside the assets directory
    if (!isChildPath(filePath, nodeModulesPath)) {
      BOOST_LOG(warning) << "Someone requested a path " << filePath << " that is outside the assets folder";
      bad_request(response, request);
      return;
    }
    if (!fs::exists(filePath)) {
      not_found(response, request);
      return;
    }

    auto relPath = fs::relative(filePath, webDirPath);
    // get the mime type from the file extension mime_types map
    // remove the leading period from the extension
    auto mimeType = mime_types.find(relPath.extension().string().substr(1));
    // check if the extension is in the map at the x position
    if (mimeType == mime_types.end()) {
      bad_request(response, request);
      return;
    }

    // if it is, set the content type to the mime type
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", mimeType->second);
    std::ifstream in(filePath.string(), std::ios::binary);
    response->write(success_ok, in, headers);
  }

  /**
   * @brief Get the list of available applications.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/apps| GET| null}
   */
  void getApps(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    try {
      std::string content = file_handler::read_file(config::stream.file_apps.c_str());
      nlohmann::json file_tree = nlohmann::json::parse(content);

      // Legacy versions of Sunshine used strings for boolean and integers, let's convert them
      // List of keys to convert to boolean
      std::vector<std::string> boolean_keys = {
        "exclude-global-prep-cmd",
        "elevated",
        "auto-detach",
        "wait-all"
      };

      // List of keys to convert to integers
      std::vector<std::string> integer_keys = {
        "exit-timeout"
      };

      // Walk fileTree and convert true/false strings to boolean or integer values
      for (auto &app : file_tree["apps"]) {
        for (const auto &key : boolean_keys) {
          if (app.contains(key) && app[key].is_string()) {
            app[key] = app[key] == "true";
          }
        }
        for (const auto &key : integer_keys) {
          if (app.contains(key) && app[key].is_string()) {
            app[key] = std::stoi(app[key].get<std::string>());
          }
        }
        if (app.contains("prep-cmd")) {
          for (auto &prep : app["prep-cmd"]) {
            if (prep.contains("elevated") && prep["elevated"].is_string()) {
              prep["elevated"] = prep["elevated"] == "true";
            }
          }
        }
      }

      send_response(response, file_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "GetApps: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Save an application. To save a new application the index must be `-1`. To update an existing application, you must provide the current index of the application.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the post request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "name": "Application Name",
   *   "output": "Log Output Path",
   *   "cmd": "Command to run the application",
   *   "index": -1,
   *   "exclude-global-prep-cmd": false,
   *   "elevated": false,
   *   "auto-detach": true,
   *   "wait-all": true,
   *   "exit-timeout": 5,
   *   "prep-cmd": [
   *     {
   *       "do": "Command to prepare",
   *       "undo": "Command to undo preparation",
   *       "elevated": false
   *     }
   *   ],
   *   "detached": [
   *     "Detached command"
   *   ],
   *   "image-path": "Full path to the application image. Must be a png file."
   * }
   * @endcode
   *
   * @api_examples{/api/apps| POST| {"name":"Hello, World!","index":-1}}
   */
  void saveApp(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      // TODO: Input Validation
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss);
      std::string file = file_handler::read_file(config::stream.file_apps.c_str());
      BOOST_LOG(info) << file;
      nlohmann::json file_tree = nlohmann::json::parse(file);

      if (input_tree["prep-cmd"].empty()) {
        input_tree.erase("prep-cmd");
      }

      if (input_tree["detached"].empty()) {
        input_tree.erase("detached");
      }

      auto &apps_node = file_tree["apps"];
      int index = input_tree["index"].get<int>();  // this will intentionally cause exception if the provided value is the wrong type

      input_tree.erase("index");

      if (index == -1) {
        apps_node.push_back(input_tree);
      } else {
        nlohmann::json newApps = nlohmann::json::array();
        for (size_t i = 0; i < apps_node.size(); ++i) {
          if (i == index) {
            newApps.push_back(input_tree);
          } else {
            newApps.push_back(apps_node[i]);
          }
        }
        file_tree["apps"] = newApps;
      }

      // Sort the apps array by name
      std::sort(apps_node.begin(), apps_node.end(), [](const nlohmann::json &a, const nlohmann::json &b) {
        return a["name"].get<std::string>() < b["name"].get<std::string>();
      });

      file_handler::write_file(config::stream.file_apps.c_str(), file_tree.dump(4));
      proc::refresh(config::stream.file_apps);

      output_tree["status"] = true;
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "SaveApp: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Close the currently running application.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/apps/close| POST| null}
   */
  void closeApp(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    proc::proc.terminate();

    nlohmann::json output_tree;
    output_tree["status"] = true;
    send_response(response, output_tree);
  }

  /**
   * @brief Delete an application.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/apps/9999| DELETE| null}
   */
  void deleteApp(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    try {
      nlohmann::json output_tree;
      nlohmann::json new_apps = nlohmann::json::array();
      std::string file = file_handler::read_file(config::stream.file_apps.c_str());
      nlohmann::json file_tree = nlohmann::json::parse(file);
      auto &apps_node = file_tree["apps"];
      const int index = std::stoi(request->path_match[1]);

      if (index < 0 || index >= static_cast<int>(apps_node.size())) {
        std::string error;
        if (const int max_index = static_cast<int>(apps_node.size()) - 1; max_index < 0) {
          error = "No applications to delete";
        } else {
          error = "'index' out of range, max index is "s + std::to_string(max_index);
        }
        bad_request(response, request, error);
        return;
      }

      for (size_t i = 0; i < apps_node.size(); ++i) {
        if (i != index) {
          new_apps.push_back(apps_node[i]);
        }
      }
      file_tree["apps"] = new_apps;

      file_handler::write_file(config::stream.file_apps.c_str(), file_tree.dump(4));
      proc::refresh(config::stream.file_apps);

      output_tree["status"] = true;
      output_tree["result"] = "application " + std::to_string(index) + " deleted";
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "DeleteApp: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Get the list of paired clients.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/clients/list| GET| null}
   */
  void getClients(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    const nlohmann::json named_certs = nvhttp::get_all_clients();

    nlohmann::json output_tree;
    output_tree["named_certs"] = named_certs;
    output_tree["status"] = true;
    send_response(response, output_tree);
  }

  /**
   * @brief Unpair a client.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the post request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *  "uuid": "<uuid>"
   * }
   * @endcode
   *
   * @api_examples{/api/unpair| POST| {"uuid":"1234"}}
   */
  void unpair(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();

    try {
      // TODO: Input Validation
      nlohmann::json output_tree;
      const nlohmann::json input_tree = nlohmann::json::parse(ss);
      const std::string uuid = input_tree.value("uuid", "");
      output_tree["status"] = nvhttp::unpair_client(uuid);
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "Unpair: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Unpair all clients.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/clients/unpair-all| POST| null}
   */
  void unpairAll(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    nvhttp::erase_all_clients();
    proc::proc.terminate();

    nlohmann::json output_tree;
    output_tree["status"] = true;
    send_response(response, output_tree);
  }

  /**
   * @brief Get the configuration settings.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/config| GET| null}
   */
  void getConfig(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    nlohmann::json output_tree;
    output_tree["status"] = true;
    output_tree["platform"] = SUNSHINE_PLATFORM;
    output_tree["version"] = PROJECT_VER;

    auto vars = config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str()));

    for (auto &[name, value] : vars) {
      output_tree[name] = std::move(value);
    }

    send_response(response, output_tree);
  }

  /**
   * @brief Get the locale setting. This endpoint does not require authentication.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/configLocale| GET| null}
   */
  void getLocale(resp_https_t response, req_https_t request) {
    // we need to return the locale whether authenticated or not

    print_req(request);

    nlohmann::json output_tree;
    output_tree["status"] = true;
    output_tree["locale"] = config::sunshine.locale;
    send_response(response, output_tree);
  }

  /**
   * @brief Save the configuration settings.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the post request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "key": "value"
   * }
   * @endcode
   *
   * @attention{It is recommended to ONLY save the config settings that differ from the default behavior.}
   *
   * @api_examples{/api/config| POST| {"key":"value"}}
   */
  void saveConfig(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      // TODO: Input Validation
      std::stringstream config_stream;
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss);
      for (const auto &[k, v] : input_tree.items()) {
        if (v.is_null() || (v.is_string() && v.get<std::string>().empty())) {
          continue;
        }

        // v.dump() will dump valid json, which we do not want for strings in the config right now
        // we should migrate the config file to straight json and get rid of all this nonsense
        config_stream << k << " = " << (v.is_string() ? v.get<std::string>() : v.dump()) << std::endl;
      }
      file_handler::write_file(config::sunshine.config_file.c_str(), config_stream.str());
      output_tree["status"] = true;
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "SaveConfig: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Upload a cover image.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the post request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "key": "igdb_<game_id>",
   *   "url": "https://images.igdb.com/igdb/image/upload/t_cover_big_2x/<slug>.png"
   * }
   * @endcode
   *
   * @api_examples{/api/covers/upload| POST| {"key":"igdb_1234","url":"https://images.igdb.com/igdb/image/upload/t_cover_big_2x/abc123.png"}}
   */
  void uploadCover(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss);

      std::string key = input_tree.value("key", "");
      if (key.empty()) {
        bad_request(response, request, "Cover key is required");
        return;
      }
      std::string url = input_tree.value("url", "");

      const std::string coverdir = platf::appdata().string() + "/covers/";
      file_handler::make_directory(coverdir);

      std::basic_string path = coverdir + http::url_escape(key) + ".png";
      if (!url.empty()) {
        if (http::url_get_host(url) != "images.igdb.com") {
          bad_request(response, request, "Only images.igdb.com is allowed");
          return;
        }
        if (!http::download_file(url, path)) {
          bad_request(response, request, "Failed to download cover");
          return;
        }
      } else {
        auto data = SimpleWeb::Crypto::Base64::decode(input_tree.value("data", ""));

        std::ofstream imgfile(path);
        imgfile.write(data.data(), static_cast<int>(data.size()));
      }
      output_tree["status"] = true;
      output_tree["path"] = path;
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "UploadCover: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Get the logs from the log file.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/logs| GET| null}
   */
  void getLogs(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::string content = file_handler::read_file(config::sunshine.log_file.c_str());
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/plain");
    response->write(success_ok, content, headers);
  }

  /**
   * @brief Update existing credentials.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the post request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "currentUsername": "Current Username",
   *   "currentPassword": "Current Password",
   *   "newUsername": "New Username",
   *   "newPassword": "New Password",
   *   "confirmNewPassword": "Confirm New Password"
   * }
   * @endcode
   *
   * @api_examples{/api/password| POST| {"currentUsername":"admin","currentPassword":"admin","newUsername":"admin","newPassword":"admin","confirmNewPassword":"admin"}}
   */
  void savePassword(resp_https_t response, req_https_t request) {
    if (!config::sunshine.username.empty() && !authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::vector<std::string> errors = {};
    std::stringstream ss;
    std::stringstream config_stream;
    ss << request->content.rdbuf();
    try {
      // TODO: Input Validation
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss);
      std::string username = input_tree.value("currentUsername", "");
      std::string newUsername = input_tree.value("newUsername", "");
      std::string password = input_tree.value("currentPassword", "");
      std::string newPassword = input_tree.value("newPassword", "");
      std::string confirmPassword = input_tree.value("confirmNewPassword", "");
      if (newUsername.empty()) {
        newUsername = username;
      }
      if (newUsername.empty()) {
        errors.emplace_back("Invalid Username");
      } else {
        auto hash = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();
        if (config::sunshine.username.empty() || (boost::iequals(username, config::sunshine.username) && hash == config::sunshine.password)) {
          if (newPassword.empty() || newPassword != confirmPassword) {
            errors.emplace_back("Password Mismatch");
          } else {
            http::save_user_creds(config::sunshine.credentials_file, newUsername, newPassword);
            http::reload_user_creds(config::sunshine.credentials_file);
            output_tree["status"] = true;
          }
        } else {
          errors.emplace_back("Invalid Current Credentials");
        }
      }

      if (!errors.empty()) {
        // join the errors array
        std::string error = std::accumulate(errors.begin(), errors.end(), std::string(), [](const std::string &a, const std::string &b) {
          return a.empty() ? b : a + ", " + b;
        });
        bad_request(response, request, error);
        return;
      }

      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "SavePassword: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Send a pin code to the host. The pin is generated from the Moonlight client during the pairing process.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the post request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "pin": "<pin>",
   *   "name": "Friendly Client Name"
   * }
   * @endcode
   *
   * @api_examples{/api/pin| POST| {"pin":"1234","name":"My PC"}}
   */
  void savePin(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss);
      const std::string name = input_tree.value("name", "");
      const std::string pin = input_tree.value("pin", "");

      int _pin = 0;
      _pin = std::stoi(pin);
      if (_pin < 0 || _pin > 9999) {
        bad_request(response, request, "PIN must be between 0000 and 9999");
      }

      output_tree["status"] = nvhttp::pin(pin, name);
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "SavePin: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Reset the display device persistence.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/reset-display-device-persistence| POST| null}
   */
  void resetDisplayDevicePersistence(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    nlohmann::json output_tree;
    output_tree["status"] = display_device::reset_persistence();
    send_response(response, output_tree);
  }

  /**
   * @brief Restart Sunshine.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/restart| POST| null}
   */
  void restart(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    // We may not return from this call
    platf::restart();
  }

  /**
   * @brief Generate a new API token with specified scopes.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/token| POST| {"scopes":[{"path":"/api/apps","methods":["GET"]}]}}
   *
   * Request body example:
   * {
   *   "scopes": [
   *     { "path": "/api/apps", "methods": ["GET", "POST"] }
   *   ]
   * }
   *
   * Response example:
   * { "token": "..." }
   */
  void generateApiToken(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    std::string request_body;
    request->content >> request_body;
    auto result = apiTokenManager.generate_api_token(request_body, config::sunshine.username);
    if (result) {
      response->write(*result);
    } else {
      response->write(nlohmann::json {{"error", "Internal server error"}}.dump());
    }
  }

  /**
   * @brief List all active API tokens and their scopes.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/tokens| GET| null}
   *
   * Response example:
   * [
   *   {
   *     "hash": "...",
   *     "username": "admin",
   *     "created_at": 1719000000,
   *     "scopes": [
   *       { "path": "/api/apps", "methods": ["GET"] }
   *     ]
   *   }
   * ]
   */
  void listApiTokens(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }
    response->write(apiTokenManager.list_api_tokens_json());
  }

  /**
   * @brief Revoke (delete) an API token by its hash.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/token/abcdef1234567890| DELETE| null}
   *
   * Response example:
   * { "status": true }
   */
  void revokeApiToken(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    std::string hash;
    if (request->path_match.size() > 1) {
      hash = request->path_match[1];
    }

    auto result = apiTokenManager.revoke_api_token_by_hash(hash);
    if (result) {
      response->write(nlohmann::json {{"status", true}}.dump());
    } else {
      response->write(nlohmann::json {{"error", "Internal server error"}}.dump());
    }
  }

  /**
   * @brief Saves the current API tokens to the configuration state file in JSON format.
   *
   * This function serializes the `api_tokens` data structure into a JSON array,
   * then converts it into a Boost property tree for compatibility with the configuration
   * file format. Each API token includes its hash, username, creation timestamp, and
   * associated scopes (paths and allowed HTTP methods). The resulting structure is
   * written to the file specified by `config::nvhttp.file_state` under the
   * "root.api_tokens" key. If the file already exists, its contents are loaded and
   * updated; otherwise, a new file is created.
   *
   */
  void save_api_tokens() {
    apiTokenManager.save_api_tokens();
  }

  /**
   * @brief Loads API tokens from a JSON configuration file into the api_tokens map.
   *
   * This function clears the existing api_tokens map and attempts to read API token information
   * from the JSON file specified by config::nvhttp.file_state. If the file does not exist,
   * the function returns immediately.
   *
   * The function expects the JSON to contain a "root.api_tokens" array, where each entry describes
   * an API token with the following fields:
   *   - "hash": The token hash (string, required)
   *   - "username": The associated username (string, optional)
   *   - "created_at": The creation timestamp (int64, optional)
   *   - "scopes": An array of scope objects, each with:
   *       - "path": The API path (string)
   *       - "methods": An array of HTTP methods (strings)
   *
   * Only tokens with a non-empty "hash" and at least one valid scope are loaded.
   * Each loaded token is stored in the api_tokens map, keyed by its hash.
   *
   * @note If the configuration file or required fields are missing, the function silently skips loading.
   */
  void load_api_tokens() {
    apiTokenManager.load_api_tokens();
  }

  void start() {
    auto shutdown_event = mail::man->event<bool>(mail::shutdown);

    auto port_https = net::map_port(PORT_HTTPS);
    auto address_family = net::af_from_enum_string(config::sunshine.address_family);

    // Fix server initialization to use config::nvhttp.cert and config::nvhttp.pkey
    https_server_t server(config::nvhttp.cert, config::nvhttp.pkey);
    std::thread tcp; // Declare here for correct scope
    server.default_resource["DELETE"] = [](resp_https_t response, req_https_t request) {
      bad_request(response, request);
    };
    server.default_resource["PATCH"] = [](resp_https_t response, req_https_t request) {
      bad_request(response, request);
    };
    server.default_resource["POST"] = [](resp_https_t response, req_https_t request) {
      bad_request(response, request);
    };
    server.default_resource["PUT"] = [](resp_https_t response, req_https_t request) {
      bad_request(response, request);
    };
    server.default_resource["GET"] = not_found;
    server.resource["^/$"]["GET"] = getIndexPage;
    server.resource["^/pin/?$"]["GET"] = getPinPage;
    server.resource["^/apps/?$"]["GET"] = getAppsPage;
    server.resource["^/clients/?$"]["GET"] = getClientsPage;
    server.resource["^/config/?$"]["GET"] = getConfigPage;
    server.resource["^/password/?$"]["GET"] = getPasswordPage;
    server.resource["^/welcome/?$"]["GET"] = getWelcomePage;
    server.resource["^/login/?$"]["GET"] = getLoginPage;
    server.resource["^/troubleshooting/?$"]["GET"] = getTroubleshootingPage;
    server.resource["^/api/pin$"]["POST"] = savePin;
    server.resource["^/api/apps$"]["GET"] = getApps;
    server.resource["^/api/logs$"]["GET"] = getLogs;
    server.resource["^/api/apps$"]["POST"] = saveApp;
    server.resource["^/api/config$"]["GET"] = getConfig;
    server.resource["^/api/config$"]["POST"] = saveConfig;
    server.resource["^/api/configLocale$"]["GET"] = getLocale;
    server.resource["^/api/restart$"]["POST"] = restart;
    server.resource["^/api/reset-display-device-persistence$"]["POST"] = resetDisplayDevicePersistence;
    server.resource["^/api/password$"]["POST"] = savePassword;
    server.resource["^/api/apps/([0-9]+)$"]["DELETE"] = deleteApp;
    server.resource["^/api/clients/unpair-all$"]["POST"] = unpairAll;
    server.resource["^/api/clients/list$"]["GET"] = getClients;
    server.resource["^/api/clients/unpair$"]["POST"] = unpair;
    server.resource["^/api/apps/close$"]["POST"] = closeApp;
    server.resource["^/api/covers/upload$"]["POST"] = uploadCover;
    server.resource["^/images/sunshine.ico$"]["GET"] = getFaviconImage;
    server.resource["^/images/logo-sunshine-45.png$"]["GET"] = getSunshineLogoImage;
    server.resource["^/assets\\/.+$"]["GET"] = getNodeModules;
    server.resource["^/api/token$"]["POST"] = generateApiToken;
    server.resource["^/api/tokens$"]["GET"] = listApiTokens;
    server.resource["^/api/token/([a-fA-F0-9]+)$"]["DELETE"] = revokeApiToken;
    server.resource["^/api-tokens/?$"]["GET"] = getTokenPage;
    server.resource["^/api/auth/login$"]["POST"] = loginUser;
    server.resource["^/api/auth/logout$"]["POST"] = logoutUser;
    server.resource["^/api/auth/refresh$"]["POST"] = refreshSessionToken;
    server.config.reuse_address = true;
    server.config.address = net::af_to_any_address_string(address_family);
    server.config.port = port_https;

    auto accept_and_run = [&](auto *server) {
      try {
        server->start([](unsigned short port) {
          BOOST_LOG(info) << "Configuration UI available at [https://localhost:"sv << port << "]";
        });
      } catch (boost::system::system_error &err) {
        // It's possible the exception gets thrown after calling server->stop() from a different thread
        if (shutdown_event->peek()) {
          return;
        }
        BOOST_LOG(fatal) << "Couldn't start Configuration HTTPS server on port ["sv << port_https << "]: "sv << err.what();
        shutdown_event->raise(true);
        return;
      }
    };
    tcp = std::thread{accept_and_run, &server};

    load_api_tokens();

    // Start a background task to clean up expired session tokens every hour
    std::jthread cleanup_thread([shutdown_event]() {
      while (!shutdown_event->peek()) {
        std::this_thread::sleep_for(std::chrono::hours(1));
        sessionTokenManager.cleanup_expired_session_tokens();
      }
    });

    // Wait for any event
    shutdown_event->view();

    server.stop();

    if (tcp.joinable()) {
      tcp.join();
    }
    // std::jthread auto-joins on destruction, no need for joinable/join
  }

  /**
   * @brief Handles the HTTP request to serve the API token management page.
   *
   * This function authenticates the incoming request and, if successful,
   * reads the "api-tokens.html" file from the web directory and sends its
   * contents as an HTTP response with the appropriate content type.
   *
   * @param response The HTTP response object used to send data back to the client.
   * @param request The HTTP request object containing client request data.
   */
  void getTokenPage(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }
    print_req(request);
    std::string content = file_handler::read_file(WEB_DIR "api-tokens.html");
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/html; charset=utf-8");
    response->write(content, headers);
  }

  /**
   * @brief Converts a string representation of a token scope to its corresponding TokenScope enum value.
   *
   * This function takes a string view and returns the matching TokenScope enum value.
   * Supported string values are "Read", "read", "Write", and "write".
   * If the input string does not match any known scope, an std::invalid_argument exception is thrown.
   *
   * @param s The string view representing the token scope.
   * @return TokenScope The corresponding TokenScope enum value.
   * @throws std::invalid_argument If the input string does not match any known scope.
   */
  TokenScope scope_from_string(std::string_view s) {
    if (s == "Read" || s == "read") {
      return TokenScope::Read;
    }
    if (s == "Write" || s == "write") {
      return TokenScope::Write;
    }
    throw std::invalid_argument("Unknown TokenScope: " + std::string(s));
  }

  /**
   * @brief Converts a TokenScope enum value to its string representation.
   * @param scope The TokenScope enum value to convert.
   * @return The string representation of the scope.
   */
  std::string scope_to_string(TokenScope scope) {
    switch (scope) {
      case TokenScope::Read:
        return "Read";
      case TokenScope::Write:
        return "Write";
      default:
        throw std::invalid_argument("Unknown TokenScope enum value");
    }
  }

  /**
   * @brief Helper to check session token authentication.
   * @param rawAuth The raw authorization header value.
   * @return AuthResult with outcome and response details if not authorized.
   */
  AuthResult check_session_auth(const std::string &rawAuth) {
    if (rawAuth.rfind("Session ", 0) != 0) {
      return make_auth_error(client_error_unauthorized, "Invalid session token format", true);
    }
    
    if (auto token = rawAuth.substr(8); !sessionTokenManager.validate_session_token(token)) {
      return make_auth_error(client_error_unauthorized, "Invalid or expired session token", true);
    }
    
    return {true, success_ok, {}, {}};
  }

  /**
   * @brief User login endpoint to generate session tokens.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * 
   * Expects JSON body:
   * {
   *   "username": "string",
   *   "password": "string"
   * }
   * 
   * Returns:
   * {
   *   "status": true,
   *   "token": "session_token_string",
   *   "expires_in": 86400
   * }
   * 
   * @api_examples{/api/auth/login| POST| {"username": "admin", "password": "password"}}
   */
  void loginUser(resp_https_t response, req_https_t request) {
    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    
    try {
      nlohmann::json input_tree = nlohmann::json::parse(ss);
      
      if (!input_tree.contains("username") || !input_tree.contains("password")) {
        bad_request(response, request, "Missing username or password");
        return;
      }
      
      std::string username = input_tree["username"].get<std::string>();
      std::string password = input_tree["password"].get<std::string>();
      
      // Validate credentials using existing basic auth logic
      if (auto hash = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();
          !boost::iequals(username, config::sunshine.username) || hash != config::sunshine.password) {
        
        auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
        BOOST_LOG(info) << "Web UI: ["sv << address << "] -- login failed for user: " << username;
        
        nlohmann::json error_tree;
        error_tree["status"] = false;
        error_tree["error"] = "Invalid credentials";
        
        SimpleWeb::CaseInsensitiveMultimap headers;
        headers.emplace("Content-Type", "application/json");
        
        response->write(client_error_unauthorized, error_tree.dump(), headers);
        return;
      }
      
      // Generate session token
      std::string session_token = sessionTokenManager.generate_session_token(username);
      
      nlohmann::json output_tree;
      output_tree["status"] = true;
      output_tree["token"] = session_token;
      output_tree["expires_in"] = std::chrono::duration_cast<std::chrono::seconds>(SESSION_TOKEN_DURATION).count();
      
      // Set session cookie
      SimpleWeb::CaseInsensitiveMultimap headers;
      headers.emplace("Content-Type", "application/json");
      std::string cookie = "session_token=" + session_token + "; Path=/; HttpOnly; SameSite=Strict";
      headers.emplace("Set-Cookie", cookie);
      response->write(success_ok, output_tree.dump(), headers);
      return;
      
    } catch (const nlohmann::json::exception &e) {
      BOOST_LOG(warning) << "Login JSON error:"sv << e.what();
      bad_request(response, request, "Invalid JSON format");
    }
  }

  /**
   * @brief User logout endpoint to revoke session tokens.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * 
   * @api_examples{/api/auth/logout| POST| null}
   */
  void logoutUser(resp_https_t response, req_https_t request) {
    print_req(request);

    if (auto auth = request->header.find("authorization"); 
        auth != request->header.end() && auth->second.rfind("Session ", 0) == 0) {
      std::string token = auth->second.substr(8);
      sessionTokenManager.revoke_session_token(token);
    }

    nlohmann::json output_tree;
    output_tree["status"] = true;
    output_tree["message"] = "Logged out successfully";

    send_response(response, output_tree);
  }

  /**
   * @brief Refresh session token endpoint.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * 
   * @api_examples{/api/auth/refresh| POST| null}
   */
  void refreshSessionToken(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    auto auth = request->header.find("authorization");
    if (auth == request->header.end() || auth->second.rfind("Session ", 0) != 0) {
      bad_request(response, request, "Session token required for refresh");
      return;
    }

    std::string old_token = auth->second.substr(8);
    // Get username via SessionTokenManager
    auto maybe_user = sessionTokenManager.get_username_for_token(old_token);
    if (!maybe_user) {
      bad_request(response, request, "Invalid session token");
      return;
    }
    std::string username = *maybe_user;

    // Revoke old token and generate new one
    sessionTokenManager.revoke_session_token(old_token);
    std::string new_token = sessionTokenManager.generate_session_token(username);

    nlohmann::json output_tree;
    output_tree["status"] = true;
    output_tree["token"] = new_token;
    output_tree["expires_in"] = std::chrono::duration_cast<std::chrono::seconds>(SESSION_TOKEN_DURATION).count();
    send_response(response, output_tree);
  }

  /**
   * @brief Helper to determine if request is for HTML page vs API.
   * @param path The request path.
   * @return True if this is a request for an HTML page.
   */
  bool is_html_request(const std::string &path) {
    // API requests start with /api/
    if (path.rfind("/api/", 0) == 0) {
      return false;
    }
    
    // Asset requests
    if (path.rfind("/assets/", 0) == 0 || path.rfind("/images/", 0) == 0) {
      return false;
    }
    
    // Everything else is likely an HTML page request
    return true;
  }
}  // namespace confighttp
