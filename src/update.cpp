/**
 * @file src/update.cpp
 * @brief Definitions for update checking, notification, and command execution.
 */

#include "update.h"

// standard includes
#include <cstdio>
#include <future>
#include <regex>
#include <sstream>
#include <thread>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "config.h"
#include "httpcommon.h"
#include "logging.h"
#include "nvhttp.h"  // for save_state persistence hooks
#include "platform/common.h"
#include "process.h"
#include "rtsp.h"  // for session_count
#include "system_tray.h"
#include "utility.h"

#include <boost/process/v1/environment.hpp>


using namespace std::literals;

namespace update {
  state_t state;

  static size_t write_to_string(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    auto *out = static_cast<std::string *>(userp);
    out->append(static_cast<char *>(contents), total);
    return total;
  }

  bool download_github_release_data(const std::string &owner, const std::string &repo, std::string &out_json) {
    std::string url = "https://api.github.com/repos/" + owner + "/" + repo + "/releases";
    CURL *curl = curl_easy_init();
    if (!curl) {
      BOOST_LOG(error) << "CURL init failed for GitHub API"sv;
      return false;
    }
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
    headers = curl_slist_append(headers, "User-Agent: Sunshine-Updater/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_json);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      BOOST_LOG(error) << "GitHub API request failed: "sv << curl_easy_strerror(res);
      curl_slist_free_all(headers);
      curl_easy_cleanup(curl);
      return false;
    }
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (code < 200 || code >= 300) {
      BOOST_LOG(error) << "GitHub API returned HTTP "sv << code;
      return false;
    }
    return true;
  }

  static std::string normalize_version(std::string v) {
    if (v.starts_with('v')) {
      v = v.substr(1);
    }
    return v;
  }

