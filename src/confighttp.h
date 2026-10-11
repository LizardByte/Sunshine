/**
 * @file src/confighttp.h
 * @brief Declarations for the Web UI Config HTTP server.
 */
#pragma once

// standard includes
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

// lib includes
#include <libvirtualhid/license.hpp>
#include <nlohmann/json.hpp>
#include <Simple-Web-Server/server_https.hpp>

// local includes
#include "platform/permissions.h"
#include "thread_safe.h"

/**
 * @def WEB_DIR
 * @brief Macro for WEB DIR.
 */
#define WEB_DIR SUNSHINE_ASSETS_DIR "/web/"

namespace nvhttp {
  class service_t;
}

namespace http {
  class state_t;
}

namespace proc {
  class proc_t;
}

namespace display_device {
  class manager_t;
}

namespace input {
  class service_t;
}

namespace lifetime {
  class controller_t;
}

namespace rtsp_stream {
  class service_t;
}

namespace confighttp {
  constexpr auto PORT_HTTPS = 1;  ///< GameStream port offset for port https.

  // Type aliases for HTTPS server components
  using https_server_t = SimpleWeb::Server<SimpleWeb::HTTPS>;
  using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
  using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;

  // Main server start function

  void print_req(const req_https_t &request);
  void send_response(const resp_https_t &response, const nlohmann::json &output_tree);
  void send_unauthorized(const resp_https_t &response, const req_https_t &request);
  void send_redirect(const resp_https_t &response, const req_https_t &request, const char *path);

  void not_found(const resp_https_t &response, const req_https_t &request, const std::string &error_message = "Not Found");
  void bad_request(const resp_https_t &response, const req_https_t &request, const std::string &error_message = "Bad Request");
  /**
   * @brief Check content type.
   *
   * @param response HTTP response object to populate.
   * @param request HTTP request data from the client.
   * @param contentType Expected HTTP content type.
   * @return True when the request passes validation and processing may continue.
   */
  bool check_content_type(const resp_https_t &response, const req_https_t &request, const std::string_view &contentType);

  /**
   * @brief Check whether a detected driver version identifies a development build.
   *
   * Numeric versions beginning with `0.0` and containing at least three
   * components are development builds.
   *
   * @param version Detected driver version.
   * @return True when the version identifies a development build.
   */
  bool is_driver_version_development(std::string_view version);

  /**
   * @brief Check whether a detected driver version satisfies a minimum version.
   *
   * Empty minimum versions accept any detected version. Non-empty minimum versions
   * require a fully numeric dotted version string. Development versions beginning
   * with `0.0` are always accepted.
   *
   * @param version Detected driver version.
   * @param minimum_version Minimum supported driver version, or empty for any version.
   * @return True when the driver version is supported.
   */
  bool is_driver_version_supported(std::string_view version, std::string_view minimum_version);

  /**
   * @brief Build a standard driver status response.
   *
   * @param installed Whether the driver was detected.
   * @param version Detected driver version.
   * @param minimum_version Minimum supported driver version, or empty for any version.
   * @return Driver status JSON object.
   */
  nlohmann::json build_driver_status(bool installed, const std::string &version, std::string_view minimum_version);

  /**
   * @brief Convert a libvirtualhid license result into a Web UI response.
   *
   * @param result License operation result.
   * @return License status JSON object without the submitted license key.
   */
  nlohmann::json build_virtualhid_license_status(const lvh::LicenseResult &result);

  /**
   * @brief Build Virtual HID Broker version and installation status.
   *
   * @return Virtual HID Broker status JSON.
   */
  nlohmann::json get_virtualhid_driver_status();

  /**
   * @brief Build ViGEmBus fallback driver version and installation status.
   *
   * @return ViGEmBus fallback driver status JSON.
   */
  nlohmann::json get_vigembus_driver_status();

