#include "src/http_auth.h"

#include <boost/property_tree/ptree.hpp>
#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace confighttp;
using namespace testing;
namespace pt = boost::property_tree;

/**
 * @brief Mock dependencies for ApiTokenManager testing.
 */
class MockApiTokenManagerDependencies {
public:
  MOCK_METHOD(bool, file_exists, (const std::string &), (const));
  MOCK_METHOD(void, read_json, (const std::string &, pt::ptree &), (const));
  MOCK_METHOD(void, write_json, (const std::string &, const pt::ptree &), (const));
  MOCK_METHOD(std::chrono::system_clock::time_point, now, (), (const));
  MOCK_METHOD(std::string, rand_alphabet, (std::size_t), (const));
  MOCK_METHOD(std::string, hash, (const std::string &), (const));

  ApiTokenManagerDependencies create_dependencies() const {
    ApiTokenManagerDependencies deps;
    deps.file_exists = [this](const std::string &path) {
      return file_exists(path);
    };
    deps.read_json = [this](const std::string &path, pt::ptree &tree) {
      read_json(path, tree);
    };
    deps.write_json = [this](const std::string &path, const pt::ptree &tree) {
      write_json(path, tree);
    };
    deps.now = [this]() {
      return now();
    };
    deps.rand_alphabet = [this](std::size_t length) {
      return rand_alphabet(length);
    };
    deps.hash = [this](const std::string &input) {
      return hash(input);
    };
    return deps;
  }
};

class ApiTokenManagerTest: public Test {
protected:
  void SetUp() override {
    mock_deps = std::make_unique<MockApiTokenManagerDependencies>();
    deps = mock_deps->create_dependencies();
    manager = std::make_unique<ApiTokenManager>(deps);
    test_time = std::chrono::system_clock::now();
  }

public:
  /**
   * @brief Helper to fill a ptree with a single token.
   */
  static void FillPtreeWithToken(const ApiTokenInfo &token_info, pt::ptree &tree) {
    pt::ptree tokens_tree;
    pt::ptree token_tree;
    token_tree.put("hash", token_info.hash);
    token_tree.put("username", token_info.username);
    auto time_t_value = std::chrono::system_clock::to_time_t(token_info.created_at);
    token_tree.put("created_at", time_t_value);
    pt::ptree scopes_tree;
    for (const auto &[path, methods] : token_info.path_methods) {
      pt::ptree scope_tree;
      scope_tree.put("path", path);
      pt::ptree methods_tree;
      for (const auto &method : methods) {
        methods_tree.push_back({"", pt::ptree(method)});
      }
      scope_tree.add_child("methods", methods_tree);
      scopes_tree.push_back({"", scope_tree});
    }
    token_tree.add_child("scopes", scopes_tree);
    tokens_tree.push_back({"", token_tree});
    tree.put_child("root.api_tokens", tokens_tree);
  }

