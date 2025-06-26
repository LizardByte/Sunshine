#pragma once
#include "config.h"
#include "crypto.h"
#include "utility.h"

#include <boost/function.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <chrono>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <Simple-Web-Server/server_https.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace confighttp {
  using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
  using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;

  struct AuthResult {
    bool ok;
    SimpleWeb::StatusCode code;
    std::string body;
    SimpleWeb::CaseInsensitiveMultimap headers;
  };

  struct ApiTokenManagerDependencies {
    boost::function<bool(const std::string &)> file_exists;
    boost::function<void(const std::string &, boost::property_tree::ptree &)> read_json;
    boost::function<void(const std::string &, const boost::property_tree::ptree &)> write_json;
    boost::function<std::chrono::system_clock::time_point()> now;
    boost::function<std::string(std::size_t)> rand_alphabet;
    boost::function<std::string(const std::string &)> hash;
    ApiTokenManagerDependencies() = default;
    ApiTokenManagerDependencies(const ApiTokenManagerDependencies &) = default;
    ApiTokenManagerDependencies &operator=(const ApiTokenManagerDependencies &) = default;
    ApiTokenManagerDependencies(ApiTokenManagerDependencies &&) noexcept = default;
    ApiTokenManagerDependencies &operator=(ApiTokenManagerDependencies &&) noexcept = default;
  };

  struct ApiTokenInfo {
    std::string hash;
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    std::string username;
    std::chrono::system_clock::time_point created_at;
  };

  class InvalidScopeException: public std::exception {
  public:
    explicit InvalidScopeException(const std::string &msg);
    const char *what() const noexcept override;
  private:
    std::string message_;
  };

  class ApiTokenManager {
  public:
    explicit ApiTokenManager(const ApiTokenManagerDependencies &dependencies = ApiTokenManager::make_default_dependencies());
    bool authenticate_bearer(std::string_view rawAuth, const std::string &path, const std::string &method);
    std::optional<std::string> generate_api_token(const std::string &request_body, const std::string &username);
    std::string list_api_tokens_json() const;
    std::optional<std::string> revoke_api_token(const std::string &token_hash) const;
    bool authenticate_token(const std::string &token, const std::string &path, const std::string &method);
    std::optional<std::string> create_api_token(const nlohmann::json &scopes_json, const std::string &username);
    nlohmann::json get_api_tokens_list() const;
    bool revoke_api_token_by_hash(const std::string &hash);
    void save_api_tokens() const;
    void load_api_tokens();
    static ApiTokenManagerDependencies make_default_dependencies();
    const std::map<std::string, ApiTokenInfo, std::less<>> &retrieve_loaded_api_tokens() const;
  private:
    std::optional<std::map<std::string, std::set<std::string, std::less<>>, std::less<>>>
      parse_json_scopes(const nlohmann::json &scopes_json) const;
    std::optional<std::pair<std::string, std::set<std::string, std::less<>>>> parse_scope(const boost::property_tree::ptree &scope_tree) const;
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> build_scope_map(const boost::property_tree::ptree &scopes_node) const;
    ApiTokenManagerDependencies dependencies_;
    std::map<std::string, ApiTokenInfo, std::less<>> api_tokens;
  };

  struct SessionToken {
    std::string username;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
  };

  struct SessionTokenManagerDependencies {
    boost::function<std::chrono::system_clock::time_point()> now;
    boost::function<std::string(std::size_t)> rand_alphabet;
    boost::function<std::string(const std::string &)> hash;
    SessionTokenManagerDependencies() = default;
    SessionTokenManagerDependencies(const SessionTokenManagerDependencies &) = default;
    SessionTokenManagerDependencies &operator=(const SessionTokenManagerDependencies &) = default;
    SessionTokenManagerDependencies(SessionTokenManagerDependencies &&) noexcept = default;
    SessionTokenManagerDependencies &operator=(SessionTokenManagerDependencies &&) noexcept = default;
  };

  class SessionTokenManager {
  public:
    explicit SessionTokenManager(const SessionTokenManagerDependencies &dependencies = SessionTokenManager::make_default_dependencies());
    std::string generate_session_token(const std::string &username);
    bool validate_session_token(const std::string &token);
    void revoke_session_token(const std::string &token);
    void cleanup_expired_session_tokens();
    std::optional<std::string> get_username_for_token(const std::string &token);
    size_t session_count() const;
    static SessionTokenManagerDependencies make_default_dependencies();
  private:
    SessionTokenManagerDependencies dependencies_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SessionToken> session_tokens_;
  };

  struct APIResponse {
    SimpleWeb::StatusCode status_code;
    std::string body;
    SimpleWeb::CaseInsensitiveMultimap headers;
    APIResponse(SimpleWeb::StatusCode code, std::string response_body = "", SimpleWeb::CaseInsensitiveMultimap response_headers = {}):
        status_code(code),
        body(std::move(response_body)),
        headers(std::move(response_headers)) {}
  };

  class SessionTokenAPI {
  public:
    explicit SessionTokenAPI(SessionTokenManager &session_manager);
    APIResponse login(const std::string &username, const std::string &password, const std::string &redirect_url = "/");
    APIResponse logout(const std::string &session_token);
    APIResponse validate_session(const std::string &session_token);
  private:
    SessionTokenManager &session_manager_;
    static constexpr std::chrono::hours SESSION_TOKEN_DURATION {2};
    bool validate_credentials(const std::string &username, const std::string &password) const;
    APIResponse create_success_response(const nlohmann::json &data = {}) const;
    APIResponse create_error_response(const std::string &error_message, SimpleWeb::StatusCode status_code = SimpleWeb::StatusCode::client_error_bad_request) const;
  };

  bool authenticate_basic(const std::string_view rawAuth);
  AuthResult make_auth_error(SimpleWeb::StatusCode code, const std::string &error, bool add_www_auth = false, const std::string &location = {});
  AuthResult check_bearer_auth(const std::string &rawAuth, const std::string &path, const std::string &method);
  AuthResult check_basic_auth(const std::string &rawAuth);
  AuthResult check_session_auth(const std::string &rawAuth);
  bool is_html_request(const std::string &path);
  AuthResult check_auth(const std::string &remote_address, const std::string &auth_header, 
                       const std::string &path, const std::string &method);
  std::string extract_session_token_from_cookie(const SimpleWeb::CaseInsensitiveMultimap &headers);
  extern ApiTokenManager apiTokenManager;
  extern SessionTokenManager sessionTokenManager;
  extern SessionTokenAPI sessionTokenAPI;

}  // namespace confighttp