  using permission_status_provider_t = std::function<std::vector<platf::permission_status_t>()>;  ///< Permission query bound to a handler instance.
  using permissions_handler_t = std::function<void(const resp_https_t &, const req_https_t &)>;  ///< Permission endpoint with its own provider.

#ifdef SUNSHINE_TESTS
  using virtual_input_license_status_provider_t = std::function<lvh::LicenseResult()>;  ///< Test provider for current libvirtualhid license status.
  using portal_token_path_provider_t = std::function<std::filesystem::path()>;  ///< Test provider for the XDG Portal token path.

  /**
   * @brief Exercise request-local sensitive string clearing for unit tests.
   *
   * @param value Mutable sensitive string to overwrite and clear.
   */
  void clear_sensitive_string_for_testing(std::string &value);
#endif

  // Browse helper functions (also exposed for unit testing)
  /**
   * @brief Checks whether a directory entry qualifies as an executable file.
   * @param entry The directory entry to check.
   * @param status The cached file status for the entry.
   * @return True if the file should be included in an executable-type listing.
   */
  bool is_browsable_executable(const std::filesystem::directory_entry &entry, const std::filesystem::file_status &status);

  /**
   * @brief Lists, filters, and sorts the entries of a directory for the browse API.
   * @param dir_path The directory to list.
   * @param type_str Filter type: "directory", "executable", "file", or "any".
   * @return Sorted JSON array of entry objects with name/type/path fields.
   */
  nlohmann::json build_browse_entries(const std::filesystem::path &dir_path, const std::string &type_str);

#ifdef _WIN32
  /**
   * @brief Builds a JSON array of available Windows drive letters.
   * @return JSON array of drive-letter entries.
   */
  nlohmann::json get_windows_drives();
#endif
  /**
   * @brief Own Web UI handlers, request state, and their explicit application dependencies.
   */
  class service_t {
  public:
    /**
     * @brief Bind dependencies before registering HTTP callbacks.
     * @param gamestream GameStream pairing and client state.
     * @param http_state Host HTTP identity and origin policy.
     * @param processes Application process owner.
     * @param displays Display configuration and recovery owner.
     * @param inputs Shared input owner.
     * @param rtsp Session and launch request owner.
     * @param lifetime Application termination controller.
     * @param host_mail Application shutdown mailbox.
     */
    service_t(nvhttp::service_t &gamestream, http::state_t &http_state, proc::proc_t &processes, display_device::manager_t &displays, input::service_t &inputs, rtsp_stream::service_t &rtsp, lifetime::controller_t &lifetime, safe::mail_t host_mail);
    service_t(const service_t &) = delete;
    service_t &operator=(const service_t &) = delete;

#ifdef SUNSHINE_TESTS
    /**
     * @brief Replace the virtual-input license status provider for unit tests.
     *
     * @param status_provider Provider returning the license status for the response.
     */
    void set_virtual_input_license_status_provider_for_testing(virtual_input_license_status_provider_t status_provider);
#endif
#ifdef SUNSHINE_TESTS
    /**
     * @brief Restore the production virtual-input license status provider after a unit test.
     */
    void reset_virtual_input_license_status_provider_for_testing();
#endif
#ifdef SUNSHINE_TESTS
    /**
     * @brief Replace the XDG Portal token path provider for unit tests.
     *
     * @param path_provider Provider returning the token path used by the reset endpoint.
     */
    void set_portal_token_path_provider_for_testing(portal_token_path_provider_t path_provider);
#endif
#ifdef SUNSHINE_TESTS
    /**
     * @brief Restore the production XDG Portal token path provider after a unit test.
     */
    void reset_portal_token_path_provider_for_testing();
#endif
    /**
     * @brief Handle authenticate for this Web UI service.
     * @param response HTTPS response to populate.
     * @param request HTTPS request to process.
     * @return Result of the request.
     */
    bool authenticate(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle get_client_id for this Web UI service.
     * @return Result of the request.
     */
    std::string get_client_id(const req_https_t &request);
    /**
     * @brief Handle generate_csrf_token for this Web UI service.
     * @return Result of the request.
     */
    std::string generate_csrf_token(const std::string &client_id);
    /**
     * @brief Handle validate stored csrf token for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     * @param client_id Identity bound to the CSRF token.
     * @param provided_token CSRF token supplied by the client.
     * @return Result of the requested operation.
     */
    bool validate_stored_csrf_token(const resp_https_t &response, const req_https_t &request, const std::string_view client_id, const std::string_view provided_token);
    /**
     * @brief Validate CSRF token.
     *
     * @param response HTTP response object to populate.
     * @param request HTTP request data from the client.
     * @param client_id Client identifier used to look up the CSRF token.
     * @return True when the request passes validation and processing may continue.
     */
    bool validate_csrf_token(const resp_https_t &response, const req_https_t &request, const std::string &client_id);
    /**
     * @brief Check app index.
     *
     * @param response HTTP response object to populate.
     * @param request HTTP request data from the client.
     * @param index Zero-based index of the item being addressed.
     * @return True when the request passes validation and processing may continue.
     */
    bool check_app_index(const resp_https_t &response, const req_https_t &request, int index);
    /**
     * @brief Serve the Web UI single-page application entry document.
     *
     * @param response HTTP response object to populate.
     * @param request HTTP request data from the client.
     * @param require_auth Whether HTTP authentication is required.
     * @param redirect_if_username Whether configured users should be redirected to the authenticated home route.
     */
    void getPage(const resp_https_t &response, const req_https_t &request, bool require_auth = true, bool redirect_if_username = false);
    /**
     * @brief Serve the SPA entry for browser routes and preserve 404 responses for server-owned route prefixes.
     *
     * @param response HTTP response object to populate.
     * @param request HTTP request data from the client.
     */
    void getFallbackPage(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle get favicon image for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void getFaviconImage(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle get sunshine logo image for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void getSunshineLogoImage(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle getAsset for this Web UI service.
     * @param response HTTPS response to populate.
     * @param request HTTPS request to process.
     */
    void getAsset(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle getCSRFToken for this Web UI service.
     * @param response HTTPS response to populate.
     * @param request HTTPS request to process.
     */
    void getCSRFToken(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle get apps for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void getApps(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle save app for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void saveApp(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle close app for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void closeApp(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle delete app for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void deleteApp(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle get clients for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void getClients(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle update client for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void updateClient(resp_https_t response, req_https_t request);
    /**
     * @brief Handle unpair for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void unpair(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle unpair all for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void unpairAll(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle get config for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void getConfig(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle getLocale for this Web UI service.
     * @param response HTTPS response to populate.
     * @param request HTTPS request to process.
     */
    void getLocale(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle save config for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void saveConfig(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle get cover for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void getCover(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle upload cover for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void uploadCover(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle get logs for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void getLogs(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle save password for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void savePassword(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief List authenticated pairing requests awaiting operator approval.
     *
     * @param response HTTP response object to populate.
     * @param request HTTP request data from the client.
     */
    void getPendingPairings(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Cancel an explicitly selected pairing request.
     *
     * @param response HTTP response object to populate.
     * @param request HTTP request data from the client.
     */
    void cancelPairing(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Apply a PIN to an explicitly selected pairing request.
     *
     * @param response HTTP response object to populate.
     * @param request HTTP request data from the client.
     */
    void savePin(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle reset display device persistence for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void resetDisplayDevicePersistence(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle resetPortalToken for this Web UI service.
     * @param response HTTPS response to populate.
     * @param request HTTPS request to process.
     */
    void resetPortalToken(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle restart for this Web UI service.
     *
     * @param response HTTPS response to populate.
     * @param request HTTPS request from the client.
     */
    void restart(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Return the host's permission statuses to the Web UI.
     *
     * @param response HTTPS response receiving the status JSON.
     * @param request Authenticated HTTPS request.
     */
    void getPermissions(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Bind a permission query to an authenticated HTTP handler.
     *
     * @param provider Permission query owned by the returned handler.
     * @return Handler that queries its provider after authenticating a request.
     */
    permissions_handler_t make_permissions_handler(permission_status_provider_t provider);
    /**
     * @brief Initiate a native permission request from the Web UI.
     *
     * @param response HTTPS response receiving the request result.
     * @param request Authenticated, CSRF protected request.
     */
    void requestPermission(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle getVirtualInputStatus for this Web UI service.
     * @param response HTTPS response to populate.
     * @param request HTTPS request to process.
     */
    void getVirtualInputStatus(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle getVirtualInputLicense for this Web UI service.
     * @param response HTTPS response to populate.
     * @param request HTTPS request to process.
     */
    void getVirtualInputLicense(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle updateVirtualInputLicense for this Web UI service.
     * @param response HTTPS response to populate.
     * @param request HTTPS request to process.
     */
    void updateVirtualInputLicense(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle browseDirectory for this Web UI service.
     * @param response HTTPS response to populate.
     * @param request HTTPS request to process.
     */
    void browseDirectory(const resp_https_t &response, const req_https_t &request);
    /**
     * @brief Handle start for this Web UI service.
     */
    void start();

  private:
    /**
     * @brief Return current license details after authenticating a request.
     * @param response HTTPS response receiving license details.
     * @param request HTTPS request to authenticate.
     */
    void get_virtual_input_license(const resp_https_t &response, const req_https_t &request);

    /**
     * @brief CSRF token and the deadline enforced by this service.
     */
    struct csrf_token_t {
      std::string token;  ///< Request token bound to a client.
      std::chrono::steady_clock::time_point expiration;  ///< Monotonic deadline after which the token is rejected.
    };

    /**
     * @brief Access the license provider owned by these handlers.
     * @return License status provider.
     */
    std::function<lvh::LicenseResult()> &virtual_input_license_status_provider();
#if defined(linux) || defined(__FreeBSD__) || defined(SUNSHINE_TESTS)
    /**
     * @brief Access the saved portal token path provider owned by these handlers.
     * @return Portal path provider.
     */
    std::function<std::filesystem::path()> &portal_token_path_provider();
    std::function<std::filesystem::path()> token_path_provider;  ///< Bound portal token path query.
#endif
    std::function<lvh::LicenseResult()> license_provider = lvh::get_license_status;  ///< Bound license status query.
    nvhttp::service_t &gamestream;  ///< Pairing and client state.
    http::state_t &http_state;  ///< Host HTTP identity and origin policy.
    proc::proc_t &processes;  ///< Application process owner.
    display_device::manager_t &displays;  ///< Display configuration and recovery owner.
    input::service_t &inputs;  ///< Shared input owner.
    rtsp_stream::service_t &rtsp;  ///< Session and launch request owner.
    lifetime::controller_t &lifetime;  ///< Application lifetime controller.
    safe::mail_t host_mail;  ///< Application shutdown mailbox.
    std::map<std::string, csrf_token_t, std::less<>> csrf_tokens;  ///< Tokens local to this Web UI instance.
    std::mutex csrf_tokens_mutex;  ///< Protects token issuance and validation.
  };
}  // namespace confighttp

// mime types map
/**
 * @brief File-extension to MIME-type mapping used when serving the Web UI.
 */
const std::map<std::string, std::string> mime_types = {
  {"css", "text/css"},
  {"gif", "image/gif"},
  {"htm", "text/html"},
  {"html", "text/html"},
  {"ico", "image/x-icon"},
  {"jpeg", "image/jpeg"},
  {"jpg", "image/jpeg"},
  {"js", "application/javascript"},
  {"json", "application/json"},
  {"png", "image/png"},
  {"svg", "image/svg+xml"},
  {"ttf", "font/ttf"},
  {"txt", "text/plain"},
  {"woff2", "font/woff2"},
  {"xml", "text/xml"},
};
