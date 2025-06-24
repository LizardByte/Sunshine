#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "src/http_auth.h"
#include <nlohmann/json.hpp>
#include <boost/property_tree/ptree.hpp>
#include <chrono>
#include <sstream>

using namespace confighttp;
using namespace testing;
namespace pt = boost::property_tree;

/**
 * @brief Mock dependencies for ApiTokenManager testing.
 */
class MockApiTokenManagerDependencies {
public:
    MOCK_METHOD(bool, file_exists, (const std::string&), (const));
    MOCK_METHOD(void, read_json, (const std::string&, pt::ptree&), (const));
    MOCK_METHOD(void, write_json, (const std::string&, const pt::ptree&), (const));
    MOCK_METHOD(std::chrono::system_clock::time_point, now, (), (const));
    MOCK_METHOD(std::string, rand_alphabet, (std::size_t), (const));
    MOCK_METHOD(std::string, hash, (const std::string&), (const));

    ApiTokenManagerDependencies create_dependencies() {
        ApiTokenManagerDependencies deps;
        deps.file_exists = [this](const std::string& path) { return file_exists(path); };
        deps.read_json = [this](const std::string& path, pt::ptree& tree) { read_json(path, tree); };
        deps.write_json = [this](const std::string& path, const pt::ptree& tree) { write_json(path, tree); };
        deps.now = [this]() { return now(); };
        deps.rand_alphabet = [this](std::size_t length) { return rand_alphabet(length); };
        deps.hash = [this](const std::string& input) { return hash(input); };
        return deps;
    }
};

class ApiTokenManagerTest : public Test {
protected:
    void SetUp() override {
        mock_deps = std::make_unique<MockApiTokenManagerDependencies>();
        deps = mock_deps->create_dependencies();
        manager = std::make_unique<ApiTokenManager>(deps);
        test_time = std::chrono::system_clock::now();
    }

    std::unique_ptr<MockApiTokenManagerDependencies> mock_deps;
    ApiTokenManagerDependencies deps;
    std::unique_ptr<ApiTokenManager> manager;
    std::chrono::system_clock::time_point test_time;
};

TEST_F(ApiTokenManagerTest, given_valid_token_and_matching_scope_when_authenticating_then_should_return_true) {
    // Given: A valid token with GET permission for /api/data path
    EXPECT_CALL(*mock_deps, hash("valid_token"))
        .WillOnce(Return("token_hash_123"));
    
    // Setup token in manager
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    path_methods["/api/data"] = {"GET", "POST"};
    ApiTokenInfo token_info{"token_hash_123", path_methods, "test_user", test_time};
    manager->api_tokens["token_hash_123"] = token_info;
    
    // When: Authenticating with valid token for allowed path and method
    bool result = manager->authenticate_token("valid_token", "/api/data", "GET");
    
    // Then: Authentication should succeed
    EXPECT_TRUE(result);
}

TEST_F(ApiTokenManagerTest, given_invalid_token_when_authenticating_then_should_return_false) {
    // Given: An invalid token that doesn't exist in the system
    EXPECT_CALL(*mock_deps, hash("invalid_token"))
        .WillOnce(Return("nonexistent_hash"));
    
    // When: Authenticating with invalid token
    bool result = manager->authenticate_token("invalid_token", "/api/data", "GET");
    
    // Then: Authentication should fail
    EXPECT_FALSE(result);
}

TEST_F(ApiTokenManagerTest, given_valid_token_but_wrong_method_when_authenticating_then_should_return_false) {
    // Given: A valid token with only GET permission
    EXPECT_CALL(*mock_deps, hash("valid_token"))
        .WillOnce(Return("token_hash_123"));
    
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    path_methods["/api/data"] = {"GET"};
    ApiTokenInfo token_info{"token_hash_123", path_methods, "test_user", test_time};
    manager->api_tokens["token_hash_123"] = token_info;
    
    // When: Authenticating with POST method (not allowed)
    bool result = manager->authenticate_token("valid_token", "/api/data", "POST");
    
    // Then: Authentication should fail
    EXPECT_FALSE(result);
}

TEST_F(ApiTokenManagerTest, given_valid_token_but_wrong_path_when_authenticating_then_should_return_false) {
    // Given: A valid token with permission for /api/data only
    EXPECT_CALL(*mock_deps, hash("valid_token"))
        .WillOnce(Return("token_hash_123"));
    
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    path_methods["/api/data"] = {"GET"};
    ApiTokenInfo token_info{"token_hash_123", path_methods, "test_user", test_time};
    manager->api_tokens["token_hash_123"] = token_info;
    
    // When: Accessing different path
    bool result = manager->authenticate_token("valid_token", "/api/admin", "GET");
    
    // Then: Authentication should fail
    EXPECT_FALSE(result);
}

