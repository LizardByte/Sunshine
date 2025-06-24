#ifndef HTTP_AUTH_H
#define HTTP_AUTH_H

#include <chrono>
#include <exception>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <boost/property_tree/ptree.hpp>

namespace confighttp {

/**
 * @brief Exception thrown for invalid API token scopes.
 */
class InvalidScopeException : public std::exception {
  public:
    explicit InvalidScopeException(const std::string &msg);

    const char *what() const noexcept override;

  private:
    std::string message_;
};

/**
 * @brief Struct representing an API token's information.
 */
struct ApiTokenInfo {
    std::string hash;
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    std::string username;
    std::chrono::system_clock::time_point created_at;
};

/**
 * @brief Dependencies required by ApiTokenManager, can be mocked for testability.
 */
struct ApiTokenManagerDependencies {
    std::function<bool(const std::string &)> file_exists;
    std::function<void(const std::string &, boost::property_tree::ptree &)> read_json;
    std::function<void(const std::string &, const boost::property_tree::ptree &)> write_json;
    std::function<std::chrono::system_clock::time_point()> now;
    std::function<std::string(std::size_t)> rand_alphabet;
    std::function<std::string(const std::string &)> hash;
};

/**
 * @brief Manages API token creation, validation, and storage.
 */
class ApiTokenManager {
  public:
    explicit ApiTokenManager(const ApiTokenManagerDependencies &dependencies);

    /**
     * @brief Checks if a bearer token is valid for the request.
     * @param rawAuth The raw Authorization header value.
     * @param path The request path being accessed.
     * @param method The HTTP method (e.g., "GET", "POST").
     * @param username Optional username to check for token ownership.
     * @return true if authenticated, false otherwise.
     */
    bool authenticate_bearer(std::string_view rawAuth, std::string_view path, std::string_view method,
                             std::optional<std::string_view> username = std::nullopt);

    /**
     * @brief Generates a new API token for a user.
     * @param username The username for whom the token is generated.
     * @param scopes The map of path to allowed HTTP methods for the token.
     * @return The generated token string, or std::nullopt on failure.
     */
    std::optional<std::string> generateApiToken(
        const std::string &username,
        const std::map<std::string, std::set<std::string, std::less<>>, std::less<>> &scopes);

    /**
     * @brief Lists all API tokens for a user.
     * @param username The username whose tokens to list.
     * @return A vector of ApiTokenInfo for the user.
     */
    std::vector<ApiTokenInfo> listApiTokens(const std::string &username) const;

    /**
     * @brief Revokes an API token for a user.
     * @param username The username whose token is to be revoked.
     * @param token The token string to revoke.
     * @return True if the token was revoked, false if not found.
     */
    bool revokeApiToken(const std::string &username, const std::string &token);

    /**
     * @brief Helper to parse scopes from JSON for testability.
     * @param scopes_json The JSON array of scopes.
     * @param response The HTTPS response object (for error reporting).
     * @return Optional map of path to allowed methods.
     */
    std::optional<std::map<std::string, std::set<std::string, std::less<>>, std::less<>>>
    ApiTokenManager::parse_scopes_json(const nlohmann::json &scopes_json, resp_https_t response);

  private:
    ApiTokenManagerDependencies dependencies_;
    std::map<std::string, ApiTokenInfo> api_tokens;

    /**
     * @brief Saves all API tokens to persistent storage.
     */
    void save_api_tokens();

    /**
     * @brief Parses a scope from a property tree.
     */
    std::optional<std::pair<std::string, std::set<std::string, std::less<>>>> ApiTokenManager::parse_scope(
        const boost::property_tree::ptree &scope_tree);

    /**
     * @brief Builds a map of scopes from a property tree.
     */
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> ApiTokenManager::build_scope_map(
        const boost::property_tree::ptree &scopes_node);

    /**
     * @brief Loads API tokens from persistent storage.
     */
    void load_api_tokens();

    /**
     * @brief Creates and returns the default delegates for ApiTokenManager.
     */
    static ApiTokenManagerDependencies make_default_dependencies();
};

} // namespace confighttp

#endif // HTTP_AUTH_H