  static std::vector<int> extract_version_parts(const std::string &v) {
    std::vector<int> parts;
    std::string s = normalize_version(v);
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, '.')) {
      int value = 0;
      // Parse numeric prefix of each token (e.g., "0-rc1" -> 0)
      size_t i = 0;
      while (i < token.size() && std::isdigit(static_cast<unsigned char>(token[i]))) {
        int digit = token[i] - '0';
        value = value * 10 + digit;
        ++i;
      }
      parts.push_back(value);
    }
    return parts;
  }

  static bool version_greater(const std::string &a, const std::string &b) {
    if (a.empty() || b.empty()) {
      return false;
    }
    auto pa = extract_version_parts(a);
    auto pb = extract_version_parts(b);
    const size_t max_len = std::max(pa.size(), pb.size());
    pa.resize(max_len, 0);
    pb.resize(max_len, 0);
    for (size_t i = 0; i < max_len; ++i) {
      if (pa[i] != pb[i]) {
        return pa[i] > pb[i];
      }
    }
    return false;
  }

  static void notify_new_version(const std::string &version, bool prerelease) {
#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
    if (version.empty()) {
      return;
    }
    std::string title = prerelease ? "New update available (Pre-release)" : "New update available (Stable)";
    std::string body = "Version " + version;
    state.last_notified_version = version;
    state.last_notified_is_prerelease = prerelease;
    state.last_notified_url = prerelease ? state.latest_prerelease.url : state.latest_release.url;
    // On click, open the release page directly
    system_tray::tray_notify(title.c_str(), body.c_str(), []() {
      open_last_notified_release_page();
    });
#endif
    // We intentionally allow repeated notifications; do not persist last_notified_version
  }

  static void perform_check(bool allow_auto_execute) {
    state.check_in_progress = true;
    auto fg = util::fail_guard([]() {
      state.check_in_progress = false;
    });
    try {
      // Fetch releases list once and compute latest stable/prerelease
      std::string releases_json;
      if (download_github_release_data(SUNSHINE_REPO_OWNER, SUNSHINE_REPO_NAME, releases_json)) {
        auto j = nlohmann::json::parse(releases_json);
        // Reset release info
        state.latest_release = release_info_t{};
        state.latest_prerelease = release_info_t{};

        for (auto &rel : j) {
          bool is_prerelease = rel.value("prerelease", false);
          bool is_draft = rel.value("draft", false);
          
          // Parse assets for this release
          std::vector<asset_info_t> assets;
          if (rel.contains("assets") && rel["assets"].is_array()) {
            for (auto &asset : rel["assets"]) {
              asset_info_t asset_info;
              asset_info.name = asset.value("name", "");
              asset_info.download_url = asset.value("browser_download_url", "");
              asset_info.size = asset.value("size", 0);
              asset_info.content_type = asset.value("content_type", "");
              
              // Extract SHA256 from digest field (format: "sha256:hash")
              std::string digest = asset.value("digest", "");
              if (digest.starts_with("sha256:")) {
                asset_info.sha256 = digest.substr(7);
              }
              
              if (!asset_info.name.empty() && !asset_info.download_url.empty()) {
                assets.push_back(asset_info);
              }
            }
          }

          if (!is_draft && !is_prerelease && state.latest_release.version.empty()) {
            state.latest_release.version = rel.value("tag_name", "");
            state.latest_release.url = rel.value("html_url", "");
            state.latest_release.name = rel.value("name", "");
            state.latest_release.body = rel.value("body", "");
            state.latest_release.published_at = rel.value("published_at", "");
            state.latest_release.is_prerelease = false;
            state.latest_release.assets = assets;
            BOOST_LOG(info) << "Update check: latest stable tag="sv << state.latest_release.version;
          }
          if (config::sunshine.notify_pre_releases && is_prerelease && state.latest_prerelease.version.empty()) {
            state.latest_prerelease.version = rel.value("tag_name", "");
            state.latest_prerelease.url = rel.value("html_url", "");
            state.latest_prerelease.name = rel.value("name", "");
            state.latest_prerelease.body = rel.value("body", "");
            state.latest_prerelease.published_at = rel.value("published_at", "");
            state.latest_prerelease.is_prerelease = true;
            state.latest_prerelease.assets = assets;
            BOOST_LOG(info) << "Update check: latest prerelease tag="sv << state.latest_prerelease.version;
          }
          if (!state.latest_release.version.empty() && (!config::sunshine.notify_pre_releases || !state.latest_prerelease.version.empty())) {
            break;  // got what we need
          }
        }
      }
      state.last_check_time = std::chrono::steady_clock::now();
      // Compare with current build version
      std::string current = PROJECT_VERSION;
      bool should_run_update_command = false;

      // Check for updates based on configuration preferences
      if (config::sunshine.notify_pre_releases &&
          !state.latest_prerelease.version.empty() &&
          version_greater(state.latest_prerelease.version, current)) {
        // Prerelease is newer than current
        notify_new_version(state.latest_prerelease.version, true);
        should_run_update_command = true;
      } else if (!state.latest_release.version.empty() &&
                 version_greater(state.latest_release.version, current)) {
        // Stable release is newer than current (and either no prerelease preference or no newer prerelease)
        notify_new_version(state.latest_release.version, false);
        should_run_update_command = true;
      } else {
        BOOST_LOG(info) << "Update check: no newer version found (current="sv << current
                        << ", stable="sv << state.latest_release.version
                        << ", prerelease="sv << state.latest_prerelease.version << ')';
      }

      // Only run update command automatically if allowed and no streaming sessions are active.
      if (allow_auto_execute && should_run_update_command && rtsp_stream::session_count() == 0) {
        if (run_update_command()) {
          BOOST_LOG(info) << "Update command executed successfully";
        }
      } else if (should_run_update_command && rtsp_stream::session_count() > 0) {
        BOOST_LOG(info) << "Update command not executed - streaming sessions are active";
      }
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "Update check failed: "sv << e.what();
    }
  }

  void trigger_check(bool force, bool allow_auto_execute) {
    if (state.check_in_progress.load()) {
      return;
    }
    if (!force && config::sunshine.update_check_interval_seconds == 0) {
      return;
    }
    if (!force) {
      auto now = std::chrono::steady_clock::now();
      if (now - state.last_check_time < std::chrono::seconds(config::sunshine.update_check_interval_seconds)) {
        return;
      }
    }
    std::thread([allow_auto_execute]() {
      perform_check(allow_auto_execute);
    }).detach();
  }

  bool run_update_command() {
    if (config::sunshine.update_command.empty()) {
      return false;
    }
    
    // Determine which release to use for the update command
    const release_info_t *target_release = nullptr;
    std::string target_version;
    
    if (config::sunshine.notify_pre_releases && !state.latest_prerelease.version.empty()) {
      target_release = &state.latest_prerelease;
      target_version = state.latest_prerelease.version;
    } else if (!state.latest_release.version.empty()) {
      target_release = &state.latest_release;
      target_version = state.latest_release.version;
    }
    
    // If we don't yet know a target version, still run the command as a best-effort.
    if (config::sunshine.update_command_once_per_version && state.last_update_command_version == target_version) {
      return false;
    }
    try {
      auto env = boost::this_process::environment();
      // Provide metadata so scripts can make smarter decisions
      env["SUNSHINE_VERSION_CURRENT"] = PROJECT_VERSION;
      
      if (target_release) {
        env["SUNSHINE_VERSION_AVAILABLE"] = target_release->version;
        env["SUNSHINE_UPDATE_CHANNEL"] = target_release->is_prerelease ? "prerelease" : "stable";
        env["SUNSHINE_RELEASE_URL"] = target_release->url;
        env["SUNSHINE_RELEASE_NAME"] = target_release->name;
        env["SUNSHINE_RELEASE_PUBLISHED_AT"] = target_release->published_at;

        // First, provide a single JSON payload containing all asset metadata for scripts to consume
        try {
          nlohmann::json assets_json = nlohmann::json::array();
          for (const auto &asset : target_release->assets) {
            nlohmann::json a;
            a["name"] = asset.name;
            a["url"] = asset.download_url;
            a["sha256"] = asset.sha256;
            a["size"] = asset.size;
            a["content_type"] = asset.content_type;
            assets_json.push_back(a);
          }
          std::string assets_dump = assets_json.dump();
          env["SUNSHINE_ASSETS_JSON"] = assets_dump;
          // Debug: expose the byte size of the assets JSON payload
          env["SUNSHINE_ASSETS_JSON_SIZE"] = std::to_string(assets_dump.size());
        } catch (const std::exception &e) {
          BOOST_LOG(error) << "Failed to build/assign SUNSHINE_ASSETS_JSON: "sv << e.what();
          env["SUNSHINE_ASSETS_JSON"] = "[]";
          env["SUNSHINE_ASSETS_JSON_SIZE"] = std::to_string(std::string("[]").size());
        } catch (...) {
          BOOST_LOG(error) << "Failed to build/assign SUNSHINE_ASSETS_JSON: unknown error"sv;
          env["SUNSHINE_ASSETS_JSON"] = "[]";
          env["SUNSHINE_ASSETS_JSON_SIZE"] = std::to_string(std::string("[]").size());
        }
        env["SUNSHINE_ASSET_COUNT"] = std::to_string(target_release->assets.size());

        // Then set the potentially large release body with a conservative cap to avoid environment block overflows on Windows
        constexpr size_t kMaxReleaseBodyBytes = 16384;  // 16 KiB cap
        std::string release_body = target_release->body;
        bool body_truncated = false;
        if (release_body.size() > kMaxReleaseBodyBytes) {
          release_body.resize(kMaxReleaseBodyBytes);
          body_truncated = true;
        }
        try {
          env["SUNSHINE_RELEASE_BODY"] = release_body;
        } catch (const std::exception &e) {
          BOOST_LOG(error) << "Failed to assign SUNSHINE_RELEASE_BODY: "sv << e.what();
          env["SUNSHINE_RELEASE_BODY"] = "";
        } catch (...) {
          BOOST_LOG(error) << "Failed to assign SUNSHINE_RELEASE_BODY: unknown error"sv;
          env["SUNSHINE_RELEASE_BODY"] = "";
        }
        if (body_truncated) {
          env["SUNSHINE_RELEASE_BODY_TRUNCATED"] = "1";
        }
      } else {
        // Fallback when no release data is available
        env["SUNSHINE_VERSION_AVAILABLE"] = "";
        env["SUNSHINE_UPDATE_CHANNEL"] = "none";
        env["SUNSHINE_RELEASE_URL"] = "";
        env["SUNSHINE_RELEASE_NAME"] = "";
        env["SUNSHINE_RELEASE_BODY"] = "";
        env["SUNSHINE_RELEASE_PUBLISHED_AT"] = "";
        env["SUNSHINE_ASSET_COUNT"] = "0";
        // Provide predictable values for assets JSON in no-release case
        env["SUNSHINE_ASSETS_JSON"] = "[]";
        env["SUNSHINE_ASSETS_JSON_SIZE"] = std::to_string(std::string("[]").size());
      }
      
      std::error_code ec;
      boost::filesystem::path working_dir;  // empty path (current directory)
      auto child = platf::run_command(config::sunshine.update_command_elevated, true, config::sunshine.update_command, working_dir, env, nullptr, ec, nullptr);
      if (ec) {
        BOOST_LOG(error) << "Failed to execute update command: "sv << ec.message();
        return false;
      }
      child.detach();
      if (!target_version.empty()) {
        state.last_update_command_version = target_version;
      }
      nvhttp::save_state();
      return true;
    } catch (std::exception &e) {
      BOOST_LOG(error) << "Exception executing update command: "sv << e.what();
    }
    return false;
  }

  void on_stream_started() {
    // Kick a metadata refresh after a small delay but do not auto-execute updates while streaming.
    std::thread([]() {
      std::this_thread::sleep_for(3s);
      trigger_check(true, false);
    }).detach();
  }

  void periodic() {
    // Only trigger checks if no streaming sessions are active
    if (rtsp_stream::session_count() == 0) {
      // Periodic timer is the only path that auto-executes update commands
      trigger_check(false, true);
    }
  }

  void open_last_notified_release_page() {
    try {
      const std::string &url = state.last_notified_url;
      if (!url.empty()) {
        platf::open_url(url);
      }
    } catch (...) {
      // swallow errors; opening the URL is best-effort
    }
  }
}  // namespace update