TEST_F(ApiTokenManagerTest, given_token_with_regex_path_pattern_when_authenticating_matching_path_then_should_return_true) {
    // Given: A token with regex pattern for API paths
    EXPECT_CALL(*mock_deps, hash("valid_token"))
        .WillOnce(Return("token_hash_123"));
    
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    path_methods["^/api/.*"] = {"GET"};
    ApiTokenInfo token_info{"token_hash_123", path_methods, "test_user", test_time};
    manager->api_tokens["token_hash_123"] = token_info;
    
    // When: Accessing path that matches regex
    bool result = manager->authenticate_token("valid_token", "/api/users/123", "GET");
    
    // Then: Authentication should succeed
    EXPECT_TRUE(result);
}

TEST_F(ApiTokenManagerTest, given_case_insensitive_method_when_authenticating_then_should_return_true) {
    // Given: A token with uppercase GET method
    EXPECT_CALL(*mock_deps, hash("valid_token"))
        .WillOnce(Return("token_hash_123"));
    
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    path_methods["/api/data"] = {"GET"};
    ApiTokenInfo token_info{"token_hash_123", path_methods, "test_user", test_time};
    manager->api_tokens["token_hash_123"] = token_info;
    
    // When: Authenticating with lowercase method
    bool result = manager->authenticate_token("valid_token", "/api/data", "get");
    
    // Then: Authentication should succeed (case insensitive)
    EXPECT_TRUE(result);
}

TEST_F(ApiTokenManagerTest, given_valid_bearer_header_when_authenticating_then_should_return_true) {
    // Given: A valid bearer token in header format
    EXPECT_CALL(*mock_deps, hash("valid_token"))
        .WillOnce(Return("token_hash_123"));
    
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    path_methods["/api/data"] = {"GET"};
    ApiTokenInfo token_info{"token_hash_123", path_methods, "test_user", test_time};
    manager->api_tokens["token_hash_123"] = token_info;
    
    // When: Authenticating with Bearer header
    bool result = manager->authenticate_bearer("Bearer valid_token", "/api/data", "GET");
    
    // Then: Authentication should succeed
    EXPECT_TRUE(result);
}

TEST_F(ApiTokenManagerTest, given_invalid_bearer_header_format_when_authenticating_then_should_return_false) {
    // Given: Invalid bearer header formats
    
    // When & Then: Various invalid header formats should fail
    EXPECT_FALSE(manager->authenticate_bearer("", "/api/data", "GET"));
    EXPECT_FALSE(manager->authenticate_bearer("Bear token", "/api/data", "GET"));
    EXPECT_FALSE(manager->authenticate_bearer("Bearer", "/api/data", "GET"));
    EXPECT_FALSE(manager->authenticate_bearer("Basic token", "/api/data", "GET"));
}

TEST_F(ApiTokenManagerTest, given_valid_scopes_json_when_creating_token_then_should_return_token) {
    // Given: Valid scopes JSON and mock dependencies
    nlohmann::json scopes = nlohmann::json::array();
    scopes.push_back({{"path", "/api/data"}, {"methods", {"GET", "POST"}}});
    
    EXPECT_CALL(*mock_deps, rand_alphabet(32))
        .WillOnce(Return("generated_token_123"));
    EXPECT_CALL(*mock_deps, hash("generated_token_123"))
        .WillOnce(Return("token_hash_456"));
    EXPECT_CALL(*mock_deps, now())
        .WillOnce(Return(test_time));
    EXPECT_CALL(*mock_deps, file_exists(_))
        .WillOnce(Return(false));
    EXPECT_CALL(*mock_deps, write_json(_, _))
        .Times(1);
    
    // When: Creating API token
    auto result = manager->create_api_token(scopes, "test_user");
    
    // Then: Token should be created successfully
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "generated_token_123");
}

TEST_F(ApiTokenManagerTest, given_invalid_scopes_json_when_creating_token_then_should_return_nullopt) {
    // Given: Invalid scopes JSON (missing methods)
    nlohmann::json scopes = nlohmann::json::array();
    scopes.push_back({{"path", "/api/data"}});  // Missing methods
    
    // When: Creating API token with invalid scopes
    auto result = manager->create_api_token(scopes, "test_user");
    
    // Then: Token creation should fail
    EXPECT_FALSE(result.has_value());
}

