/**
 * @file tests/unit/test_confighttp.cpp
 * @brief Unit tests for confighttp authentication methods.
 */

// standard includes
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <string>
#include <map>

// lib includes
#include <Simple-Web-Server/crypto.hpp>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

// local includes
#include "../tests_common.h"
#include "src/confighttp.h"
#include "src/http_auth.h"
#include "src/config.h"
#include "src/crypto.h"
#include "src/utility.h"
#include "src/network.h"

using namespace testing;

namespace confighttp {

/**
 * @brief Test fixture for authentication helper functions.
 */
class ConfigHttpAuthHelpersTest : public Test {
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
    }

    void TearDown() override {
        // Restore original config values
        config::sunshine.username = original_username;
        config::sunshine.password = original_password;
        config::sunshine.salt = original_salt;
    }

    std::string createBasicAuthHeader(const std::string& username, const std::string& password) {
        auto credentials = username + ":" + password;
        auto encoded = SimpleWeb::Crypto::Base64::encode(credentials);
        return "Basic " + encoded;
    }

private:
    std::string original_username;
    std::string original_password;
    std::string original_salt;
};

/**
 * @brief Test authenticate_basic function with valid credentials.
 */
TEST_F(ConfigHttpAuthHelpersTest, given_valid_basic_auth_credentials_when_authenticating_then_should_return_true) {
    // Given: Valid username and password for basic authentication
    auto auth_header = createBasicAuthHeader("testuser", "testpass");
    
    // When: Authenticating with valid credentials
    bool result = authenticate_basic(auth_header);
    
    // Then: Authentication should succeed
    EXPECT_TRUE(result);
}

/**
 * @brief Test authenticate_basic function with invalid password.
 */
TEST_F(ConfigHttpAuthHelpersTest, given_invalid_password_when_authenticating_then_should_return_false) {
    // Given: Valid username but wrong password
    auto auth_header = createBasicAuthHeader("testuser", "wrongpass");
    
    // When: Authenticating with invalid password
    bool result = authenticate_basic(auth_header);
    
    // Then: Authentication should fail
    EXPECT_FALSE(result);
}

/**
 * @brief Test authenticate_basic function with invalid username.
 */
TEST_F(ConfigHttpAuthHelpersTest, given_invalid_username_when_authenticating_then_should_return_false) {
    // Given: Wrong username but valid password
    auto auth_header = createBasicAuthHeader("wronguser", "testpass");
    
    // When: Authenticating with invalid username
    bool result = authenticate_basic(auth_header);
    
    // Then: Authentication should fail
    EXPECT_FALSE(result);
}

/**
 * @brief Test authenticate_basic function with malformed auth header (no colon).
 */
TEST_F(ConfigHttpAuthHelpersTest, given_malformed_auth_header_without_colon_when_authenticating_then_should_return_false) {
    // Given: Malformed auth header without colon separator
    auto encoded = SimpleWeb::Crypto::Base64::encode("testusertestpass");
    auto auth_header = "Basic " + encoded;
    
    // When: Authenticating with malformed header
    bool result = authenticate_basic(auth_header);
    
    // Then: Authentication should fail
    EXPECT_FALSE(result);
}

/**
 * @brief Test authenticate_basic function with empty credentials.
 */
TEST_F(ConfigHttpAuthHelpersTest, given_empty_credentials_when_authenticating_then_should_return_false) {
    // Given: Empty username and password
    auto encoded = SimpleWeb::Crypto::Base64::encode(":");
    auto auth_header = "Basic " + encoded;
    
    // When: Authenticating with empty credentials
    bool result = authenticate_basic(auth_header);
    
    // Then: Authentication should fail
    EXPECT_FALSE(result);
}

/**
 * @brief Test authenticate_basic function with colon at start.
 */
TEST_F(ConfigHttpAuthHelpersTest, given_empty_username_when_authenticating_then_should_return_false) {
    // Given: Empty username with valid password
    auto encoded = SimpleWeb::Crypto::Base64::encode(":testpass");
    auto auth_header = "Basic " + encoded;
    
    // When: Authenticating with empty username
    bool result = authenticate_basic(auth_header);
    
    // Then: Authentication should fail
    EXPECT_FALSE(result);
}

/**
 * @brief Test authenticate_basic function with colon at end.
 */
TEST_F(ConfigHttpAuthHelpersTest, given_empty_password_when_authenticating_then_should_return_false) {
    // Given: Valid username with empty password
    auto encoded = SimpleWeb::Crypto::Base64::encode("testuser:");
    auto auth_header = "Basic " + encoded;
    
    // When: Authenticating with empty password
    bool result = authenticate_basic(auth_header);
    
    // Then: Authentication should fail
    EXPECT_FALSE(result);
}

