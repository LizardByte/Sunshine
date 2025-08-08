#pragma once
/**
 * @file src/http_auth.h
 * @brief Declarations for HTTP authentication, API tokens, and session token management utilities.
 */
// standard includes
#include <chrono>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// local includes
#include "config.h"
#include "crypto.h"
#include "utility.h"

// platform includes
#include <boost/function.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <nlohmann/json.hpp>
#include <Simple-Web-Server/server_https.hpp>

namespace confighttp {
  using StatusCode = SimpleWeb::StatusCode;
  using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
  using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;

  struct AuthResult {
    bool ok;
    StatusCode code;
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
    /**
     * @brief Construct with a human readable message.
     * @param msg Explanation of the invalid scope condition.
     */
    explicit InvalidScopeException(const std::string &msg);
    /**
     * @brief Describe the error.
     * @return Null terminated message string.
     */
    const char *what() const noexcept override;

  private:
    std::string message_;
  };

  class ApiTokenManager {
  public:
    /**
     * @brief Construct a manager with injected dependencies.
     * @param dependencies Dependency functors (defaults to production implementations).
     */
    explicit ApiTokenManager(const ApiTokenManagerDependencies &dependencies = ApiTokenManager::make_default_dependencies());
    /**
     * @brief Authenticate a Bearer token for a given path and method.
     * @param rawAuth Raw Authorization header value (e.g. "Bearer <token>").
     * @param path Requested URI path.
     * @param method HTTP method (e.g. GET / POST ...).
     * @return `true` if authorized for the requested scope.
     */
    bool authenticate_bearer(std::string_view rawAuth, const std::string &path, const std::string &method);
    /**
     * @brief Generate a new API token from a creation request body.
     * @param request_body JSON string describing requested scopes.
     * @param username User who owns the token.
     * @return The plain token string (not hashed) or empty on failure.
     */
    std::optional<std::string> generate_api_token(const std::string &request_body, const std::string &username);
    /**
     * @brief Serialize the loaded API tokens to JSON.
     * @return JSON string listing tokens (hashed) and metadata.
     */
    std::string list_api_tokens_json() const;
    /**
     * @brief Revoke a token by its hash string.
     * @param token_hash Hash identifying the token.
     * @return Username owning the revoked token, or empty if not found.
     */
    std::optional<std::string> revoke_api_token(const std::string &token_hash) const;
    /**
     * @brief Authenticate using a raw token (not hash) for path & method.
     * @param token The un-hashed provided token.
     * @param path Target path.
     * @param method HTTP method.
     * @return `true` if token exists and includes the scope.
     */
    bool authenticate_token(const std::string &token, const std::string &path, const std::string &method);
    /**
     * @brief Create and store a token from structured JSON scopes.
     * @param scopes_json JSON specifying scopes (path->methods mapping).
     * @param username Owner username.
     * @return The plain token value, or empty if validation fails.
     */
    std::optional<std::string> create_api_token(const nlohmann::json &scopes_json, const std::string &username);
    /**
     * @brief Get a structured list of tokens.
     * @return JSON array/object of tokens and properties.
     */
    nlohmann::json get_api_tokens_list() const;
    /**
     * @brief Revoke a token by hash (mutable operation).
     * @param hash Hash of the token.
     * @return `true` if removed.
     */
    bool revoke_api_token_by_hash(const std::string &hash);
    /**
     * @brief Persist tokens to backing storage.
     */
    void save_api_tokens() const;
    /**
     * @brief Load tokens from backing storage.
     */
    void load_api_tokens();
    /**
     * @brief Provide default dependency functors.
     * @return Dependencies configured with production behaviors.
     */
    static ApiTokenManagerDependencies make_default_dependencies();
    /**
     * @brief Access currently loaded tokens (thread-safe for reading via mutex).
     * @return Map of token hash to token info.
     */
    const std::map<std::string, ApiTokenInfo, std::less<>> &retrieve_loaded_api_tokens() const;

  private:
    /**
     * @brief Parse scope JSON into internal map form.
     * @param scopes_json Incoming JSON object.
     * @return Scope map or empty on validation error.
     */
    std::optional<std::map<std::string, std::set<std::string, std::less<>>, std::less<>>>
      parse_json_scopes(const nlohmann::json &scopes_json) const;
    /**
     * @brief Parse a single scope entry from a property tree representation.
     * @param scope_tree Scope node tree.
     * @return Pair of path and method set or empty if invalid.
     */
    std::optional<std::pair<std::string, std::set<std::string, std::less<>>>> parse_scope(const boost::property_tree::ptree &scope_tree) const;
    /**
     * @brief Build scope mapping from PT subtree.
     * @param scopes_node Node containing scopes array/objects.
     * @return Map path->allowed methods set.
     */
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> build_scope_map(const boost::property_tree::ptree &scopes_node) const;
    ApiTokenManagerDependencies dependencies_;
    mutable std::mutex mutex_;
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
    /**
     * @brief Construct with injected dependencies.
     * @param dependencies Dependency functors (defaults available).
     */
    explicit SessionTokenManager(const SessionTokenManagerDependencies &dependencies = SessionTokenManager::make_default_dependencies());
    /**
     * @brief Create a new session token for a user.
     * @param username Account name.
     * @return Newly generated opaque token string.
     */
    std::string generate_session_token(const std::string &username);
    /**
     * @brief Validate a session token (and update internal state if expired).
     * @param token Opaque token string.
     * @return `true` if exists and not expired.
     */
    bool validate_session_token(const std::string &token);
    /**7
     * @brief Revoke (delete) a session token.
     * @param token Token to remove.
     */
    void revoke_session_token(const std::string &token);
    /**
     * @brief Remove all expired tokens.
     */
    void cleanup_expired_session_tokens();
    /**
     * @brief Lookup username for a token.
     * @param token Token string.
     * @return Username or empty if not found/expired.
     */
    std::optional<std::string> get_username_for_token(const std::string &token);
    /**
     * @brief Count active session tokens.
     * @return Number of stored (possibly unexpired) sessions.
     */
    size_t session_count() const;
    /**
     * @brief Create default dependency set.
     * @return Dependency functors.
     */
    static SessionTokenManagerDependencies make_default_dependencies();