TEST_F(ApiTokenManagerTest, given_scopes_with_missing_path_when_creating_token_then_should_return_nullopt) {
    // Given: Invalid scopes JSON (missing path)
    nlohmann::json scopes = nlohmann::json::array();
    scopes.push_back({{"methods", {"GET"}}});  // Missing path
    
    // When: Creating API token with invalid scopes
    auto result = manager->create_api_token(scopes, "test_user");
    
    // Then: Token creation should fail
    EXPECT_FALSE(result.has_value());
}

TEST_F(ApiTokenManagerTest, given_scopes_with_invalid_methods_format_when_creating_token_then_should_return_nullopt) {
    // Given: Invalid scopes JSON (methods not array)
    nlohmann::json scopes = nlohmann::json::array();
    scopes.push_back({{"path", "/api/data"}, {"methods", "GET"}});  // Methods should be array
    
    // When: Creating API token with invalid scopes
    auto result = manager->create_api_token(scopes, "test_user");
    
    // Then: Token creation should fail
    EXPECT_FALSE(result.has_value());
}

TEST_F(ApiTokenManagerTest, given_valid_request_body_when_generating_api_token_then_should_return_success_response) {
    // Given: Valid request body JSON
    nlohmann::json request_json;
    request_json["scopes"] = nlohmann::json::array();
    request_json["scopes"].push_back({{"path", "/api/data"}, {"methods", {"GET"}}});
    std::string request_body = request_json.dump();
    
    EXPECT_CALL(*mock_deps, rand_alphabet(32))
        .WillOnce(Return("generated_token_123"));
    EXPECT_CALL(*mock_deps, hash("generated_token_123"))
        .WillOnce(Return("token_hash_456"));
    EXPECT_CALL(*mock_deps, now())
        .WillOnce(Return(test_time));
    EXPECT_CALL(*mock_deps, file_exists(_))
        .WillOnce(Return(false));
    EXPECT_CALL(*mock_deps, write_json(_, _))
        .Times(1);
    
    // When: Generating API token
    auto result = manager->generate_api_token(request_body, "test_user");
    
    // Then: Should return success response with token
    ASSERT_TRUE(result.has_value());
    nlohmann::json response = nlohmann::json::parse(*result);
    EXPECT_TRUE(response.contains("token"));
    EXPECT_EQ(response["token"], "generated_token_123");
}

TEST_F(ApiTokenManagerTest, given_invalid_json_request_body_when_generating_api_token_then_should_return_error_response) {
    // Given: Invalid JSON request body
    std::string invalid_json = "{invalid json}";
    
    // When: Generating API token with invalid JSON
    auto result = manager->generate_api_token(invalid_json, "test_user");
    
    // Then: Should return error response
    ASSERT_TRUE(result.has_value());
    nlohmann::json response = nlohmann::json::parse(*result);
    EXPECT_TRUE(response.contains("error"));
    EXPECT_THAT(response["error"].get<std::string>(), HasSubstr("Invalid JSON"));
}

TEST_F(ApiTokenManagerTest, given_request_body_missing_scopes_when_generating_api_token_then_should_return_error_response) {
    // Given: Request body without scopes
    nlohmann::json request_json;
    request_json["other_field"] = "value";
    std::string request_body = request_json.dump();
    
    // When: Generating API token without scopes
    auto result = manager->generate_api_token(request_body, "test_user");
    
    // Then: Should return error response
    ASSERT_TRUE(result.has_value());
    nlohmann::json response = nlohmann::json::parse(*result);
    EXPECT_TRUE(response.contains("error"));
    EXPECT_EQ(response["error"], "Missing scopes array");
}

TEST_F(ApiTokenManagerTest, given_api_tokens_exist_when_listing_tokens_then_should_return_all_tokens) {
    // Given: Multiple API tokens in the manager
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods1;
    path_methods1["/api/data"] = {"GET"};
    ApiTokenInfo token1{"hash1", path_methods1, "user1", test_time};
    
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods2;
    path_methods2["/api/admin"] = {"POST"};
    ApiTokenInfo token2{"hash2", path_methods2, "user2", test_time};
    
    manager->api_tokens["hash1"] = token1;
    manager->api_tokens["hash2"] = token2;
    
    // When: Listing API tokens
    auto result = manager->get_api_tokens_list();
    
    // Then: Should return all tokens with correct structure
    EXPECT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), 2);
    
    // Verify token structure
    for (const auto& token : result) {
        EXPECT_TRUE(token.contains("hash"));
        EXPECT_TRUE(token.contains("username"));
        EXPECT_TRUE(token.contains("created_at"));
        EXPECT_TRUE(token.contains("scopes"));
        EXPECT_TRUE(token["scopes"].is_array());
    }
}

