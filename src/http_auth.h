#pragma once
#include <string>
#include <string_view>
#include <map>
#include <set>
#include <chrono>
#include <optional>
#include <nlohmann/json.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <filesystem>
#include <exception>
#include <memory>
#include <vector>
#include <Simple-Web-Server/server_https.hpp>
#include "config.h"
#include "crypto.h"
#include "utility.h"
#include <boost/function.hpp>

namespace confighttp {
    using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
    using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;

    /**
     * @brief Struct holding dependencies for I/O, clock, random, and hash operations.
     */
    struct ApiTokenManagerDependencies {
        boost::function<bool(const std::string&)> file_exists;
        boost::function<void(const std::string&, boost::property_tree::ptree&)> read_json;
        boost::function<void(const std::string&, const boost::property_tree::ptree&)> write_json;
        boost::function<std::chrono::system_clock::time_point()> now;
        boost::function<std::string(std::size_t)> rand_alphabet;
        boost::function<std::string(const std::string&)> hash;
    };

    struct ApiTokenInfo {
        std::string hash;
        std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
        std::string username;
        std::chrono::system_clock::time_point created_at;
    };    class InvalidScopeException : public std::exception {
    public:
        explicit InvalidScopeException(const std::string &msg);
        const char *what() const noexcept override;
    private:
        std::string message_;
    };

    class ApiTokenManager {
    public:
        /**
         * @brief Constructs an ApiTokenManager with dependencies for testability.
         * @param dependencies Struct of function dependencies for I/O, clock, random, and hash operations.
         */
        explicit ApiTokenManager(const ApiTokenManagerDependencies& dependencies = ApiTokenManager::make_default_dependencies());
          // HTTP interface methods (now using primitives)
        bool authenticate_bearer(std::string_view rawAuth, const std::string& path, const std::string& method);
        std::optional<std::string> generate_api_token(const std::string& request_body, const std::string& username);
        std::string list_api_tokens_json();
        std::optional<std::string> revoke_api_token(const std::string& token_hash);
        
        // Core business logic methods (testable with primitives)
        bool authenticate_token(const std::string& token, const std::string& path, const std::string& method);
        std::optional<std::string> create_api_token(const nlohmann::json& scopes_json, const std::string& username);
        nlohmann::json get_api_tokens_list();
        bool revoke_api_token_by_hash(const std::string& hash);
          void save_api_tokens();
        void load_api_tokens();

    private:
        std::optional<std::pair<std::string, std::set<std::string, std::less<>>>> parse_scope(const boost::property_tree::ptree &scope_tree);
        std::map<std::string, std::set<std::string, std::less<>>, std::less<>> build_scope_map(const boost::property_tree::ptree &scopes_node);
        
        /**
         * @brief Helper to parse scopes from JSON for testability.
         * @param scopes_json The JSON array of scopes.
         * @return Optional map of path to allowed methods, or error message.
         */
        std::optional<std::map<std::string, std::set<std::string, std::less<>>, std::less<>>>
        parse_scopes_json_core(const nlohmann::json &scopes_json);
        
        ApiTokenManagerDependencies dependencies_;
    public:
        std::map<std::string, ApiTokenInfo> api_tokens;
        /**
         * @brief Returns a set of default dependencies using boost and std facilities.
         */
        static ApiTokenManagerDependencies make_default_dependencies();
    };
}
