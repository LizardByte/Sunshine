#include "http_auth.h"

#include "config.h"
#include "confighttp.h"
#include "crypto.h"
#include "httpcommon.h"
#include "logging.h"
#include "network.h"
#include "nvhttp.h"
#include "utility.h"

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/function.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/regex.hpp>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <set>
#include <Simple-Web-Server/crypto.hpp>
#include <Simple-Web-Server/server_https.hpp>
#include <string>
#include <vector>
using namespace std::literals;
namespace pt = boost::property_tree;
namespace fs = std::filesystem;

namespace confighttp {

  // Global instances for authentication
  ApiTokenManager api_token_manager;
  SessionTokenManager session_token_manager(SessionTokenManager::make_default_dependencies());
  SessionTokenAPI session_token_api(session_token_manager);

  InvalidScopeException::InvalidScopeException(const std::string &msg):
      _message(msg) {}

  const char *InvalidScopeException::what() const noexcept {
    return _message.c_str();
  }

  ApiTokenManager::ApiTokenManager(const ApiTokenManagerDependencies &dependencies):
      _dependencies(dependencies) {}

  bool ApiTokenManager::authenticate_token(const std::string &token, const std::string &path, const std::string &method) {
    std::scoped_lock lock(_mutex);
    std::string token_hash = _dependencies.hash(token);
    auto it = _api_tokens.find(token_hash);
    if (it == _api_tokens.end()) {
      return false;
    }

    std::string req_method = boost::to_upper_copy(method);

    auto is_method_allowed = [&](const std::set<std::string, std::less<>> &methods) {
      return std::ranges::any_of(methods, [&](const std::string &m) {
        return boost::iequals(m, req_method);
      });
    };

    auto regex_path_match = [](const std::string &scope_path, const std::string &request_path) {
      std::string pattern = scope_path;
      if (pattern.empty() || pattern[0] != '^') {
        pattern = "^" + pattern;
      }
      if (pattern.empty() || pattern.back() != '$') {
        pattern += "$";
      }
      boost::regex re(pattern);
      return boost::regex_match(request_path, re);
    };

    return std::ranges::any_of(it->second.path_methods, [&](const auto &pair) {
      const auto &scope_path = pair.first;
      const auto &methods = pair.second;
      return regex_path_match(scope_path, path) && is_method_allowed(methods);
    });
  }

  bool ApiTokenManager::authenticate_bearer(std::string_view raw_auth, const std::string &path, const std::string &method) {
    if (raw_auth.length() <= 7 || !raw_auth.starts_with("Bearer ")) {
      return false;
    }
    auto token = std::string(raw_auth.substr(7));
    return authenticate_token(token, path, method);
  }

  std::optional<std::string> ApiTokenManager::create_api_token(const nlohmann::json &scopes_json, const std::string &username) {
    auto path_methods = parse_json_scopes(scopes_json);
    if (!path_methods) {
      return std::nullopt;
    }
    std::string token = _dependencies.rand_alphabet(32);
    std::string token_hash = _dependencies.hash(token);
    ApiTokenInfo info {token_hash, *path_methods, username, _dependencies.now()};
    {
      std::scoped_lock lock(_mutex);
      _api_tokens[token_hash] = info;
    }
    save_api_tokens();
    return token;
  }

  std::optional<std::string> ApiTokenManager::generate_api_token(const std::string &request_body, const std::string &username) {
    nlohmann::json input;
    try {
      input = nlohmann::json::parse(request_body);
    } catch (const nlohmann::json::exception &) {
      return std::nullopt;
    }
    if (!input.contains("scopes") || !input["scopes"].is_array()) {
      return std::nullopt;
    }
    return create_api_token(input["scopes"], username);
  }