TEST_F(ApiTokenManagerTest, given_no_api_tokens_when_listing_tokens_then_should_return_empty_array) {
    // Given: No API tokens in the manager
    
    // When: Listing API tokens
    auto result = manager->get_api_tokens_list();
    
    // Then: Should return empty array
    EXPECT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), 0);
}

TEST_F(ApiTokenManagerTest, given_existing_token_hash_when_revoking_token_then_should_return_true) {
    // Given: An existing token
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    path_methods["/api/data"] = {"GET"};
    ApiTokenInfo token_info{"hash123", path_methods, "test_user", test_time};
    manager->api_tokens["hash123"] = token_info;
    
    EXPECT_CALL(*mock_deps, file_exists(_))
        .WillOnce(Return(false));
    EXPECT_CALL(*mock_deps, write_json(_, _))
        .Times(1);
    
    // When: Revoking the token
    bool result = manager->revoke_api_token_by_hash("hash123");
    
    // Then: Should return true and token should be removed
    EXPECT_TRUE(result);
    EXPECT_EQ(manager->api_tokens.find("hash123"), manager->api_tokens.end());
}

TEST_F(ApiTokenManagerTest, given_non_existent_token_hash_when_revoking_token_then_should_return_false) {
    // Given: No tokens in the manager
    
    // When: Revoking non-existent token
    bool result = manager->revoke_api_token_by_hash("nonexistent_hash");
    
    // Then: Should return false
    EXPECT_FALSE(result);
}

TEST_F(ApiTokenManagerTest, given_empty_token_hash_when_revoking_token_then_should_return_false) {
    // Given: Empty token hash
    
    // When: Revoking with empty hash
    bool result = manager->revoke_api_token_by_hash("");
    
    // Then: Should return false
    EXPECT_FALSE(result);
}

TEST_F(ApiTokenManagerTest, given_existing_token_when_revoking_via_api_then_should_return_success_response) {
    // Given: An existing token
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    path_methods["/api/data"] = {"GET"};
    ApiTokenInfo token_info{"hash123", path_methods, "test_user", test_time};
    manager->api_tokens["hash123"] = token_info;
    
    EXPECT_CALL(*mock_deps, file_exists(_))
        .WillOnce(Return(false));
    EXPECT_CALL(*mock_deps, write_json(_, _))
        .Times(1);
    
    // When: Revoking token via API
    auto result = manager->revoke_api_token("hash123");
    
    // Then: Should return success response
    ASSERT_TRUE(result.has_value());
    nlohmann::json response = nlohmann::json::parse(*result);
    EXPECT_TRUE(response.contains("status"));
    EXPECT_TRUE(response["status"].get<bool>());
}

TEST_F(ApiTokenManagerTest, given_non_existent_token_when_revoking_via_api_then_should_return_error_response) {
    // Given: No tokens in the manager
    
    // When: Revoking non-existent token via API
    auto result = manager->revoke_api_token("nonexistent_hash");
    
    // Then: Should return error response
    ASSERT_TRUE(result.has_value());
    nlohmann::json response = nlohmann::json::parse(*result);
    EXPECT_TRUE(response.contains("error"));
    EXPECT_EQ(response["error"], "Token not found");
}

TEST_F(ApiTokenManagerTest, given_file_exists_when_loading_tokens_then_should_load_tokens_from_file) {
    // Given: File exists and contains tokens
    EXPECT_CALL(*mock_deps, file_exists(_))
        .WillOnce(Return(true));
    
    EXPECT_CALL(*mock_deps, read_json(_, _))
        .WillOnce(Invoke([this](const std::string&, pt::ptree& tree) {
            // Create mock token data
            pt::ptree tokens_tree;
            pt::ptree token_tree;
            token_tree.put("hash", "test_hash");
            token_tree.put("username", "test_user");
            token_tree.put("created_at", 1234567890);
            
            pt::ptree scopes_tree;
            pt::ptree scope_tree;
            scope_tree.put("path", "/api/data");
            pt::ptree methods_tree;
            methods_tree.push_back({"", pt::ptree("GET")});
            scope_tree.add_child("methods", methods_tree);
            scopes_tree.push_back({"", scope_tree});
            token_tree.add_child("scopes", scopes_tree);
            
            tokens_tree.push_back({"", token_tree});
            tree.put_child("root.api_tokens", tokens_tree);
        }));
    
    // When: Loading tokens
    manager->load_api_tokens();
    
    // Then: Tokens should be loaded
    EXPECT_EQ(manager->api_tokens.size(), 1);
    EXPECT_TRUE(manager->api_tokens.contains("test_hash"));
    EXPECT_EQ(manager->api_tokens["test_hash"].username, "test_user");
}

