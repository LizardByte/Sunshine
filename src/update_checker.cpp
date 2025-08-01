/**
 * @file src/update_checker.cpp
 * @brief Definitions for the update checker functionality.
 */
// header include
#include "update_checker.h"

// standard includes
#include <atomic>
#include <sstream>
#include <thread>

// lib includes
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <curl/curl.h>

// local includes
#include "config.h"
#include "logging.h"
#include "platform/common.h"

using namespace std::literals;

namespace update_checker {
  static std::atomic<bool> running{false};
  static std::thread update_thread;
  static UpdateCallback update_callback;

  /**
   * @brief CURL write callback to store response data.
   */
  static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *data) {
    size_t total_size = size * nmemb;
    data->append(static_cast<char*>(contents), total_size);
    return total_size;
  }

  /**
   * @brief Fetch JSON data from a URL.
   */
  std::string fetch_json(const std::string &url) {
    CURL *curl = curl_easy_init();
    if (!curl) {
      BOOST_LOG(error) << "Failed to initialize CURL for update check";
      return "";
    }

    std::string response_data;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Sunshine-Update-Checker/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode result = curl_easy_perform(curl);
    
    if (result != CURLE_OK) {
      BOOST_LOG(warning) << "Failed to fetch update information: " << curl_easy_strerror(result);
      response_data.clear();
    }

    curl_easy_cleanup(curl);
    return response_data;
  }

  std::vector<int> parse_version(const std::string &version) {
    if (version.empty()) {
      return {};
    }

    std::string v = version;
    if (v[0] == 'v') {
      v = v.substr(1);
    }

    std::vector<int> parts;
    std::istringstream ss(v);
    std::string part;

    while (std::getline(ss, part, '.')) {
      try {
        parts.push_back(std::stoi(part));
      } catch (const std::exception&) {
        // If we can't parse a part as int, stop here
        break;
      }
    }

    return parts;
  }

  bool Version::isGreater(const Version &other) const {
    auto this_parts = parse_version(this->version);
    auto other_parts = parse_version(other.version);

    if (this_parts.empty() || other_parts.empty()) {
      return false;
    }

    size_t min_size = std::min(static_cast<size_t>(3), std::min(this_parts.size(), other_parts.size()));
    for (size_t i = 0; i < min_size; ++i) {
      if (this_parts[i] != other_parts[i]) {
        return this_parts[i] > other_parts[i];
      }
    }

    return false;
  }

  Version parse_release(const boost::property_tree::ptree &release) {
    Version version;
    
    try {
      version.tag_name = release.get<std::string>("tag_name", "");
      version.version = version.tag_name;
      version.name = release.get<std::string>("name", "");
      version.html_url = release.get<std::string>("html_url", "");
      version.body = release.get<std::string>("body", "");
      version.prerelease = release.get<bool>("prerelease", false);
    } catch (const std::exception &e) {
      BOOST_LOG(warning) << "Failed to parse release data: " << e.what();
    }

    return version;
  }

  UpdateInfo check_for_updates() {
    UpdateInfo info = {};
    
    // Get current version
    info.current_version.version = PROJECT_VERSION;
    
    BOOST_LOG(debug) << "Checking for updates. Current version: " << PROJECT_VERSION;

    try {
      // Fetch latest release
      std::string latest_response = fetch_json("https://api.github.com/repos/LizardByte/Sunshine/releases/latest");
      if (!latest_response.empty()) {
        std::istringstream latest_stream(latest_response);
        boost::property_tree::ptree latest_tree;
        boost::property_tree::read_json(latest_stream, latest_tree);
        
        info.stable_version = parse_release(latest_tree);
        info.has_stable_update = info.stable_version.isGreater(info.current_version);
      }

      // Fetch all releases to find latest prerelease
      if (config::sunshine.notify_pre_releases) {
        std::string releases_response = fetch_json("https://api.github.com/repos/LizardByte/Sunshine/releases");
        if (!releases_response.empty()) {
          std::istringstream releases_stream(releases_response);
          boost::property_tree::ptree releases_tree;
          boost::property_tree::read_json(releases_stream, releases_tree);
          
          // Find the latest prerelease
          for (const auto &release_pair : releases_tree) {
            const auto &release = release_pair.second;
            if (release.get<bool>("prerelease", false)) {
              Version prerelease = parse_release(release);
              if (prerelease.isGreater(info.current_version) && 
                  prerelease.isGreater(info.stable_version)) {
                info.prerelease_version = prerelease;
                info.has_prerelease_update = true;
                break;  // Releases are ordered by date, so first prerelease is the latest
              }
            }
          }
        }
      }
    } catch (const std::exception &e) {
      BOOST_LOG(error) << "Exception during update check: " << e.what();
    }

    BOOST_LOG(debug) << "Update check complete. Stable update available: " << info.has_stable_update 
                    << ", Prerelease update available: " << info.has_prerelease_update;

    return info;
  }

  void update_thread_function() {
    BOOST_LOG(info) << "Update checker thread started";

    while (running) {
      try {
        // Check if update checking is disabled
        if (config::sunshine.update_check_interval.count() == 0) {
          BOOST_LOG(info) << "Update checking is disabled (interval = 0)";
          break;
        }

        UpdateInfo info = check_for_updates();
        
        if ((info.has_stable_update || info.has_prerelease_update) && update_callback) {
          update_callback(info);
        }
      } catch (const std::exception &e) {
        BOOST_LOG(error) << "Error in update checker thread: " << e.what();
      }

      // Sleep for the configured interval
      auto interval = config::sunshine.update_check_interval;
      auto sleep_minutes = std::chrono::duration_cast<std::chrono::minutes>(interval);
      
      // Sleep in 1-minute chunks to allow for quick shutdown
      for (int i = 0; i < sleep_minutes.count() && running; ++i) {
        std::this_thread::sleep_for(std::chrono::minutes(1));
      }
    }

    BOOST_LOG(info) << "Update checker thread stopped";
  }

  void init(UpdateCallback callback) {
    update_callback = callback;
    BOOST_LOG(info) << "Update checker initialized with " << config::sunshine.update_check_interval.count() << " hour interval";
  }

  void start() {
    if (running) {
      return;
    }

    // Don't start if update checking is disabled
    if (config::sunshine.update_check_interval.count() == 0) {
      BOOST_LOG(info) << "Update checking is disabled, not starting update checker";
      return;
    }

    running = true;
    update_thread = std::thread(update_thread_function);
    BOOST_LOG(info) << "Update checker started";
  }

  void stop() {
    if (!running) {
      return;
    }

    running = false;
    if (update_thread.joinable()) {
      update_thread.join();
    }
    BOOST_LOG(info) << "Update checker stopped";
  }

}  // namespace update_checker