/**
 * @brief Test authenticate_basic function with case-insensitive username check.
 */
TEST_F(ConfigHttpAuthHelpersTest, given_uppercase_username_when_authenticating_then_should_return_true) {
    // Given: Valid credentials with uppercase username
    auto auth_header = createBasicAuthHeader("TESTUSER", "testpass");
    
    // When: Authenticating with case-different username
    bool result = authenticate_basic(auth_header);
    
    // Then: Authentication should succeed (case insensitive)
    EXPECT_TRUE(result);
}

/**
 * @brief Test authenticate_basic function with multiple colons.
 */
TEST_F(ConfigHttpAuthHelpersTest, given_password_with_colons_when_authenticating_then_should_return_true) {
    // Given: Valid credentials where password contains colons
    auto credentials = "testuser:pass:with:colons";
    auto encoded = SimpleWeb::Crypto::Base64::encode(credentials);
    auto auth_header = "Basic " + encoded;
    
    // Update config to match the expected password hash
    config::sunshine.password = util::hex(crypto::hash(std::string("pass:with:colons") + config::sunshine.salt)).to_string();
    
    // When: Authenticating with password containing colons
    bool result = authenticate_basic(auth_header);
    
    // Then: Authentication should succeed
    EXPECT_TRUE(result);
}

/**
 * @brief Test make_auth_error helper function for unauthorized error.
 */
TEST_F(ConfigHttpAuthHelpersTest, given_unauthorized_error_when_making_auth_error_then_should_return_proper_response) {
    // Given: Unauthorized error with WWW-Authenticate header requested
    
    // When: Creating auth error response
    auto result = make_auth_error(SimpleWeb::StatusCode::client_error_unauthorized, "Unauthorized", true);
    
    // Then: Should return proper unauthorized response
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, SimpleWeb::StatusCode::client_error_unauthorized);
    EXPECT_FALSE(result.body.empty());
    
    auto json_response = nlohmann::json::parse(result.body);
    EXPECT_EQ(json_response["status_code"], static_cast<int>(SimpleWeb::StatusCode::client_error_unauthorized));
    EXPECT_FALSE(json_response["status"]);
    EXPECT_EQ(json_response["error"], "Unauthorized");
    
    // Check content type header
    auto content_type_header = result.headers.find("Content-Type");
    EXPECT_NE(content_type_header, result.headers.end());
    EXPECT_EQ(content_type_header->second, "application/json");
    
    // Check WWW-Authenticate header
    auto auth_header = result.headers.find("WWW-Authenticate");
    EXPECT_NE(auth_header, result.headers.end());
}

/**
 * @brief Test make_auth_error helper function for forbidden error.
 */
TEST_F(ConfigHttpAuthHelpersTest, given_forbidden_error_when_making_auth_error_then_should_return_proper_response) {
    // Given: Forbidden error without WWW-Authenticate header
    
    // When: Creating auth error response
    auto result = make_auth_error(SimpleWeb::StatusCode::client_error_forbidden, "Forbidden", false);
    
    // Then: Should return proper forbidden response
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, SimpleWeb::StatusCode::client_error_forbidden);
    EXPECT_FALSE(result.body.empty());
    
    auto json_response = nlohmann::json::parse(result.body);
    EXPECT_EQ(json_response["error"], "Forbidden");
    
    // Check that WWW-Authenticate header is not present
    auto auth_header = result.headers.find("WWW-Authenticate");
    EXPECT_EQ(auth_header, result.headers.end());
}

/**
 * @brief Test make_auth_error helper function with redirect.
 */
TEST_F(ConfigHttpAuthHelpersTest, given_redirect_location_when_making_auth_error_then_should_return_redirect_response) {
    // Given: Redirect error with location header
    
    // When: Creating redirect auth error response
    auto result = make_auth_error(SimpleWeb::StatusCode::redirection_temporary_redirect, 
                                 "Redirect", false, "/welcome");
    
    // Then: Should return proper redirect response
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, SimpleWeb::StatusCode::redirection_temporary_redirect);
    EXPECT_TRUE(result.body.empty());
    
    auto location_header = result.headers.find("Location");
    EXPECT_NE(location_header, result.headers.end());
    EXPECT_EQ(location_header->second, "/welcome");
}

/**
 * @brief Test make_auth_error helper function with custom error message.
 */