TEST_F(ApiTokenManagerTest, given_file_does_not_exist_when_loading_tokens_then_should_not_load_any_tokens) {
    // Given: File does not exist
    EXPECT_CALL(*mock_deps, file_exists(_))
        .WillOnce(Return(false));
    
    // When: Loading tokens
    manager->load_api_tokens();
    
    // Then: No tokens should be loaded
    EXPECT_EQ(manager->api_tokens.size(), 0);
}

TEST_F(ApiTokenManagerTest, given_tokens_exist_when_saving_tokens_then_should_write_to_file) {
    // Given: Tokens exist in manager
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    path_methods["/api/data"] = {"GET"};
    ApiTokenInfo token_info{"test_hash", path_methods, "test_user", test_time};
    manager->api_tokens["test_hash"] = token_info;
    
    EXPECT_CALL(*mock_deps, file_exists(_))
        .WillOnce(Return(false));
    EXPECT_CALL(*mock_deps, write_json(_, _))
        .Times(1);
    
    // When: Saving tokens
    manager->save_api_tokens();
    
    // Then: File should be written (verified by mock expectation)
}

TEST_F(ApiTokenManagerTest, given_default_dependencies_when_creating_manager_then_should_work_correctly) {
    // Given: Default dependencies
    auto default_deps = ApiTokenManager::make_default_dependencies();
    
    // When: Creating manager with default dependencies
    ApiTokenManager default_manager(default_deps);
    
    // Then: Manager should be created successfully and dependencies should be valid
    EXPECT_NE(default_deps.file_exists, nullptr);
    EXPECT_NE(default_deps.read_json, nullptr);
    EXPECT_NE(default_deps.write_json, nullptr);
    EXPECT_NE(default_deps.now, nullptr);
    EXPECT_NE(default_deps.rand_alphabet, nullptr);
    EXPECT_NE(default_deps.hash, nullptr);
}

TEST_F(ApiTokenManagerTest, given_complex_regex_pattern_when_authenticating_then_should_match_correctly) {
    // Given: Token with complex regex pattern
    EXPECT_CALL(*mock_deps, hash("valid_token"))
        .WillRepeatedly(Return("token_hash_123"));
    
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    path_methods["^/api/v[0-9]+/users/[0-9]+$"] = {"GET"};
    ApiTokenInfo token_info{"token_hash_123", path_methods, "test_user", test_time};
    manager->api_tokens["token_hash_123"] = token_info;
    
    // When & Then: Should match valid paths
    EXPECT_TRUE(manager->authenticate_token("valid_token", "/api/v1/users/123", "GET"));
    EXPECT_TRUE(manager->authenticate_token("valid_token", "/api/v2/users/456", "GET"));
    
    // When & Then: Should not match invalid paths
    EXPECT_FALSE(manager->authenticate_token("valid_token", "/api/v1/users/abc", "GET"));
    EXPECT_FALSE(manager->authenticate_token("valid_token", "/api/v1/posts/123", "GET"));
}

TEST_F(ApiTokenManagerTest, given_multiple_scopes_in_token_when_authenticating_then_should_check_all_scopes) {
    // Given: Token with multiple scopes
    EXPECT_CALL(*mock_deps, hash("valid_token"))
        .WillRepeatedly(Return("token_hash_123"));
    
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    path_methods["/api/users"] = {"GET", "POST"};
    path_methods["/api/admin"] = {"DELETE"};
    ApiTokenInfo token_info{"token_hash_123", path_methods, "test_user", test_time};
    manager->api_tokens["token_hash_123"] = token_info;
    
    // When & Then: Should allow access to different scopes
    EXPECT_TRUE(manager->authenticate_token("valid_token", "/api/users", "GET"));
    EXPECT_TRUE(manager->authenticate_token("valid_token", "/api/users", "POST"));
    EXPECT_TRUE(manager->authenticate_token("valid_token", "/api/admin", "DELETE"));
    
    // When & Then: Should deny access to unauthorized combinations
    EXPECT_FALSE(manager->authenticate_token("valid_token", "/api/admin", "GET"));
    EXPECT_FALSE(manager->authenticate_token("valid_token", "/api/users", "DELETE"));
}



TEST_F(ApiTokenManagerTest, given_token_with_empty_pattern_when_applying_regex_then_should_handle_correctly) {
    // Given: Token with empty path pattern
    EXPECT_CALL(*mock_deps, hash("valid_token"))
        .WillOnce(Return("token_hash_123"));
    
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    path_methods[""] = {"GET"};  // Empty pattern
    ApiTokenInfo token_info{"token_hash_123", path_methods, "test_user", test_time};
    manager->api_tokens["token_hash_123"] = token_info;
    
    // When: Authenticating with empty pattern (should add ^ and $)
    bool result = manager->authenticate_token("valid_token", "", "GET");
    
    // Then: Should match empty path
    EXPECT_TRUE(result);
}