  void InjectToken(const ApiTokenInfo &token_info) {
    EXPECT_CALL(*mock_deps, file_exists(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_deps, read_json(_, _)).WillOnce(Invoke([token_info](const std::string &, pt::ptree &tree) {
      FillPtreeWithToken(token_info, tree);
    }));
    manager->load_api_tokens();
  }

  /**
   * @brief Helper to fill a ptree with multiple tokens.
   */
  static void FillPtreeWithMultipleTokens(const std::vector<std::pair<std::string, ApiTokenInfo>> &tokens, pt::ptree &tree) {
    pt::ptree tokens_tree;
    for (const auto &[hash, token_info] : tokens) {
      pt::ptree token_tree;
      token_tree.put("hash", token_info.hash);
      token_tree.put("username", token_info.username);
      auto time_t_value = std::chrono::system_clock::to_time_t(token_info.created_at);
      token_tree.put("created_at", time_t_value);
      pt::ptree scopes_tree;
      for (const auto &[path, methods] : token_info.path_methods) {
        pt::ptree scope_tree;
        scope_tree.put("path", path);
        pt::ptree methods_tree;
        for (const auto &method : methods) {
          methods_tree.push_back({"", pt::ptree(method)});
        }
        scope_tree.add_child("methods", methods_tree);
        scopes_tree.push_back({"", scope_tree});
      }
      token_tree.add_child("scopes", scopes_tree);
      tokens_tree.push_back({"", token_tree});
    }
    tree.put_child("root.api_tokens", tokens_tree);
  }

  void InjectMultipleTokens(const std::vector<std::pair<std::string, ApiTokenInfo>> &tokens) {
    EXPECT_CALL(*mock_deps, file_exists(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_deps, read_json(_, _)).WillOnce(Invoke([tokens](const std::string &, pt::ptree &tree) {
      FillPtreeWithMultipleTokens(tokens, tree);
    }));
    manager->load_api_tokens();
  }

  /**
   * @brief Helper to fill a ptree with a large number of tokens.
   */
  static void FillPtreeWithLargeNumberOfTokens(size_t num_tokens, const std::chrono::system_clock::time_point &test_time, pt::ptree &tree) {
    pt::ptree tokens_tree;
    for (size_t i = 0; i < num_tokens; ++i) {
      pt::ptree token_tree;
      std::string hash = std::format("hash{}", i);
      std::string username = std::format("user{}", i);
      token_tree.put("hash", hash);
      token_tree.put("username", username);
      auto time_t_value = std::chrono::system_clock::to_time_t(test_time);
      token_tree.put("created_at", time_t_value);
      pt::ptree scopes_tree;
      pt::ptree scope_tree;
      scope_tree.put("path", "/api/data");
      pt::ptree methods_tree;
      methods_tree.push_back({"", pt::ptree("GET")});
      scope_tree.add_child("methods", methods_tree);
      scopes_tree.push_back({"", scope_tree});
      token_tree.add_child("scopes", scopes_tree);
      tokens_tree.push_back({"", token_tree});
    }
    tree.put_child("root.api_tokens", tokens_tree);
  }

  void InjectLargeNumberOfTokens(size_t num_tokens) {
    EXPECT_CALL(*mock_deps, file_exists(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_deps, read_json(_, _)).WillOnce(Invoke([num_tokens, this](const std::string &, pt::ptree &tree) {
      FillPtreeWithLargeNumberOfTokens(num_tokens, test_time, tree);
    }));
    manager->load_api_tokens();
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
  ApiTokenInfo token_info {"token_hash_123", path_methods, "test_user", test_time};
  InjectToken(token_info);

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
  ApiTokenInfo token_info {"token_hash_123", path_methods, "test_user", test_time};
  InjectToken(token_info);

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
  ApiTokenInfo token_info {"token_hash_123", path_methods, "test_user", test_time};
  InjectToken(token_info);

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
  ApiTokenInfo token_info {"token_hash_123", path_methods, "test_user", test_time};
  InjectToken(token_info);

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
  ApiTokenInfo token_info {"token_hash_123", path_methods, "test_user", test_time};
  InjectToken(token_info);

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
  ApiTokenInfo token_info {"token_hash_123", path_methods, "test_user", test_time};
  InjectToken(token_info);

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
  ApiTokenInfo token1 {"hash1", path_methods1, "user1", test_time};

  std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods2;
  path_methods2["/api/admin"] = {"POST"};
  ApiTokenInfo token2 {"hash2", path_methods2, "user2", test_time};

  std::vector<std::pair<std::string, ApiTokenInfo>> tokens = {
    {"hash1", token1},
    {"hash2", token2}
  };
  InjectMultipleTokens(tokens);

  // When: Listing API tokens
  auto result = manager->get_api_tokens_list();

  // Then: Should return all tokens with correct structure
  EXPECT_TRUE(result.is_array());
  EXPECT_EQ(result.size(), 2);

  // Verify token structure
  for (const auto &token : result) {
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
  ApiTokenInfo token_info {"hash123", path_methods, "test_user", test_time};
  InjectToken(token_info);

  EXPECT_CALL(*mock_deps, file_exists(_))
    .WillOnce(Return(false));
  EXPECT_CALL(*mock_deps, write_json(_, _))
    .Times(1);

  // When: Revoking the token
  bool result = manager->revoke_api_token_by_hash("hash123");

  // Then: Should return true and token should be removed
  EXPECT_TRUE(result);
  const auto &tokens = manager->retrieve_loaded_api_tokens();
  EXPECT_EQ(tokens.find("hash123"), tokens.end());
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
  ApiTokenInfo token_info {"hash123", path_methods, "test_user", test_time};
  InjectToken(token_info);

  EXPECT_CALL(*mock_deps, file_exists(_))
    .WillOnce(Return(false));
  EXPECT_CALL(*mock_deps, write_json(_, _))
    .Times(1);

  // When: Revoking token via API
  bool result = manager->revoke_api_token_by_hash("hash123");

  // Then: Should return success response
  ASSERT_TRUE(result);
}

TEST_F(ApiTokenManagerTest, given_non_existent_token_when_revoking_via_api_then_should_return_error_response) {
  // Given: No tokens in the manager

  // When: Revoking non-existent token via API
  bool result = manager->revoke_api_token_by_hash("nonexistent_hash");

  // Then: Should return false
  EXPECT_FALSE(result);
}

TEST_F(ApiTokenManagerTest, given_file_exists_when_loading_tokens_then_should_load_tokens_from_file) {
  // Given: File exists and contains tokens
  EXPECT_CALL(*mock_deps, file_exists(_))
    .WillOnce(Return(true));

  // Lambda to fill ptree with mock token data for this test
  auto FillPtreeWithMockTokenData = [](pt::ptree &tree) {
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
  };

  EXPECT_CALL(*mock_deps, read_json(_, _))
    .WillOnce(Invoke([FillPtreeWithMockTokenData](const std::string &, pt::ptree &tree) {
      FillPtreeWithMockTokenData(tree);
    }));

  // When: Loading tokens
  manager->load_api_tokens();
  // Then: Tokens should be loaded
  const auto &tokens = manager->retrieve_loaded_api_tokens();
  EXPECT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens.contains("test_hash"));
  EXPECT_EQ(tokens.at("test_hash").username, "test_user");
}

TEST_F(ApiTokenManagerTest, given_file_does_not_exist_when_loading_tokens_then_should_not_load_any_tokens) {
  // Given: File does not exist
  EXPECT_CALL(*mock_deps, file_exists(_))
    .WillOnce(Return(false));

  // When: Loading tokens
  manager->load_api_tokens();
  // Then: No tokens should be loaded
  const auto &tokens = manager->retrieve_loaded_api_tokens();
  EXPECT_EQ(tokens.size(), 0);
}

TEST_F(ApiTokenManagerTest, given_tokens_exist_when_saving_tokens_then_should_write_to_file) {
  // Given: Tokens exist in manager
  std::map<std::string, std::set<std::string, std::less<>>, std::less<>> path_methods;
  path_methods["/api/data"] = {"GET"};
  ApiTokenInfo token_info {"test_hash", path_methods, "test_user", test_time};
  InjectToken(token_info);

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
  ApiTokenInfo token_info {"token_hash_123", path_methods, "test_user", test_time};
  InjectToken(token_info);

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
  ApiTokenInfo token_info {"token_hash_123", path_methods, "test_user", test_time};
  InjectToken(token_info);

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
  ApiTokenInfo token_info {"token_hash_123", path_methods, "test_user", test_time};
  InjectToken(token_info);

  // When: Authenticating with empty pattern (should not match any path)
  bool result = manager->authenticate_token("valid_token", "", "GET");

  // Then: Should not match empty path for security
  EXPECT_FALSE(result);
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
  ApiTokenInfo token_info {"token_hash_123", path_methods, "test_user", test_time};
  InjectToken(token_info);

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
  ApiTokenInfo token_info {"token_hash_123", empty_path_methods, "test_user", test_time};
  InjectToken(token_info);

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
    .WillOnce(Invoke([](const std::string &, pt::ptree &tree) {
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
  const auto &tokens = manager->retrieve_loaded_api_tokens();
  EXPECT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens.contains("valid_hash"));
}

TEST_F(ApiTokenManagerTest, given_large_number_of_tokens_when_listing_then_should_handle_efficiently) {
  // Given: Large number of tokens
  const size_t num_tokens = 1000;
  InjectLargeNumberOfTokens(num_tokens);

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
  const auto &tokens = manager->retrieve_loaded_api_tokens();
  const auto &token_info = tokens.at("test_hash");
  const auto &methods = token_info.path_methods.at("/api/data");
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
  ApiTokenInfo token_info {"test_hash", path_methods, "test_user", test_time};
  InjectToken(token_info);

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
  const auto &tokens = manager->retrieve_loaded_api_tokens();
  ASSERT_TRUE(tokens.contains("test_hash"));
  const auto &token_info = tokens.at("test_hash");
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
    .WillOnce(Invoke([](const std::string &, pt::ptree &tree) {
      pt::ptree tokens_tree;

      // Add a token with valid scope
      auto add_valid_token = [&tokens_tree]() {
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
      };

      // Add a token with malformed scope (empty methods should cause scope to be skipped)
      auto add_malformed_token = [&tokens_tree]() {
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
      };

      add_valid_token();
      add_malformed_token();

      tree.put_child("root.api_tokens", tokens_tree);
    }));
  // When: Loading tokens with malformed scope data
  manager->load_api_tokens();

  // Then: Should load both tokens, but malformed one should have no valid scopes
  const auto &tokens = manager->retrieve_loaded_api_tokens();
  EXPECT_EQ(tokens.size(), 2);
  EXPECT_TRUE(tokens.contains("valid_hash"));
  EXPECT_TRUE(tokens.contains("malformed_hash"));

  // Valid token should have scopes
  EXPECT_FALSE(tokens.at("valid_hash").path_methods.empty());
  EXPECT_TRUE(tokens.at("valid_hash").path_methods.contains("/api/data"));

  // Malformed token should have no valid scopes (empty methods were rejected)
  EXPECT_TRUE(tokens.at("malformed_hash").path_methods.empty());
}

// ---------------- SessionTokenManager Unit Tests ----------------

class SessionTokenManagerTest : public ::testing::Test {
protected:
  std::chrono::system_clock::time_point fake_now = std::chrono::system_clock::now();
  SessionTokenManagerDependencies deps;
  std::function<void(std::chrono::system_clock::duration)> advance_time;
  std::vector<std::string> generated_tokens;

  void SetUp() override {
    deps.now = [this]() { return fake_now; };
    deps.rand_alphabet = [this](std::size_t len) {
      std::string tok = std::string(len, 'A' + (generated_tokens.size() % 26));
      generated_tokens.push_back(tok);
      return tok;
    };
    advance_time = [this](std::chrono::system_clock::duration d) { fake_now += d; };
    mgr = std::make_unique<SessionTokenManager>(deps);
    generated_tokens.clear();
  }

  std::unique_ptr<SessionTokenManager> mgr;
};

TEST_F(SessionTokenManagerTest, given_username_when_generating_session_token_then_should_return_valid_token) {
  // Given: A username for session token generation
  std::string username = "test_user";

  // When: Generating a session token
  std::string token = mgr->generate_session_token(username);

  // Then: Should return a non-empty token that validates successfully
  EXPECT_FALSE(token.empty());
  EXPECT_TRUE(mgr->validate_session_token(token));
  
  // And the token should be associated with the correct username
  auto retrieved_username = mgr->get_username_for_token(token);
  ASSERT_TRUE(retrieved_username.has_value());
  EXPECT_EQ(*retrieved_username, username);
}

TEST_F(SessionTokenManagerTest, given_valid_session_token_when_validating_then_should_return_true) {
  // Given: A valid session token
  std::string token = mgr->generate_session_token("user123");

  // When: Validating the token
  bool is_valid = mgr->validate_session_token(token);

  // Then: Should return true
  EXPECT_TRUE(is_valid);
}

TEST_F(SessionTokenManagerTest, given_invalid_session_token_when_validating_then_should_return_false) {
  // Given: An invalid/non-existent session token
  std::string invalid_token = "invalid_token_12345";

  // When: Validating the invalid token
  bool is_valid = mgr->validate_session_token(invalid_token);

  // Then: Should return false
  EXPECT_FALSE(is_valid);
}

TEST_F(SessionTokenManagerTest, given_empty_session_token_when_validating_then_should_return_false) {
  // Given: An empty session token
  std::string empty_token = "";

  // When: Validating the empty token
  bool is_valid = mgr->validate_session_token(empty_token);

  // Then: Should return false
  EXPECT_FALSE(is_valid);
}

TEST_F(SessionTokenManagerTest, given_valid_session_token_when_revoking_then_should_invalidate_token) {
  // Given: A valid session token
  std::string token = mgr->generate_session_token("user_to_revoke");
  ASSERT_TRUE(mgr->validate_session_token(token));

  // When: Revoking the token
  mgr->revoke_session_token(token);

  // Then: Token should no longer be valid
  EXPECT_FALSE(mgr->validate_session_token(token));
  
  // And username lookup should fail
  auto username = mgr->get_username_for_token(token);
  EXPECT_FALSE(username.has_value());
}

TEST_F(SessionTokenManagerTest, given_non_existent_token_when_revoking_then_should_handle_gracefully) {
  // Given: A non-existent token
  std::string non_existent_token = "does_not_exist_12345";

  // When: Attempting to revoke the non-existent token
  // Then: Should not crash or throw exception
  EXPECT_NO_THROW(mgr->revoke_session_token(non_existent_token));
}

TEST_F(SessionTokenManagerTest, given_valid_session_token_when_getting_username_then_should_return_correct_username) {
  // Given: A session token for a specific user
  std::string expected_username = "expected_user";
  std::string token = mgr->generate_session_token(expected_username);

  // When: Getting username for the token
  auto username = mgr->get_username_for_token(token);

  // Then: Should return the correct username
  ASSERT_TRUE(username.has_value());
  EXPECT_EQ(*username, expected_username);
}

TEST_F(SessionTokenManagerTest, given_invalid_token_when_getting_username_then_should_return_nullopt) {
  // Given: An invalid token
  std::string invalid_token = "invalid_token_xyz";

  // When: Getting username for invalid token
  auto username = mgr->get_username_for_token(invalid_token);

  // Then: Should return nullopt
  EXPECT_FALSE(username.has_value());
}

TEST_F(SessionTokenManagerTest, given_multiple_tokens_when_counting_sessions_then_should_return_correct_count) {
  // Given: Initial session count
  size_t initial_count = mgr->session_count();

  // When: Creating multiple session tokens
  std::string token1 = mgr->generate_session_token("user1");
  std::string token2 = mgr->generate_session_token("user2");
  std::string token3 = mgr->generate_session_token("user3");

  // Then: Session count should increase by 3
  EXPECT_EQ(mgr->session_count(), initial_count + 3);

  // When: Revoking one token
  mgr->revoke_session_token(token2);

  // Then: Session count should decrease by 1
  EXPECT_EQ(mgr->session_count(), initial_count + 2);

  // When: Revoking remaining tokens
  mgr->revoke_session_token(token1);
  mgr->revoke_session_token(token3);

  // Then: Session count should return to initial value
  EXPECT_EQ(mgr->session_count(), initial_count);
}

TEST_F(SessionTokenManagerTest, given_no_sessions_when_counting_then_should_return_zero_or_initial_count) {
  // Given: A fresh session manager or after cleanup
  
  // When: Getting session count without any active sessions
  size_t count = mgr->session_count();

  // Then: Should return a non-negative count (could be > 0 if other tests ran first)
  EXPECT_GE(count, 0);
}

TEST_F(SessionTokenManagerTest, given_session_manager_when_cleaning_up_expired_tokens_then_should_not_crash) {
  // Given: A session manager with some tokens
  std::string token1 = mgr->generate_session_token("user1");
  std::string token2 = mgr->generate_session_token("user2");
  size_t count_before = mgr->session_count();

  // When: Calling cleanup (note: tokens won't actually be expired in this test)
  EXPECT_NO_THROW(mgr->cleanup_expired_session_tokens());

  // Then: Should not crash and valid tokens should remain
  EXPECT_TRUE(mgr->validate_session_token(token1));
  EXPECT_TRUE(mgr->validate_session_token(token2));
  EXPECT_EQ(mgr->session_count(), count_before);
}

TEST_F(SessionTokenManagerTest, given_same_username_when_generating_multiple_tokens_then_should_create_different_tokens) {
  // Given: The same username
  std::string username = "same_user";

  // When: Generating multiple tokens for the same user
  std::string token1 = mgr->generate_session_token(username);
  std::string token2 = mgr->generate_session_token(username);
  std::string token3 = mgr->generate_session_token(username);

  // Then: All tokens should be different
  EXPECT_NE(token1, token2);
  EXPECT_NE(token2, token3);
  EXPECT_NE(token1, token3);

  // And all tokens should be valid and map to the same username
  EXPECT_TRUE(mgr->validate_session_token(token1));
  EXPECT_TRUE(mgr->validate_session_token(token2));
  EXPECT_TRUE(mgr->validate_session_token(token3));

  auto user1 = mgr->get_username_for_token(token1);
  auto user2 = mgr->get_username_for_token(token2);
  auto user3 = mgr->get_username_for_token(token3);

  ASSERT_TRUE(user1.has_value());
  ASSERT_TRUE(user2.has_value());
  ASSERT_TRUE(user3.has_value());
  EXPECT_EQ(*user1, username);
  EXPECT_EQ(*user2, username);
  EXPECT_EQ(*user3, username);
}

TEST_F(SessionTokenManagerTest, given_token_with_special_characters_in_username_when_generating_then_should_handle_correctly) {
  // Given: Username with special characters
  std::string special_username = "user@domain.com_123!";

  // When: Generating token for username with special characters
  std::string token = mgr->generate_session_token(special_username);

  // Then: Should handle correctly and return the username as-is
  EXPECT_FALSE(token.empty());
  EXPECT_TRUE(mgr->validate_session_token(token));
  
  auto retrieved_username = mgr->get_username_for_token(token);
  ASSERT_TRUE(retrieved_username.has_value());
  EXPECT_EQ(*retrieved_username, special_username);
}

TEST_F(SessionTokenManagerTest, given_empty_username_when_generating_token_then_should_handle_gracefully) {
  // Given: Empty username
  std::string empty_username = "";

  // When: Generating token for empty username
  std::string token = mgr->generate_session_token(empty_username);

  // Then: Should still generate a valid token
  EXPECT_FALSE(token.empty());
  EXPECT_TRUE(mgr->validate_session_token(token));
  
  // And should correctly return the empty username
  auto retrieved_username = mgr->get_username_for_token(token);
  ASSERT_TRUE(retrieved_username.has_value());
  EXPECT_EQ(*retrieved_username, empty_username);
}

TEST_F(SessionTokenManagerTest, given_token_when_expired_then_should_not_validate) {
  std::string username = "expiring_user";
  std::string token = mgr->generate_session_token(username);
  // Should be valid immediately
  EXPECT_TRUE(mgr->validate_session_token(token));
  // Advance time past expiry
  advance_time(std::chrono::hours(25));
  // Should now be invalid
  EXPECT_FALSE(mgr->validate_session_token(token));
  // Username lookup should fail
  EXPECT_FALSE(mgr->get_username_for_token(token).has_value());
}


class SessionTokenAPITest : public Test {
protected:
    void SetUp() override {
        // Save original config values
        original_username = config::sunshine.username;
        original_password = config::sunshine.password;
        original_salt = config::sunshine.salt;
        
        // Set test config
        config::sunshine.username = "testuser";
        config::sunshine.password = util::hex(crypto::hash(std::string("testpass") + "testsalt")).to_string();
        config::sunshine.salt = "testsalt";

        // Create SessionTokenManager with fake dependencies
        deps.now = [this]() { return fake_now; };
        deps.rand_alphabet = [this](std::size_t len) {
            return "fake_token_" + std::to_string(token_counter++);
        };
        
        session_manager = std::make_unique<SessionTokenManager>(deps);
        session_api = std::make_unique<SessionTokenAPI>(*session_manager);
    }

    void TearDown() override {
        // Restore original config values
        config::sunshine.username = original_username;
        config::sunshine.password = original_password;
        config::sunshine.salt = original_salt;
    }

    std::string original_username;
    std::string original_password;
    std::string original_salt;
    SessionTokenManagerDependencies deps;
    std::chrono::system_clock::time_point fake_now = std::chrono::system_clock::now();
    std::unique_ptr<SessionTokenManager> session_manager;
    std::unique_ptr<SessionTokenAPI> session_api;
    int token_counter = 0;
};

/**
 * @brief Test successful login with valid credentials.
 */
TEST_F(SessionTokenAPITest, given_valid_credentials_when_logging_in_then_should_return_success_with_token) {
    // When: Logging in with valid credentials
    auto response = session_api->login("testuser", "testpass");

    // Then: Should return success response with token
    EXPECT_EQ(response.status_code, SimpleWeb::StatusCode::success_ok);
    EXPECT_FALSE(response.body.empty());
    
    auto json_response = nlohmann::json::parse(response.body);
    EXPECT_TRUE(json_response["status"]);
    EXPECT_TRUE(json_response.contains("token"));
    EXPECT_TRUE(json_response.contains("expires_in"));
    EXPECT_EQ(json_response["redirect"], "/");
    
    // Check content type header
    auto content_type = response.headers.find("Content-Type");
    EXPECT_NE(content_type, response.headers.end());
    EXPECT_EQ(content_type->second, "application/json");
    
    // Check session cookie
    auto cookie = response.headers.find("Set-Cookie");
    EXPECT_NE(cookie, response.headers.end());
    EXPECT_THAT(cookie->second, HasSubstr("session_token="));
    EXPECT_THAT(cookie->second, HasSubstr("HttpOnly"));
}

/**
 * @brief Test login with invalid credentials.
 */
TEST_F(SessionTokenAPITest, given_invalid_credentials_when_logging_in_then_should_return_unauthorized) {
    // When: Logging in with invalid credentials
    auto response = session_api->login("testuser", "wrongpass");

    // Then: Should return unauthorized response
    EXPECT_EQ(response.status_code, SimpleWeb::StatusCode::client_error_unauthorized);
    EXPECT_FALSE(response.body.empty());
    
    auto json_response = nlohmann::json::parse(response.body);
    EXPECT_FALSE(json_response["status"]);
    EXPECT_EQ(json_response["error"], "Invalid credentials");
}

/**
 * @brief Test logout functionality.
 */
TEST_F(SessionTokenAPITest, given_session_token_when_logging_out_then_should_return_success) {
    // Given: A valid session token
    auto login_response = session_api->login("testuser", "testpass");
    auto login_json = nlohmann::json::parse(login_response.body);
    std::string token = login_json["token"];

    // When: Logging out
    auto response = session_api->logout(token);

    // Then: Should return success response
    EXPECT_EQ(response.status_code, SimpleWeb::StatusCode::success_ok);
    auto json_response = nlohmann::json::parse(response.body);
    EXPECT_TRUE(json_response["status"]);
    EXPECT_EQ(json_response["message"], "Logged out successfully");
}

/**
 * @brief Test token refresh functionality.
 */
TEST_F(SessionTokenAPITest, given_valid_token_when_refreshing_then_should_return_new_token) {
    // Given: A valid session token
    auto login_response = session_api->login("testuser", "testpass");
    auto login_json = nlohmann::json::parse(login_response.body);
    std::string old_token = login_json["token"];

    // When: Refreshing the token
    auto response = session_api->refresh_token(old_token);

    // Then: Should return success response with new token
    EXPECT_EQ(response.status_code, SimpleWeb::StatusCode::success_ok);
    auto json_response = nlohmann::json::parse(response.body);
    EXPECT_TRUE(json_response["status"]);
    EXPECT_TRUE(json_response.contains("token"));
    EXPECT_NE(json_response["token"], old_token); // Should be a different token
}

/**
 * @brief Test token validation functionality.
 */
TEST_F(SessionTokenAPITest, given_valid_token_when_validating_then_should_return_success) {
    // Given: A valid session token
    auto login_response = session_api->login("testuser", "testpass");
    auto login_json = nlohmann::json::parse(login_response.body);
    std::string token = login_json["token"];

    // When: Validating the token
    auto response = session_api->validate_session(token);

    // Then: Should return success response
    EXPECT_EQ(response.status_code, SimpleWeb::StatusCode::success_ok);
    auto json_response = nlohmann::json::parse(response.body);
    EXPECT_TRUE(json_response["status"]);
}

/**
 * @brief Test token validation with invalid token.
 */
TEST_F(SessionTokenAPITest, given_invalid_token_when_validating_then_should_return_unauthorized) {
    // When: Validating an invalid token
    auto response = session_api->validate_session("invalid_token");

    // Then: Should return unauthorized response
    EXPECT_EQ(response.status_code, SimpleWeb::StatusCode::client_error_unauthorized);
    auto json_response = nlohmann::json::parse(response.body);
    EXPECT_FALSE(json_response["status"]);
    EXPECT_EQ(json_response["error"], "Invalid or expired session token");
}

/**
 * @brief Test login with a custom safe redirect.
 */
TEST_F(SessionTokenAPITest, given_safe_redirect_when_logging_in_then_should_return_custom_redirect) {
    auto response = session_api->login("testuser", "testpass", "/dashboard");
    EXPECT_EQ(response.status_code, SimpleWeb::StatusCode::success_ok);
    auto json_response = nlohmann::json::parse(response.body);
    EXPECT_TRUE(json_response["status"]);
    EXPECT_EQ(json_response["redirect"], "/dashboard");
}

/**
 * @brief Test login with an unsafe redirect (double slash).
 */
TEST_F(SessionTokenAPITest, given_unsafe_redirect_when_logging_in_then_should_return_root_redirect) {
    auto response = session_api->login("testuser", "testpass", "//malicious");
    EXPECT_EQ(response.status_code, SimpleWeb::StatusCode::success_ok);
    auto json_response = nlohmann::json::parse(response.body);
    EXPECT_TRUE(json_response["status"]);
    EXPECT_EQ(json_response["redirect"], "/");
}

/**
 * @brief Test login with an unsafe redirect (dot dot).
 */
TEST_F(SessionTokenAPITest, given_dotdot_redirect_when_logging_in_then_should_return_root_redirect) {
    auto response = session_api->login("testuser", "testpass", "/../admin");
    EXPECT_EQ(response.status_code, SimpleWeb::StatusCode::success_ok);
    auto json_response = nlohmann::json::parse(response.body);
    EXPECT_TRUE(json_response["status"]);
    EXPECT_EQ(json_response["redirect"], "/");
}

/**
 * @brief Test login with an empty redirect (should default to root).
 */
TEST_F(SessionTokenAPITest, given_empty_redirect_when_logging_in_then_should_return_root_redirect) {
    auto response = session_api->login("testuser", "testpass", "");
    EXPECT_EQ(response.status_code, SimpleWeb::StatusCode::success_ok);
    auto json_response = nlohmann::json::parse(response.body);
    EXPECT_TRUE(json_response["status"]);
    EXPECT_EQ(json_response["redirect"], "/");
}
