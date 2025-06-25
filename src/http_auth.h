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
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <Simple-Web-Server/server_https.hpp>
#include <string>
#include <string_view>
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
}  // namespace confighttp