TEST_F(ApiTokenManagerTest, given_bearer_token_with_exact_minimum_length_when_authenticating_then_should_handle_correctly) {
    // Given: Bearer token with exact minimum length (7 chars = "Bearer ")
    std::string minimum_bearer = "Bearer ";  // Exactly 7 chars, no token
    
    // When: Authenticating with minimum length bearer header
    bool result = manager->authenticate_bearer(minimum_bearer, "/api/data", "GET");
    
    // Then: Should return false (no actual token)
    EXPECT_FALSE(result);
}

TEST_F(ApiTokenManagerTest, given_bearer_token_with_one_extra_character_when_authenticating_then_should_extract_token) {
    // Given: Bearer token with one character token
    EXPECT_CALL(*mock_deps, hash("x"))
        .WillOnce(Return("single_char_hash"));
    
    // When: Authenticating with single character token
    bool result = manager->authenticate_bearer("Bearer x", "/api/data", "GET");
    
    // Then: Should attempt authentication (will fail as token doesn't exist, but token was extracted)
    EXPECT_FALSE(result);  // Token doesn't exist in our test setup
}

TEST_F(ApiTokenManagerTest, given_pattern_starting_with_caret_when_authenticating_then_should_not_double_add_caret) {
    // Given: Token with pattern already starting with ^
    EXPECT_CALL(*mock_deps, hash("valid_token"))
        .WillOnce(Return("token_hash_123"));
    
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    path_methods["^/api/data$"] = {"GET"};  // Already has ^ and $
    ApiTokenInfo token_info{"token_hash_123", path_methods, "test_user", test_time};
    manager->api_tokens["token_hash_123"] = token_info;
    
    // When: Authenticating (should not double-add ^ and $)
    bool result = manager->authenticate_token("valid_token", "/api/data", "GET");
    
    // Then: Should match correctly
    EXPECT_TRUE(result);
}



TEST_F(ApiTokenManagerTest, given_token_with_no_path_methods_when_authenticating_then_should_return_false) {
    // Given: Token with empty path_methods map
    EXPECT_CALL(*mock_deps, hash("valid_token"))
        .WillOnce(Return("token_hash_123"));
    
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> empty_path_methods;
    ApiTokenInfo token_info{"token_hash_123", empty_path_methods, "test_user", test_time};
    manager->api_tokens["token_hash_123"] = token_info;
    
    // When: Authenticating with token that has no permissions
    bool result = manager->authenticate_token("valid_token", "/api/data", "GET");
    
    // Then: Should return false
    EXPECT_FALSE(result);
}

TEST_F(ApiTokenManagerTest, given_property_tree_with_malformed_token_data_when_loading_then_should_skip_malformed_entries) {
    // Given: File exists with malformed token data
    EXPECT_CALL(*mock_deps, file_exists(_))
        .WillOnce(Return(true));
    
    EXPECT_CALL(*mock_deps, read_json(_, _))
        .WillOnce(Invoke([](const std::string&, pt::ptree& tree) {
            pt::ptree tokens_tree;
            
            // Add a valid token
            pt::ptree valid_token;
            valid_token.put("hash", "valid_hash");
            valid_token.put("username", "valid_user");
            valid_token.put("created_at", 1234567890);
            pt::ptree valid_scopes;
            pt::ptree valid_scope;
            valid_scope.put("path", "/api/data");
            pt::ptree methods;
            methods.push_back({"", pt::ptree("GET")});
            valid_scope.add_child("methods", methods);
            valid_scopes.push_back({"", valid_scope});
            valid_token.add_child("scopes", valid_scopes);
            tokens_tree.push_back({"", valid_token});
            
            // Add malformed token (missing hash)
            pt::ptree malformed_token;
            malformed_token.put("username", "malformed_user");
            malformed_token.put("created_at", 1234567890);
            tokens_tree.push_back({"", malformed_token});
            
            tree.put_child("root.api_tokens", tokens_tree);
        }));
    
    // When: Loading tokens
    manager->load_api_tokens();
    
    // Then: Should load only valid token, skip malformed one
    EXPECT_EQ(manager->api_tokens.size(), 1);
    EXPECT_TRUE(manager->api_tokens.contains("valid_hash"));
}