TEST_F(ConfigHttpAuthHelpersTest, given_custom_error_message_when_making_auth_error_then_should_return_response_with_custom_message) {
    // Given: Custom error message for forbidden error
    
    // When: Creating auth error response with custom message
    auto result = make_auth_error(SimpleWeb::StatusCode::client_error_forbidden, 
                                 "Custom error message", false);
    
    // Then: Should return response with custom error message
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, SimpleWeb::StatusCode::client_error_forbidden);
    
    auto json_response = nlohmann::json::parse(result.body);
    EXPECT_EQ(json_response["error"], "Custom error message");
}

/**
 * @brief Test fixture for check_basic_auth function.
 */
class ConfigHttpCheckBasicAuthTest : public Test {
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
    }

    void TearDown() override {
        // Restore original config values
        config::sunshine.username = original_username;
        config::sunshine.password = original_password;
        config::sunshine.salt = original_salt;
    }

    std::string createBasicAuthHeader(const std::string& username, const std::string& password) {
        auto credentials = username + ":" + password;
        auto encoded = SimpleWeb::Crypto::Base64::encode(credentials);
        return "Basic " + encoded;
    }

private:
    std::string original_username;
    std::string original_password;
    std::string original_salt;
};

/**
 * @brief Test check_basic_auth function with valid credentials.
 */
TEST_F(ConfigHttpCheckBasicAuthTest, given_valid_basic_auth_when_checking_auth_then_should_return_success) {
    // Given: Valid basic authentication credentials
    auto auth_header = createBasicAuthHeader("testuser", "testpass");
    
    // When: Checking basic authentication
    auto result = check_basic_auth(auth_header);
    
    // Then: Should return successful result
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.code, SimpleWeb::StatusCode::success_ok);
    EXPECT_TRUE(result.body.empty());
    EXPECT_TRUE(result.headers.empty());
}

/**
 * @brief Test check_basic_auth function with invalid credentials.
 */
TEST_F(ConfigHttpCheckBasicAuthTest, given_invalid_basic_auth_when_checking_auth_then_should_return_unauthorized) {
    // Given: Invalid basic authentication credentials
    auto auth_header = createBasicAuthHeader("testuser", "wrongpass");
    
    // When: Checking basic authentication
    auto result = check_basic_auth(auth_header);
    
    // Then: Should return unauthorized error
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, SimpleWeb::StatusCode::client_error_unauthorized);
    EXPECT_FALSE(result.body.empty());
    
    auto json_response = nlohmann::json::parse(result.body);
    EXPECT_EQ(json_response["error"], "Unauthorized");
    
    // Check WWW-Authenticate header
    auto auth_header_result = result.headers.find("WWW-Authenticate");
    EXPECT_NE(auth_header_result, result.headers.end());
}

/**
 * @brief Test fixture for check_bearer_auth function.
 */
class ConfigHttpCheckBearerAuthTest : public Test {
protected:
    void SetUp() override {
        // Bearer auth tests would require mocking the API token manager
        // For now we just test the function signature and basic error case
    }
};

/**
 * @brief Test check_bearer_auth function with invalid token.
 */
TEST_F(ConfigHttpCheckBearerAuthTest, given_invalid_bearer_token_when_checking_auth_then_should_return_forbidden) {
    // Given: Invalid bearer token for API endpoint
    auto raw_auth = "Bearer invalid_token_123";
    auto path = "/api/test";
    auto method = "GET";
    
    // When: Checking bearer authentication
    auto result = check_bearer_auth(raw_auth, path, method);
    
    // Then: Should return forbidden error
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, SimpleWeb::StatusCode::client_error_forbidden);
    
    auto json_response = nlohmann::json::parse(result.body);
    EXPECT_EQ(json_response["error"], "Forbidden: Token does not have permission for this path/method.");
}

/**
 * @brief Test fixture for check_auth function.
 */
class ConfigHttpCheckAuthTest : public Test {
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
    }

    void TearDown() override {
        // Restore original config values
        config::sunshine.username = original_username;
        config::sunshine.password = original_password;
        config::sunshine.salt = original_salt;
    }

    std::string createBasicAuthHeader(const std::string& username, const std::string& password) {
        auto credentials = username + ":" + password;
        auto encoded = SimpleWeb::Crypto::Base64::encode(credentials);
        return "Basic " + encoded;
    }

private:
    std::string original_username;
    std::string original_password;
    std::string original_salt;
};

/**
 * @brief Test check_auth with valid basic authentication.
 */