  private:
    SessionTokenManagerDependencies dependencies_;
    mutable std::mutex mutex_;

    struct TransparentStringHash {
      using is_transparent = void;

      std::size_t operator()(std::string_view txt) const noexcept {
        return std::hash<std::string_view> {}(txt);
      }

      std::size_t operator()(const std::string &txt) const noexcept {
        return std::hash<std::string_view> {}(txt);
      }

      std::size_t operator()(const char *txt) const noexcept {
        return std::hash<std::string_view> {}(txt);
      }
    };

    std::unordered_map<std::string, SessionToken, TransparentStringHash, std::equal_to<>> session_tokens_;
  };

  struct APIResponse {
    StatusCode status_code;
    std::string body;
    SimpleWeb::CaseInsensitiveMultimap headers;

    /**
     * @brief Construct a response object.
     * @param code HTTP status code.
     * @param response_body Body payload.
     * @param response_headers Extra headers (case-insensitive multimap).
     */
    APIResponse(StatusCode code, std::string response_body = "", SimpleWeb::CaseInsensitiveMultimap response_headers = {}):
        status_code(code),
        body(std::move(response_body)),
        headers(std::move(response_headers)) {}
  };

  class SessionTokenAPI {
  public:
    /**
     * @brief Construct API facade referencing a session manager.
     * @param session_manager Session manager instance.
     */
    explicit SessionTokenAPI(SessionTokenManager &session_manager);
    /**
     * @brief Handle login request validating credentials and returning session token.
     * @param username User name.
     * @param password Plain password candidate.
     * @param redirect_url Optional redirect URL for HTML flows.
     * @return API response containing token or error.
     */
    APIResponse login(const std::string &username, const std::string &password, const std::string &redirect_url = "/");
    /**
     * @brief Invalidate the provided session token.
     * @param session_token Token to revoke.
     * @return Success or error response.
     */
    APIResponse logout(const std::string &session_token);
    /**
     * @brief Validate a session token for an active session.
     * @param session_token Token to validate.
     * @return Response containing validity state.
     */
    APIResponse validate_session(const std::string &session_token);

  private:
    SessionTokenManager &session_manager_;
    /**
     * @brief Validate supplied credentials (implementation-defined policy).
     * @param username Name.
     * @param password Password candidate.
     * @return `true` if credentials accepted.
     */
    bool validate_credentials(const std::string &username, const std::string &password) const;
    /**
     * @brief Build a success JSON response.
     * @param data Additional JSON payload.
     * @return Response with status 200 and JSON body.
     */
    APIResponse create_success_response(const nlohmann::json &data = {}) const;
    /**
     * @brief Build an error JSON response.
     * @param error_message Human readable error.
     * @param status_code HTTP status code (default 400).
     * @return Response with error payload.
     */
    APIResponse create_error_response(const std::string &error_message, StatusCode status_code = StatusCode::client_error_bad_request) const;
  };

  /**
   * @brief Validate basic auth header credentials.
   * @param rawAuth Raw Authorization header value.
   * @return `true` if credentials are valid.
   */
  bool authenticate_basic(const std::string_view rawAuth);
  /**
   * @brief Construct an AuthResult representing an authentication error.
   * @param code HTTP status to return.
   * @param error Message body.
   * @param add_www_auth Whether to add WWW-Authenticate header.
   * @param location Optional redirect location.
   * @return Structured AuthResult.
   */
  AuthResult make_auth_error(StatusCode code, const std::string &error, bool add_www_auth = false, const std::string &location = {});
  /**
   * @brief Check a Bearer token Authorization header.
   * @param rawAuth Header value.
   * @param path Requested path.
   * @param method HTTP method.
   * @return AuthResult with authorization outcome.
   */
  AuthResult check_bearer_auth(const std::string &rawAuth, const std::string &path, const std::string &method);
  /**
   * @brief Check a Basic auth header.
   * @param rawAuth Header value.
   * @return AuthResult with outcome.
   */
  AuthResult check_basic_auth(const std::string &rawAuth);
  /**
   * @brief Check session cookie / header authentication.
   * @param rawAuth Raw header or cookie string.
   * @return AuthResult with outcome.
   */
  AuthResult check_session_auth(const std::string &rawAuth);
  /**
   * @brief Determine whether a request path expects an HTML response.
   * @param path Requested URI path.
   * @return `true` if path indicates HTML content negotiation.
   */
  bool is_html_request(const std::string &path);
  /**
   * @brief Perform layered auth checks (session, bearer, basic) for a request.
   * @param remote_address Client IP / address.
   * @param auth_header Authorization header value.
   * @param path Request path.
   * @param method HTTP method.
   * @return AuthResult representing authentication decision.
   */
  AuthResult check_auth(const std::string &remote_address, const std::string &auth_header, const std::string &path, const std::string &method);
  /**
   * @brief Extract session token from Cookie headers.
   * @param headers Case-insensitive header multimap.
   * @return Token string (may be empty if not present).
   */
  std::string extract_session_token_from_cookie(const SimpleWeb::CaseInsensitiveMultimap &headers);
  extern ApiTokenManager apiTokenManager;
  extern SessionTokenManager sessionTokenManager;
  extern SessionTokenAPI sessionTokenAPI;

}  // namespace confighttp