TEST_F(ApiTokenManagerTest, given_large_number_of_tokens_when_listing_then_should_handle_efficiently) {
    // Given: Large number of tokens
    const size_t num_tokens = 1000;
    for (size_t i = 0; i < num_tokens; ++i) {
        std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
        path_methods["/api/data"] = {"GET"};
        ApiTokenInfo token_info{"hash" + std::to_string(i), path_methods, "user" + std::to_string(i), test_time};
        manager->api_tokens["hash" + std::to_string(i)] = token_info;
    }
    
    // When: Listing all tokens
    auto start = std::chrono::high_resolution_clock::now();
    auto result = manager->get_api_tokens_list();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Then: Should return all tokens efficiently (under 100ms for 1000 tokens)
    EXPECT_EQ(result.size(), num_tokens);
    EXPECT_LT(duration.count(), 100);  // Should be fast
}

TEST_F(ApiTokenManagerTest, given_method_with_mixed_case_when_stored_in_token_then_should_normalize_correctly) {
    // Given: Scopes with mixed case methods
    nlohmann::json scopes = nlohmann::json::array();
    scopes.push_back({{"path", "/api/data"}, {"methods", {"get", "Post", "DELETE"}}});
    
    EXPECT_CALL(*mock_deps, rand_alphabet(32))
        .WillOnce(Return("test_token"));
    EXPECT_CALL(*mock_deps, hash("test_token"))
        .WillOnce(Return("test_hash"));
    EXPECT_CALL(*mock_deps, now())
        .WillOnce(Return(test_time));
    EXPECT_CALL(*mock_deps, file_exists(_))
        .WillOnce(Return(false));
    EXPECT_CALL(*mock_deps, write_json(_, _))
        .Times(1);
    
    // When: Creating token with mixed case methods
    auto result = manager->create_api_token(scopes, "test_user");
    
    // Then: Methods should be normalized to uppercase
    ASSERT_TRUE(result.has_value());
    auto& token_info = manager->api_tokens["test_hash"];
    const auto& methods = token_info.path_methods["/api/data"];
    EXPECT_TRUE(methods.contains("GET"));
    EXPECT_TRUE(methods.contains("POST"));
    EXPECT_TRUE(methods.contains("DELETE"));
    EXPECT_EQ(methods.size(), 3);
}

TEST_F(ApiTokenManagerTest, given_invalid_scope_exception_during_parsing_when_creating_token_then_should_handle_gracefully) {
    // Given: Invalid scopes with missing required fields to trigger parsing failure
    nlohmann::json scopes = nlohmann::json::array();
    scopes.push_back({{"invalid", "scope"}});  // Missing required "path" and "methods" fields
    
    // When: Creating token with invalid scopes that will cause parsing failure
    auto result = manager->create_api_token(scopes, "test_user");
    
    // Then: Should return nullopt without crashing (invalid scope handled internally)
    EXPECT_FALSE(result.has_value());
}

TEST_F(ApiTokenManagerTest, given_json_response_methods_when_calling_list_api_tokens_json_then_should_return_valid_json_string) {
    // Given: Some tokens exist
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
    path_methods["/api/data"] = {"GET"};
    ApiTokenInfo token_info{"test_hash", path_methods, "test_user", test_time};
    manager->api_tokens["test_hash"] = token_info;
    
    // When: Getting JSON string representation
    std::string json_str = manager->list_api_tokens_json();
    
    // Then: Should return valid JSON string
    EXPECT_FALSE(json_str.empty());
    // Verify it's valid JSON by parsing it
    nlohmann::json parsed;
    EXPECT_NO_THROW(parsed = nlohmann::json::parse(json_str));
    EXPECT_TRUE(parsed.is_array());
}

TEST_F(ApiTokenManagerTest, given_scopes_with_non_string_methods_when_creating_token_then_should_handle_gracefully) {
    // Given: Scopes with invalid methods type (number instead of array)
    nlohmann::json scopes = nlohmann::json::array();
    nlohmann::json scope;
    scope["path"] = "/api/data";
    scope["methods"] = 123;  // Invalid: should be array, not number
    scopes.push_back(scope);
    
    // When: Creating token with invalid methods type via public interface
    auto result = manager->create_api_token(scopes, "test_user");
    
    // Then: Should handle gracefully and return nullopt (invalid methods format)
    EXPECT_FALSE(result.has_value());
}

