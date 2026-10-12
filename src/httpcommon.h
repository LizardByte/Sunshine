/**
 * @file src/httpcommon.h
 * @brief Declarations for common HTTP.
 */
#pragma once

// lib includes
#include <curl/curl.h>

// local includes
#include "network.h"
#include "thread_safe.h"

namespace http {

  /**
   * @brief Own host identity and Web UI origin policy for one running application.
   */
  class state_t {
  public:
    /**
     * @brief Bind the configuration and coherent credential store.
     * @param store Application configuration owner.
     */
    explicit state_t(config::store_t &store);
    /**
     * @brief Initialize credentials and host HTTP policy before worker threads start.
     * @return Zero on success, nonzero if credentials cannot be initialized.
     */
    int initialize();
    /**
     * @brief Read the persistent host identity.
     * @return Current host UUID.
     */
    const std::string &unique_id() const;
    /**
     * @brief Set the identity read from persistent pairing state.
     * @param value Host UUID to adopt.
     */
    void set_unique_id(std::string value);
    /**
     * @brief Read the allowed Web UI network origin.
     * @return Most remote network category allowed to access the Web UI.
     */
    net::net_e origin_web_ui_allowed() const;

  private:
    config::store_t &store;  ///< Application settings and authentication owner.
    std::string host_unique_id;  ///< Persistent host identity shared with GameStream clients.
    net::net_e allowed_origin {};  ///< Web UI origin policy initialized before handlers start.
  };

  /**
   * @brief Generate HTTPS credential files from the provided key and certificate paths.
   *
   * @param pkey Private key PEM data or private key file path.
   * @param cert Certificate data or object used by the operation.
   * @return Created creds object or status.
   */
  int create_creds(const std::string &pkey, const std::string &cert);
  /**
   * @brief Save salted Web UI credentials while retaining other credential-file properties.
   * @param file Credentials file path.
   * @param username Username to save.
   * @param password Password to hash and save.
   * @return Zero on success; nonzero when the credentials file cannot be read or written.
   */
  int save_user_creds(
    const std::string &file,
    const std::string &username,
    const std::string &password
  );

  /**
   * @brief Reload Web UI user credentials from disk.
   *
   * @param file Credentials file to read.
   * @param store Owner receiving the complete authentication tuple.
   * @return 0 when credentials reload successfully; nonzero on failure.
   */
  int reload_user_creds(const std::string &file, config::store_t &store);
  /**
   * @brief Download a URL to a local file using libcurl.
   *
   * @param url URL used for the HTTP request.
   * @param file Destination path for the downloaded content.
   * @param ssl_version libcurl TLS version selector for the request.
   * @return True when the file is downloaded successfully.
   */
  bool download_file(const std::string &url, const std::string &file, long ssl_version = CURL_SSLVERSION_TLSv1_2);
  /**
   * @brief Percent-encode a string for safe inclusion in a URL.
   *
   * @param url URL used for the HTTP request.
   * @return Percent-encoded URL component.
   */
  std::string url_escape(const std::string &url);
  /**
   * @brief Extract the host component from a URL.
   *
   * @param url URL used for the HTTP request.
   * @return Host name parsed from the URL, or an empty string when none is present.
   */
  std::string url_get_host(const std::string &url);

}  // namespace http