  std::optional<std::map<std::string, std::set<std::string, std::less<>>, std::less<>>>
    ApiTokenManager::parse_json_scopes(const nlohmann::json &scopes_json) const {
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    try {
      for (const auto &s : scopes_json) {
        if (!s.contains("path") || !s.contains("methods") || !s["methods"].is_array()) {
          throw InvalidScopeException("Invalid scopes configured on API Token, missing 'path' or 'methods' array");
        }
        std::string path = s["path"].get<std::string>();
        std::set<std::string, std::less<>> methods;
        for (const auto &m : s["methods"]) {
          methods.insert(boost::to_upper_copy(m.get<std::string>()));
        }
        path_methods[path] = methods;
      }
    } catch (const InvalidScopeException &) {
      BOOST_LOG(warning) << "Invalid scope detected in API token, please delete and recreate the token to resolve.";
      return std::nullopt;
    }
    return path_methods;
  }

  nlohmann::json ApiTokenManager::get_api_tokens_list() const {
    nlohmann::json arr = nlohmann::json::array();
    std::scoped_lock lock(_mutex);
    for (const auto &[hash, info] : _api_tokens) {
      nlohmann::json obj;
      obj["hash"] = hash;
      obj["username"] = info.username;
      obj["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(info.created_at.time_since_epoch()).count();
      obj["scopes"] = nlohmann::json::array();
      for (const auto &[path, methods] : info.path_methods) {
        obj["scopes"].push_back({{"path", path}, {"methods", methods}});
      }
      arr.push_back(obj);
    }
    return arr;
  }

  std::string ApiTokenManager::list_api_tokens_json() const {
    return get_api_tokens_list().dump();
  }

  bool ApiTokenManager::revoke_api_token_by_hash(const std::string &hash) {
    if (hash.empty()) {
      return false;
    }
    bool erased = false;
    {
      std::scoped_lock lock(_mutex);
      erased = _api_tokens.erase(hash) > 0;
    }
    if (erased) {
      save_api_tokens();
    }
    return erased;
  }

  void ApiTokenManager::save_api_tokens() const {
    nlohmann::json j;
    {
      std::scoped_lock lock(_mutex);
      for (const auto &[hash, info] : _api_tokens) {
        nlohmann::json obj;
        obj["hash"] = hash;
        obj["username"] = info.username;
        obj["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(info.created_at.time_since_epoch()).count();
        obj["scopes"] = nlohmann::json::array();
        for (const auto &[path, methods] : info.path_methods) {
          obj["scopes"].push_back({{"path", path}, {"methods", methods}});
        }
        j.push_back(obj);
      }
    }
    pt::ptree root;
    if (_dependencies.file_exists(config::nvhttp.file_state)) {
      _dependencies.read_json(config::nvhttp.file_state, root);
    }
    pt::ptree tokens_pt;
    for (const auto &tok : j) {
      pt::ptree t;
      t.put("hash", tok["hash"].get<std::string>());
      t.put("username", tok["username"].get<std::string>());
      t.put("created_at", tok["created_at"].get<std::int64_t>());
      pt::ptree scopes_pt;
      for (const auto &s : tok["scopes"]) {
        pt::ptree spt;
        spt.put("path", s["path"].get<std::string>());
        pt::ptree mpt;
        for (const auto &m : s["methods"]) {
          mpt.push_back({"", pt::ptree(m.get<std::string>())});
        }
        spt.add_child("methods", mpt);
        scopes_pt.push_back({"", spt});
      }
      t.add_child("scopes", scopes_pt);
      tokens_pt.push_back({"", t});
    }
    root.put_child("root.api_tokens", tokens_pt);
    _dependencies.write_json(config::nvhttp.file_state, root);
  }

  std::optional<std::pair<std::string, std::set<std::string, std::less<>>>> ApiTokenManager::parse_scope(const pt::ptree &scope_tree) const {
    const std::string path = scope_tree.get<std::string>("path", "");
    if (path.empty()) {
      return std::nullopt;
    }
    std::set<std::string, std::less<>> methods;
    if (auto m_child = scope_tree.get_child_optional("methods")) {
      for (const auto &[_, method_node] : *m_child) {
        methods.insert(method_node.data());
      }
    }
    if (methods.empty()) {
      return std::nullopt;
    }
    return std::make_pair(path, std::move(methods));
  }

  std::map<std::string, std::set<std::string, std::less<>>, std::less<>> ApiTokenManager::build_scope_map(const pt::ptree &scopes_node) const {
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> out;
    for (const auto &[_, scope_tree] : scopes_node) {
      if (auto parsed = parse_scope(scope_tree)) {
        auto [path, methods] = std::move(*parsed);
        out.try_emplace(std::move(path), std::move(methods));
      }
    }
    return out;
  }

  void ApiTokenManager::load_api_tokens() {
    std::scoped_lock lock(_mutex);
    _api_tokens.clear();
    if (!_dependencies.file_exists(config::nvhttp.file_state)) {
      return;
    }
    pt::ptree root;
    _dependencies.read_json(config::nvhttp.file_state, root);
    if (auto api_tokens_node = root.get_child_optional("root.api_tokens")) {
      for (const auto &[_, token_tree] : *api_tokens_node) {
        const std::string hash = token_tree.get<std::string>("hash", "");
        if (hash.empty()) {
          continue;
        }
        ApiTokenInfo info {
          hash,
          {},
          token_tree.get<std::string>("username", ""),
          std::chrono::system_clock::time_point {
            std::chrono::seconds(token_tree.get<std::int64_t>("created_at", 0))
          }
        };
        if (auto scopes_node = token_tree.get_child_optional("scopes")) {
          info.path_methods = build_scope_map(*scopes_node);
        }
        _api_tokens.try_emplace(hash, std::move(info));
      }
    }
  }

  ApiTokenManagerDependencies ApiTokenManager::make_default_dependencies() {
    ApiTokenManagerDependencies dependencies;
    dependencies.file_exists = [](const std::string &path) {
      return fs::exists(path);
    };
    dependencies.read_json = [](const std::string &path, pt::ptree &tree) {
      boost::property_tree::json_parser::read_json(path, tree);
    };
    dependencies.write_json = [](const std::string &path, const pt::ptree &tree) {
      boost::property_tree::json_parser::write_json(path, tree);
    };
    dependencies.now = []() {
      return std::chrono::system_clock::now();
    };
    dependencies.rand_alphabet = [](std::size_t length) {
      return crypto::rand_alphabet(length);
    };
    dependencies.hash = [](const std::string &input) {
      return util::hex(crypto::hash(input)).to_string();
    };
    return dependencies;
  }

  const std::map<std::string, ApiTokenInfo, std::less<>> &ApiTokenManager::retrieve_loaded_api_tokens() const {
    return _api_tokens;
  }

  SessionTokenManager::SessionTokenManager(const SessionTokenManagerDependencies &dependencies):
      _dependencies(dependencies) {}

  SessionTokenManagerDependencies SessionTokenManager::make_default_dependencies() {
    SessionTokenManagerDependencies deps;
    deps.now = []() {
      return std::chrono::system_clock::now();
    };
    deps.rand_alphabet = [](std::size_t length) {
      return crypto::rand_alphabet(length);
    };
    deps.hash = [](const std::string &input) {
      auto hash_result = crypto::hash(input);
      return util::hex(hash_result).to_string();
    };
    return deps;
  }

  std::string SessionTokenManager::generate_session_token(const std::string &username) {
    std::scoped_lock lock(_mutex);
    std::string token = _dependencies.rand_alphabet(64);
    std::string token_hash = _dependencies.hash(token);
    auto now = _dependencies.now();
    auto expires = now + config::sunshine.session_token_ttl;
    _session_tokens[token_hash] = SessionToken {username, now, expires};
    cleanup_expired_session_tokens();
    return token;
  }

  bool SessionTokenManager::validate_session_token(const std::string &token) {
    std::scoped_lock lock(_mutex);
    std::string token_hash = _dependencies.hash(token);
    auto it = _session_tokens.find(token_hash);
    if (it == _session_tokens.end()) {
      return false;
    }
    if (auto now = _dependencies.now(); now > it->second.expires_at) {
      _session_tokens.erase(it);
      return false;
    }
    return true;
  }

  void SessionTokenManager::revoke_session_token(const std::string &token) {
    std::scoped_lock lock(_mutex);
    std::string token_hash = _dependencies.hash(token);
    _session_tokens.erase(token_hash);
  }

  void SessionTokenManager::cleanup_expired_session_tokens() {
    auto now = _dependencies.now();
    std::erase_if(_session_tokens, [now](const auto &pair) {
      return now > pair.second.expires_at;
    });
  }

  std::optional<std::string> SessionTokenManager::get_username_for_token(const std::string &token) {
    std::scoped_lock lock(_mutex);
    std::string token_hash = _dependencies.hash(token);
    if (auto it = _session_tokens.find(token_hash); it != _session_tokens.end() && _dependencies.now() <= it->second.expires_at) {
      return it->second.username;
    }
    return std::nullopt;
  }

  size_t SessionTokenManager::session_count() const {
    std::scoped_lock lock(_mutex);
    return _session_tokens.size();
  }

  SessionTokenAPI::SessionTokenAPI(SessionTokenManager &session_manager):
      _session_manager(session_manager) {}

  APIResponse SessionTokenAPI::login(const std::string &username, const std::string &password, const std::string &redirect_url) {
    if (!validate_credentials(username, password)) {
      BOOST_LOG(info) << "Web UI: Login failed for user: " << username;
      return create_error_response("Invalid credentials", StatusCode::client_error_unauthorized);
    }

    std::string session_token = _session_manager.generate_session_token(username);

    nlohmann::json response_data;
    response_data["token"] = session_token;
    response_data["expires_in"] = std::chrono::duration_cast<std::chrono::seconds>(config::sunshine.session_token_ttl).count();

    // Hardened secure redirect handling
    std::string safe_redirect = "/";
    if (!redirect_url.empty() && redirect_url[0] == '/') {
      std::string lower = redirect_url;
      boost::algorithm::to_lower(lower);
      // Disallow dangerous patterns
      if (!boost::algorithm::contains(lower, "://") &&
          !boost::algorithm::contains(lower, "%2f") &&
          !boost::algorithm::contains(lower, "\\") &&
          !boost::algorithm::contains(lower, "/..") &&
          !(redirect_url.size() > 1 && redirect_url[1] == '/')) {  // reject double slash
        // Unicode normalization: reject if normalized path differs
        std::string norm = redirect_url;
        std::ranges::replace(norm, '\\', '/');
        if (norm == redirect_url) {
          safe_redirect = redirect_url;
        }
      }
    }
    response_data["redirect"] = safe_redirect;

    APIResponse response = create_success_response(response_data);

    // Set session cookie with Secure if HTTPS or localhost
    // Percent-encode token for safe cookie storage
    std::string encoded = http::cookie_escape(session_token);
    std::string cookie = "session_token=" + encoded + "; Path=/; HttpOnly; SameSite=Strict";
    cookie += "; Secure";
    response.headers.emplace("Set-Cookie", cookie);

    // Set CORS header for localhost only (no wildcard), dynamically set port
    std::uint16_t https_port = net::map_port(nvhttp::PORT_HTTPS);
    std::string cors_origin = std::format("https://localhost:{}", https_port);
    response.headers.emplace("Access-Control-Allow-Origin", cors_origin);

    return response;
  }

  APIResponse SessionTokenAPI::logout(const std::string &session_token) {
    if (!session_token.empty()) {
      _session_manager.revoke_session_token(session_token);
    }

    nlohmann::json response_data;
    response_data["message"] = "Logged out successfully";

    // Create success response and clear the session cookie so the client no longer retains it
    APIResponse response = create_success_response(response_data);
    // Set-Cookie header to clear the session token on client
    std::string clear_cookie = "session_token=; Path=/; HttpOnly; SameSite=Strict; Expires=Thu, 01 Jan 1970 00:00:00 GMT";
    response.headers.emplace("Set-Cookie", clear_cookie);
    return response;
  }

  APIResponse SessionTokenAPI::validate_session(const std::string &session_token) {
    if (session_token.empty()) {
      return create_error_response("Session token required", SimpleWeb::StatusCode::client_error_unauthorized);
    }

    if (bool is_valid = _session_manager.validate_session_token(session_token); !is_valid) {
      return create_error_response("Invalid or expired session token", SimpleWeb::StatusCode::client_error_unauthorized);
    }

    return create_success_response();
  }

  bool SessionTokenAPI::validate_credentials(const std::string &username, const std::string &password) const {
    if (auto hash = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();
        !boost::iequals(username, config::sunshine.username) || hash != config::sunshine.password) {
      return false;
    }
    return true;
  }

  namespace {
    std::string get_cors_origin() {
      std::uint16_t https_port = net::map_port(confighttp::PORT_HTTPS);
      return std::format("https://localhost:{}", https_port);
    }
  }  // namespace

  APIResponse SessionTokenAPI::create_success_response(const nlohmann::json &data) const {
    nlohmann::json response_body;
    response_body["status"] = true;
    for (auto &[key, value] : data.items()) {
      response_body[key] = value;
    }
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    headers.emplace("Access-Control-Allow-Origin", get_cors_origin());
    return APIResponse(StatusCode::success_ok, response_body.dump(), headers);
  }

  APIResponse SessionTokenAPI::create_error_response(const std::string &error_message, StatusCode status_code) const {
    nlohmann::json response_body;
    response_body["status"] = false;
    response_body["error"] = error_message;
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    headers.emplace("Access-Control-Allow-Origin", get_cors_origin());
    return APIResponse(status_code, response_body.dump(), headers);
  }

  bool authenticate_basic(const std::string_view raw_auth) {
    auto base64 = std::string(raw_auth.substr(6));
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

  AuthResult make_auth_error(StatusCode code, const std::string &error, bool add_www_auth, const std::string &location) {
    AuthResult result {false, code, {}, {}};
    if (!location.empty()) {
      result.headers.emplace("Location", location);
      result.headers.emplace("Access-Control-Allow-Origin", get_cors_origin());
      return result;
    }
    nlohmann::json tree;
    tree["status_code"] = static_cast<std::underlying_type_t<StatusCode>>(code);
    tree["status"] = false;
    tree["error"] = error;
    result.body = tree.dump();
    result.headers.emplace("Content-Type", "application/json");
    result.headers.emplace("Access-Control-Allow-Origin", get_cors_origin());
    if (add_www_auth) {
      result.headers.emplace("WWW-Authenticate", R"(Basic realm="Sunshine Gamestream Host", charset="UTF-8")");
    }
    return result;
  }

  AuthResult check_bearer_auth(const std::string &raw_auth, const std::string &path, const std::string &method) {
    if (!api_token_manager.authenticate_bearer(raw_auth, path, method)) {
      return make_auth_error(StatusCode::client_error_forbidden, "Forbidden: Token does not have permission for this path/method.");
    }
    return {true, StatusCode::success_ok, {}, {}};
  }

  AuthResult check_basic_auth(const std::string &raw_auth) {
    if (!authenticate_basic(raw_auth)) {
      return make_auth_error(StatusCode::client_error_unauthorized, "Unauthorized", true);
    }
    return {true, StatusCode::success_ok, {}, {}};
  }

  AuthResult check_session_auth(const std::string &raw_auth) {
    if (raw_auth.rfind("Session ", 0) != 0) {
      return make_auth_error(StatusCode::client_error_unauthorized, "Invalid session token format", true);
    }

    std::string token = raw_auth.substr(8);

    if (APIResponse api_response = session_token_api.validate_session(token); api_response.status_code == StatusCode::success_ok) {
      return {true, StatusCode::success_ok, {}, {}};
    }

    return make_auth_error(StatusCode::client_error_unauthorized, "Invalid or expired session token", true);
  }

  bool is_html_request(const std::string &path) {
    // API requests start with /api/
    if (path.rfind("/api/", 0) == 0) {
      return false;
    }

    // Asset requests in known directories
    if (path.rfind("/assets/", 0) == 0 || path.rfind("/images/", 0) == 0) {
      return false;
    }

    // Static file extensions should not be treated as HTML
    {
      std::string ext = std::filesystem::path(path).extension().string();
      boost::algorithm::to_lower(ext);
      static const std::vector<std::string> non_html_ext = {".js", ".css", ".map", ".json", ".woff", ".woff2", ".ttf", ".eot", ".ico", ".png", ".jpg", ".jpeg", ".svg"};
      if (std::ranges::find(non_html_ext, ext) != non_html_ext.end()) {
        return false;
      }
    }

    // Everything else is likely an HTML page request
    return true;
  }

  AuthResult check_auth(const std::string &remote_address, const std::string &auth_header, const std::string &path, const std::string &method) {
    // Strip query string from path for matching
    auto base_path = path;
    if (auto qpos = base_path.find('?'); qpos != std::string::npos) {
      base_path.resize(qpos);
    }

    // Allow welcome page without authentication
    if (base_path == "/welcome" || base_path == "/welcome/") {
      return {true, StatusCode::success_ok, {}, {}};
    }

    if (auto ip_type = net::from_address(remote_address); ip_type > http::origin_web_ui_allowed) {
      BOOST_LOG(info) << "Web UI: ["sv << remote_address << "] -- denied"sv;
      return make_auth_error(StatusCode::client_error_forbidden, "Forbidden");
    }

    if (config::sunshine.username.empty()) {
      return make_auth_error(StatusCode::redirection_temporary_redirect, {}, false, "/welcome");
    }

    // For login page, don't require authentication (match path without query)
    if (base_path == "/login" || base_path == "/login/") {
      return {true, StatusCode::success_ok, {}, {}};
    }

    if (auth_header.empty()) {
      // For HTML page requests, redirect to login instead of showing 401 (use base_path)
      if (is_html_request(base_path)) {
        std::string login_url = std::format("/login?redirect={}", path);
        return make_auth_error(StatusCode::redirection_temporary_redirect, {}, false, login_url);
      }
      // For API requests, send 401 with WWW-Authenticate header for basic auth
      return make_auth_error(StatusCode::client_error_unauthorized, "Unauthorized", true);
    }

    if (auth_header.rfind("Bearer ", 0) == 0) {
      return check_bearer_auth(auth_header, path, method);
    }

    if (auth_header.rfind("Basic ", 0) == 0) {
      return check_basic_auth(auth_header);
    }

    if (auth_header.rfind("Session ", 0) == 0) {
      {
        auto session_res = check_session_auth(auth_header);
        if (!session_res.ok) {
          std::string login_url = "/login?redirect=" + path;
          return make_auth_error(StatusCode::redirection_temporary_redirect, {}, false, login_url);
        }
        return session_res;
      }
    }

    // For HTML page requests, redirect to login instead of showing 401 (use base_path)
    if (is_html_request(base_path)) {
      std::string login_url = "/login?redirect=" + path;
      return make_auth_error(StatusCode::redirection_temporary_redirect, {}, false, login_url);
    }

    return make_auth_error(StatusCode::client_error_unauthorized, "Unauthorized", true);
  }

  std::string extract_session_token_from_cookie(const SimpleWeb::CaseInsensitiveMultimap &headers) {
    if (auto cookie_it = headers.find("Cookie"); cookie_it != headers.end()) {
      const std::string &cookies = cookie_it->second;
      const std::string prefix = "session_token=";
      auto pos = cookies.find(prefix);
      if (pos != std::string::npos) {
        pos += prefix.size();
        auto end = cookies.find(';', pos);
        // Decode percent-encoded session token
        auto raw = cookies.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        return http::cookie_unescape(raw);
      }
    }
    return {};
  }

}  // namespace confighttp