TEST_F(ApiTokenManagerTest, given_scopes_with_empty_methods_array_when_creating_token_then_should_allow_it) {
    // Given: Scope with empty methods array
    nlohmann::json scopes = nlohmann::json::array();
    nlohmann::json scope;
    scope["path"] = "/api/data";
    scope["methods"] = nlohmann::json::array();  // Empty methods array
    scopes.push_back(scope);
    
    EXPECT_CALL(*mock_deps, rand_alphabet(32))
        .WillOnce(Return("test_token"));
    EXPECT_CALL(*mock_deps, hash("test_token"))
        .WillOnce(Return("test_hash"));
    EXPECT_CALL(*mock_deps, now())
        .WillOnce(Return(test_time));
    EXPECT_CALL(*mock_deps, file_exists(_))
        .WillOnce(Return(false));
    EXPECT_CALL(*mock_deps, write_json(_, _))
        .Times(1);
    
    // When: Creating token with empty methods via public interface
    auto result = manager->create_api_token(scopes, "test_user");
    
    // Then: Should succeed (empty methods array is technically valid)
    EXPECT_TRUE(result.has_value());
    
    // But the token should have no methods for the path
    ASSERT_TRUE(manager->api_tokens.contains("test_hash"));
    const auto& token_info = manager->api_tokens["test_hash"];
    EXPECT_TRUE(token_info.path_methods.contains("/api/data"));
    EXPECT_TRUE(token_info.path_methods.at("/api/data").empty());
}

TEST_F(ApiTokenManagerTest, given_request_with_missing_path_field_when_generating_token_then_should_return_error) {
    // Given: Request body with scope missing path field
    nlohmann::json request_json;
    request_json["scopes"] = nlohmann::json::array();
    nlohmann::json invalid_scope;
    // Missing "path" field entirely
    invalid_scope["methods"] = {"GET"};
    request_json["scopes"].push_back(invalid_scope);
    std::string request_body = request_json.dump();
    
    // When: Generating API token with invalid scope structure
    auto result = manager->generate_api_token(request_body, "test_user");
    
    // Then: Should return error response
    ASSERT_TRUE(result.has_value());
    nlohmann::json response = nlohmann::json::parse(*result);
    EXPECT_TRUE(response.contains("error"));
    EXPECT_EQ(response["error"], "Invalid scope value");
}

TEST_F(ApiTokenManagerTest, given_malformed_property_tree_during_loading_when_loading_tokens_then_should_handle_gracefully) {
    // Given: File exists with malformed scope data (empty methods should cause scope to be skipped)
    EXPECT_CALL(*mock_deps, file_exists(_))
        .WillOnce(Return(true));
    
    EXPECT_CALL(*mock_deps, read_json(_, _))
        .WillOnce(Invoke([](const std::string&, pt::ptree& tree) {
            pt::ptree tokens_tree;
            
            // Add a valid token first
            pt::ptree valid_token;
            valid_token.put("hash", "valid_hash");
            valid_token.put("username", "valid_user");
            valid_token.put("created_at", 1234567890);
            pt::ptree valid_scopes;
            pt::ptree valid_scope;
            valid_scope.put("path", "/api/data");
            pt::ptree valid_methods;
            valid_methods.push_back({"", pt::ptree("GET")});
            valid_scope.add_child("methods", valid_methods);
            valid_scopes.push_back({"", valid_scope});
            valid_token.add_child("scopes", valid_scopes);
            tokens_tree.push_back({"", valid_token});
            
            // Add a token with malformed scope (empty methods should cause scope to be skipped)
            pt::ptree malformed_token;
            malformed_token.put("hash", "malformed_hash");
            malformed_token.put("username", "malformed_user");
            malformed_token.put("created_at", 1234567890);
            pt::ptree malformed_scopes;
            pt::ptree malformed_scope;
            malformed_scope.put("path", "/api/data");
            pt::ptree empty_methods;  // Empty methods - should cause scope to be skipped
            malformed_scope.add_child("methods", empty_methods);
            malformed_scopes.push_back({"", malformed_scope});
            malformed_token.add_child("scopes", malformed_scopes);
            tokens_tree.push_back({"", malformed_token});
            
            tree.put_child("root.api_tokens", tokens_tree);
        }));
    
    // When: Loading tokens with malformed scope data
    manager->load_api_tokens();
    
    // Then: Should load both tokens, but malformed one should have no valid scopes
    EXPECT_EQ(manager->api_tokens.size(), 2);
    EXPECT_TRUE(manager->api_tokens.contains("valid_hash"));
    EXPECT_TRUE(manager->api_tokens.contains("malformed_hash"));
    
    // Valid token should have scopes
    EXPECT_FALSE(manager->api_tokens["valid_hash"].path_methods.empty());
    EXPECT_TRUE(manager->api_tokens["valid_hash"].path_methods.contains("/api/data"));
    
    // Malformed token should have no valid scopes (empty methods were rejected)
    EXPECT_TRUE(manager->api_tokens["malformed_hash"].path_methods.empty());
}
