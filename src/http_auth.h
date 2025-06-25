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

  /**
   * @brief Struct holding dependencies for I/O, clock, random, and hash operations.
   */
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

  /**
   * @brief Session token information.
   */
  struct SessionToken {
    std::string token;
    std::string username;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
  };

  /**
   * @brief Struct holding dependencies for session token management (clock, random).
   */
  struct SessionTokenManagerDependencies {
    boost::function<std::chrono::system_clock::time_point()> now;
    boost::function<std::string(std::size_t)> rand_alphabet;

    SessionTokenManagerDependencies() = default;
    SessionTokenManagerDependencies(const SessionTokenManagerDependencies &) = default;
    SessionTokenManagerDependencies &operator=(const SessionTokenManagerDependencies &) = default;
    SessionTokenManagerDependencies(SessionTokenManagerDependencies &&) noexcept = default;
    SessionTokenManagerDependencies &operator=(SessionTokenManagerDependencies &&) noexcept = default;
  };

  /**
   * @class SessionTokenManager
   * @brief Manages session tokens for user authentication.
   */
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
    static constexpr std::chrono::hours SESSION_TOKEN_DURATION{24};
  };

  /**
   * @brief Generic API response structure for HTTP responses.
   *
   * This structure encapsulates all the data needed to write an HTTP response,
   * allowing business logic to be decoupled from the HTTP transport layer.
   */
  struct APIResponse {
    SimpleWeb::StatusCode status_code;
    std::string body;
    SimpleWeb::CaseInsensitiveMultimap headers;
    
    APIResponse(SimpleWeb::StatusCode code, std::string response_body = "", SimpleWeb::CaseInsensitiveMultimap response_headers = {})
        : status_code(code), body(std::move(response_body)), headers(std::move(response_headers)) {}
  };

  /**
   * @brief Session token management API abstraction.
   *
   * This class provides a clean API for session token operations that returns
   * structured responses instead of directly manipulating HTTP request/response objects.
   * This design improves testability by removing hard-to-mock dependencies.
   */
  class SessionTokenAPI {
  public:
    explicit SessionTokenAPI(SessionTokenManager& session_manager);

    /**
     * @brief Process user login request.
     * @param username The username for authentication.
     * @param password The password for authentication.
     * @param redirect_url Optional redirect URL after successful login.
     * @return APIResponse containing login result and session token if successful.
     */
    APIResponse login(const std::string& username, const std::string& password, const std::string& redirect_url = "/");

    /**
     * @brief Process user logout request.
     * @param session_token The session token to revoke.
     * @return APIResponse containing logout result.
     */
    APIResponse logout(const std::string& session_token);

    /**
     * @brief Process session token refresh request.
     * @param old_token The existing session token to refresh.
     * @return APIResponse containing new session token if successful.
     */
    APIResponse refresh_token(const std::string& old_token);

    /**
     * @brief Validate session token authentication.
     * @param session_token The session token to validate.
     * @return APIResponse containing validation result.
     */
    APIResponse validate_session(const std::string& session_token);

  private:
    SessionTokenManager& session_manager_;
    static constexpr std::chrono::hours SESSION_TOKEN_DURATION{24};

    /**
     * @brief Validate user credentials against configuration.
     * @param username The username to validate.
     * @param password The password to validate.
     * @return True if credentials are valid, false otherwise.
     */
    bool validate_credentials(const std::string& username, const std::string& password) const;

    /**
     * @brief Create a successful JSON response.
     * @param data Additional data to include in the response.
     * @return APIResponse with success status and JSON body.
     */
    APIResponse create_success_response(const nlohmann::json& data = {}) const;

    /**
     * @brief Create an error JSON response.
     * @param error_message The error message to include.
     * @param status_code The HTTP status code (default: 400 Bad Request).
     * @return APIResponse with error status and JSON body.
     */
    APIResponse create_error_response(const std::string& error_message, SimpleWeb::StatusCode status_code = SimpleWeb::StatusCode::client_error_bad_request) const;
  };

}  // namespace confighttp