TEST_F(ConfigHttpCheckAuthTest, given_valid_basic_auth_when_checking_full_auth_then_should_return_success) {
    // Given: Valid basic authentication credentials and allowed IP
    auto auth_header = createBasicAuthHeader("testuser", "testpass");
    
    // When: Checking full authentication flow
    auto result = check_auth("127.0.0.1", auth_header, "/api/test", "GET");
    
    // Then: Should return successful result
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.code, SimpleWeb::StatusCode::success_ok);
    EXPECT_TRUE(result.body.empty());
    EXPECT_TRUE(result.headers.empty());
}

/**
 * @brief Test check_auth with invalid basic authentication.
 */
TEST_F(ConfigHttpCheckAuthTest, given_invalid_basic_auth_when_checking_full_auth_then_should_return_unauthorized) {
    // Given: Invalid basic authentication credentials
    auto auth_header = createBasicAuthHeader("testuser", "wrongpass");
    
    // When: Checking full authentication flow
    auto result = check_auth("127.0.0.1", auth_header, "/api/test", "GET");
    
    // Then: Should return unauthorized error
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, SimpleWeb::StatusCode::client_error_unauthorized);
    
    auto json_response = nlohmann::json::parse(result.body);
    EXPECT_EQ(json_response["error"], "Unauthorized");
}

/**
 * @brief Test check_auth with no authentication header.
 */
TEST_F(ConfigHttpCheckAuthTest, given_missing_auth_header_when_checking_auth_then_should_return_unauthorized) {
    // Given: No authentication header provided
    
    // When: Checking authentication with empty header
    auto result = check_auth("127.0.0.1", "", "/api/test", "GET");
    
    // Then: Should return unauthorized error
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, SimpleWeb::StatusCode::client_error_unauthorized);
    
    auto json_response = nlohmann::json::parse(result.body);
    EXPECT_EQ(json_response["error"], "Unauthorized");
}

/**
 * @brief Test check_auth with empty username configuration (should redirect to welcome).
 */
TEST_F(ConfigHttpCheckAuthTest, given_empty_username_config_when_checking_auth_then_should_redirect_to_welcome) {
    // Given: Empty username configuration (initial setup)
    config::sunshine.username = "";
    
    // When: Checking authentication during initial setup
    auto result = check_auth("127.0.0.1", "Basic dGVzdDp0ZXN0", "/api/test", "GET");
    
    // Then: Should redirect to welcome page
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, SimpleWeb::StatusCode::redirection_temporary_redirect);
    
    auto location_header = result.headers.find("Location");
    EXPECT_NE(location_header, result.headers.end());
    EXPECT_EQ(location_header->second, "/welcome");
}

/**
 * @brief Test check_auth with disallowed IP address.
 */
TEST_F(ConfigHttpCheckAuthTest, given_disallowed_ip_address_when_checking_auth_then_should_return_forbidden) {
    // Given: Valid credentials but disallowed IP address
    auto auth_header = createBasicAuthHeader("testuser", "testpass");
    
    // When: Checking authentication from external IP
    auto result = check_auth("8.8.8.8", auth_header, "/api/test", "GET");
    
    // Then: Should return forbidden error
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, SimpleWeb::StatusCode::client_error_forbidden);
    
    auto json_response = nlohmann::json::parse(result.body);
    EXPECT_EQ(json_response["error"], "Forbidden");
}

/**
 * @brief Test check_auth with invalid bearer token.
 */
TEST_F(ConfigHttpCheckAuthTest, given_invalid_bearer_token_when_checking_auth_then_should_return_forbidden) {
    // Given: Invalid bearer token for API access
    
    // When: Checking authentication with invalid bearer token
    auto result = check_auth("127.0.0.1", "Bearer invalid_token", "/api/test", "GET");
    
    // Then: Should return forbidden error
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, SimpleWeb::StatusCode::client_error_forbidden);
    
    auto json_response = nlohmann::json::parse(result.body);
    EXPECT_EQ(json_response["error"], "Forbidden: Token does not have permission for this path/method.");
}

/**
 * @brief Test check_auth with invalid auth scheme.
 */
TEST_F(ConfigHttpCheckAuthTest, given_unsupported_auth_scheme_when_checking_auth_then_should_return_unauthorized) {
    // Given: Unsupported authentication scheme (Digest)
    
    // When: Checking authentication with unsupported scheme
    auto result = check_auth("127.0.0.1", "Digest realm=test", "/api/test", "GET");
    
    // Then: Should return unauthorized error
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, SimpleWeb::StatusCode::client_error_unauthorized);
    
    auto json_response = nlohmann::json::parse(result.body);
    EXPECT_EQ(json_response["error"], "Unauthorized");
}

}  // namespace confighttp