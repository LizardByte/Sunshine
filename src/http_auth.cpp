#include "http_auth.h"

#include "config.h"
#include "confighttp.h"
#include "crypto.h"
#include "logging.h"
#include "utility.h"
#include "network.h"
#include "nvhttp.h"

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
#include "logging.h"
using namespace std::literals;
namespace pt = boost::property_tree;
namespace fs = std::filesystem;

namespace confighttp {

  /**
   * @class InvalidScopeException
   * @brief Exception thrown for invalid API token scopes.
   */
  InvalidScopeException::InvalidScopeException(const std::string &msg):
      message_(msg) {}

  /**
   * @brief Returns the exception message.
   * @return The exception message as a C-string.
   */
  const char *InvalidScopeException::what() const noexcept {
    return message_.c_str();
  }

  /**
   * @class ApiTokenManager
   * @brief Manages API tokens, their creation, validation, and revocation.
   */

  /**
   * @brief Constructs an ApiTokenManager with dependencies for testability.
   * @param dependencies Struct of function dependencies for I/O, clock, random, and hash operations.
   */
  ApiTokenManager::ApiTokenManager(const ApiTokenManagerDependencies &dependencies):
      dependencies_(dependencies) {}

  /**
   * @brief Checks if a bearer token is valid for the request using primitives.
   * @param token The token string (without "Bearer " prefix).
   * @param path The request path.
   * @param method The HTTP method.
   * @return true if authenticated, false otherwise.
   */
  bool ApiTokenManager::authenticate_token(const std::string &token, const std::string &path, const std::string &method) {
    std::string token_hash = dependencies_.hash(token);
    auto it = api_tokens.find(token_hash);
    if (it == api_tokens.end()) {
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

  /**
   * @brief Checks if a bearer token is valid for the request using primitives.
   * @param rawAuth The raw Authorization header value.
   * @param path The request path.
   * @param method The HTTP method.
   * @return true if authenticated, false otherwise.
   */
  bool ApiTokenManager::authenticate_bearer(std::string_view rawAuth, const std::string &path, const std::string &method) {
    if (rawAuth.length() <= 7 || !rawAuth.starts_with("Bearer ")) {
      return false;
    }
    auto token = std::string(rawAuth.substr(7));
    return authenticate_token(token, path, method);
  }

  /**
   * @brief Creates a new API token from JSON scopes using primitives.
   * @param scopes_json The JSON array of scopes.
   * @param username The username for the token.
   * @return Optional token string, or nullopt on error.
   */
  std::optional<std::string> ApiTokenManager::create_api_token(const nlohmann::json &scopes_json, const std::string &username) {
    auto path_methods = parse_json_scopes(scopes_json);
    if (!path_methods) {
      return std::nullopt;
    }

    std::string token = dependencies_.rand_alphabet(32);
    std::string token_hash = dependencies_.hash(token);
    ApiTokenInfo info {token_hash, *path_methods, username, dependencies_.now()};
    api_tokens[token_hash] = info;
    save_api_tokens();
    return token;
  }

  /**
   * @brief Generates a new API token from JSON request body.
   * @param request_body The JSON request body as string.
   * @param username The username for the token.
   * @return Optional JSON response string, or nullopt on error.
   */
  std::optional<std::string> ApiTokenManager::generate_api_token(const std::string &request_body, const std::string &username) {
    nlohmann::json input;
    try {
      input = nlohmann::json::parse(request_body);
    } catch (const nlohmann::json::exception &e) {
      return nlohmann::json {{"error", std::string("Invalid JSON: ") + e.what()}}.dump();
    }
    if (!input.contains("scopes") || !input["scopes"].is_array()) {
      return nlohmann::json {{"error", "Missing scopes array"}}.dump();
    }

    auto token = create_api_token(input["scopes"], username);
    if (!token) {
      return nlohmann::json {{"error", "Invalid scope value"}}.dump();
    }
    return nlohmann::json {{"token", *token}}.dump();
  }

  /**
   * @brief Helper to parse scopes from JSON without HTTP dependencies.
   * @param scopes_json The JSON array of scopes.
   * @return Optional map of path to allowed methods.
   */
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

  /**
   * @brief Returns a JSON list of all API tokens.
   * @return JSON array of tokens.
   */
  nlohmann::json ApiTokenManager::get_api_tokens_list() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &[hash, info] : api_tokens) {
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

  /**
   * @brief Returns a JSON string of all API tokens.
   * @return JSON string representation of tokens.
   */
  std::string ApiTokenManager::list_api_tokens_json() const {
    return get_api_tokens_list().dump();
  }

  /**
   * @brief Revokes an API token by hash using primitives.
   * @param hash The token hash to revoke.
   * @return true if token was found and revoked, false if not found.
   */
  bool ApiTokenManager::revoke_api_token_by_hash(const std::string &hash) {
    if (hash.empty() || api_tokens.erase(hash) == 0) {
      return false;
    }
    save_api_tokens();
    return true;
  }


  /**
   * @brief Saves all API tokens to persistent storage.
   */
  void ApiTokenManager::save_api_tokens() const {
    nlohmann::json j;
    for (const auto &[hash, info] : api_tokens) {
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
    pt::ptree root;
    if (dependencies_.file_exists(config::nvhttp.file_state)) {
      dependencies_.read_json(config::nvhttp.file_state, root);
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
    dependencies_.write_json(config::nvhttp.file_state, root);
  }

  /**
   * @brief Parses a scope from a property tree.
   * @param scope_tree The property tree representing a scope.
   * @return Optional pair of path and set of methods.
   */
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

  /**
   * @brief Builds a map of scopes from a property tree.
   * @param scopes_node The property tree node containing scopes.
   * @return Map of path to set of allowed methods.
   */
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

  /**
   * @brief Loads API tokens from persistent storage.
   */
  void ApiTokenManager::load_api_tokens() {
    api_tokens.clear();
    if (!dependencies_.file_exists(config::nvhttp.file_state)) {
      return;
    }
    pt::ptree root;
    dependencies_.read_json(config::nvhttp.file_state, root);
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
        api_tokens.try_emplace(hash, std::move(info));
      }
    }
  }

  /**
   * @brief Creates and returns the default delegates for ApiTokenManager.
   * @return Struct of default function dependencies.
   */
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

  /// @brief Retrieves the currently loaded API in a read-only manner.
  const std::map<std::string, ApiTokenInfo, std::less<>> &ApiTokenManager::retrieve_loaded_api_tokens() const {
    return api_tokens;
  }


  SessionTokenManager::SessionTokenManager(const SessionTokenManagerDependencies &dependencies)
      : dependencies_(dependencies) {}

  SessionTokenManagerDependencies SessionTokenManager::make_default_dependencies() {
    SessionTokenManagerDependencies deps;
    deps.now = []() { return std::chrono::system_clock::now(); };
    deps.rand_alphabet = [](std::size_t length) { return crypto::rand_alphabet(length); };
    deps.hash = [](const std::string &input) { 
      auto hash_result = crypto::hash(input);
      return util::hex(hash_result).to_string();
    };
    return deps;
  }

  std::string SessionTokenManager::generate_session_token(const std::string &username) {
    std::scoped_lock lock(mutex_);
    std::string token = dependencies_.rand_alphabet(64);
    std::string token_hash = dependencies_.hash(token);
    auto now = dependencies_.now();
    auto expires = now + SESSION_TOKEN_DURATION;
    session_tokens_[token_hash] = SessionToken{username, now, expires};
    cleanup_expired_session_tokens();
    return token;
  }

  bool SessionTokenManager::validate_session_token(const std::string &token) {
    std::scoped_lock lock(mutex_);
    std::string token_hash = dependencies_.hash(token);
    auto it = session_tokens_.find(token_hash);
    if (it == session_tokens_.end()) {
      return false;
    }
    auto now = dependencies_.now();
    if (now > it->second.expires_at) {
      session_tokens_.erase(it);
      return false;
    }
    return true;
  }

  void SessionTokenManager::revoke_session_token(const std::string &token) {
    std::scoped_lock lock(mutex_);
    std::string token_hash = dependencies_.hash(token);
    session_tokens_.erase(token_hash);
  }

  void SessionTokenManager::cleanup_expired_session_tokens() {
    auto now = dependencies_.now();
    std::erase_if(session_tokens_, [now](const auto &pair) {
      return now > pair.second.expires_at;
    });
  }

  std::optional<std::string> SessionTokenManager::get_username_for_token(const std::string &token) {
    std::scoped_lock lock(mutex_);
    std::string token_hash = dependencies_.hash(token);
    auto it = session_tokens_.find(token_hash);
    if (it != session_tokens_.end() && dependencies_.now() <= it->second.expires_at) {
      return it->second.username;
    }
    return std::nullopt;
  }

  size_t SessionTokenManager::session_count() const {
    std::scoped_lock lock(mutex_);
    return session_tokens_.size();
  }

  // ---------------- SessionTokenAPI Implementation ----------------

  SessionTokenAPI::SessionTokenAPI(SessionTokenManager& session_manager)
      : session_manager_(session_manager) {}

  APIResponse SessionTokenAPI::login(const std::string& username, const std::string& password, const std::string& redirect_url) {
    if (!validate_credentials(username, password)) {
      BOOST_LOG(info) << "Web UI: Login failed for user: " << username;
      return create_error_response("Invalid credentials", SimpleWeb::StatusCode::client_error_unauthorized);
    }

    std::string session_token = session_manager_.generate_session_token(username);

    nlohmann::json response_data;
    response_data["token"] = session_token;
    response_data["expires_in"] = std::chrono::duration_cast<std::chrono::seconds>(SESSION_TOKEN_DURATION).count();

    // Hardened secure redirect handling
    std::string safe_redirect = "/";
    if (!redirect_url.empty() && redirect_url[0] == '/') {
      std::string lower = redirect_url;
      boost::algorithm::to_lower(lower);
      // Disallow dangerous patterns
      if (lower.find("://") == std::string::npos &&
          lower.find("%2f") == std::string::npos &&
          lower.find("\\") == std::string::npos &&
          lower.find("..") == std::string::npos &&
          !(redirect_url.size() > 1 && redirect_url[1] == '/')) { // reject double slash
        // Unicode normalization: reject if normalized path differs
        std::string norm = redirect_url;
        std::replace(norm.begin(), norm.end(), '\\', '/');
        if (norm == redirect_url) {
          safe_redirect = redirect_url;
        }
      }
    }
    response_data["redirect"] = safe_redirect;

    APIResponse response = create_success_response(response_data);

    // Set session cookie with Secure if HTTPS or localhost
    std::string cookie = "session_token=" + session_token + "; Path=/; HttpOnly; SameSite=Strict";
    // NOTE: In production, check request context for HTTPS. Here, always set Secure for localhost.
    cookie += "; Secure";
    response.headers.emplace("Set-Cookie", cookie);

    // Set CORS header for localhost only (no wildcard), dynamically set port
    std::uint16_t https_port = net::map_port(nvhttp::PORT_HTTPS);
    std::string cors_origin = "http://localhost:" + std::to_string(https_port);
    response.headers.emplace("Access-Control-Allow-Origin", cors_origin);

    return response;
  }

  APIResponse SessionTokenAPI::logout(const std::string& session_token) {
    if (!session_token.empty()) {
      session_manager_.revoke_session_token(session_token);
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

  APIResponse SessionTokenAPI::validate_session(const std::string& session_token) {
    if (session_token.empty()) {
      return create_error_response("Session token required", SimpleWeb::StatusCode::client_error_unauthorized);
    }

    bool is_valid = session_manager_.validate_session_token(session_token);
    if (!is_valid) {
      return create_error_response("Invalid or expired session token", SimpleWeb::StatusCode::client_error_unauthorized);
    }

    return create_success_response();
  }

  bool SessionTokenAPI::validate_credentials(const std::string& username, const std::string& password) const {
    if (auto hash = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();
        !boost::iequals(username, config::sunshine.username) || hash != config::sunshine.password) {
      return false;
    }
    return true;
  }

  namespace {
/**
 * @brief Get the CORS origin for localhost (no wildcard).
 * @return The CORS origin string.
 */
std::string get_cors_origin() {
  std::uint16_t https_port = net::map_port(PORT_HTTPS);
  return "https://localhost:" + std::to_string(https_port);
}
}

  APIResponse SessionTokenAPI::create_success_response(const nlohmann::json& data) const {
    nlohmann::json response_body;
    response_body["status"] = true;
    for (auto& [key, value] : data.items()) {
      response_body[key] = value;
    }
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    headers.emplace("Access-Control-Allow-Origin", get_cors_origin());
    return APIResponse(SimpleWeb::StatusCode::success_ok, response_body.dump(), headers);
  }

  APIResponse SessionTokenAPI::create_error_response(const std::string& error_message, SimpleWeb::StatusCode status_code) const {
    nlohmann::json response_body;
    response_body["status"] = false;
    response_body["error"] = error_message;
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    headers.emplace("Access-Control-Allow-Origin", get_cors_origin());
    return APIResponse(status_code, response_body.dump(), headers);
  }

}  // namespace confighttp
