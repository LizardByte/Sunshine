/**
 * @file src/update.cpp
 * @brief Definitions for update checking, notification, and command execution.
 */

#include "update.h"

// standard includes
#include <cstdio>
#include <future>
#include <regex>
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

#ifndef SUNSHINE_REPO_OWNER
  #define SUNSHINE_REPO_OWNER "LizardByte"
#endif
#ifndef SUNSHINE_REPO_NAME
  #define SUNSHINE_REPO_NAME "Sunshine"
#endif

using namespace std::literals;

namespace update {
  state_t state;

  // libcurl write callback
  static size_t write_to_string(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    auto *out = static_cast<std::string *>(userp);
    out->append(static_cast<char *>(contents), total);
    return total;
  }

  bool download_github_release_data(const std::string &owner, const std::string &repo, std::string &out_json) {
    // Build GitHub API URL for releases list (reverse-chronological)
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
      return v.substr(1);
    }
    return v;
  }

  static bool version_greater(const std::string &a, const std::string &b) {
    if (a.empty() || b.empty()) {
      return false;
    }
    auto na = normalize_version(a);
    auto nb = normalize_version(b);
    std::stringstream sa(na), sb(nb);
    std::string tokenA, tokenB;
    while (std::getline(sa, tokenA, '.') && std::getline(sb, tokenB, '.')) {
      int ia = std::stoi(tokenA);
      int ib = std::stoi(tokenB);
      if (ia != ib) {
        return ia > ib;
      }
    }
    // If all equal up to here, longer with extra numeric parts treated as greater
    if (std::getline(sa, tokenA, '.')) {
      return true;  // a has extra segment
    }
    return false;
  }

  static void notify_new_version(const std::string &version, bool prerelease) {
#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
    if (version.empty()) {
      return;
    }
    // Build a clear, user-friendly message and make it clickable to open the release page
    std::string title = prerelease ? "New update available (Pre-release)" : "New update available (Stable)";
    std::string body = "Version " + version;
    state.last_notified_version = version;
    state.last_notified_is_prerelease = prerelease;
    state.last_notified_url = prerelease ? state.available_prerelease_url : state.available_release_url;
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
        // Reset
        state.available_version.clear();
        state.available_release_url.clear();
        state.available_release_name.clear();
        state.available_release_body.clear();
        state.available_release_published_at.clear();
        state.available_prerelease_version.clear();
        state.available_prerelease_url.clear();
        state.available_prerelease_name.clear();
        state.available_prerelease_body.clear();
        state.available_prerelease_published_at.clear();

        for (auto &rel : j) {
          bool is_prerelease = rel.value("prerelease", false);
          bool is_draft = rel.value("draft", false);
          if (!is_draft && !is_prerelease && state.available_version.empty()) {
            state.available_version = rel.value("tag_name", "");
            state.available_release_url = rel.value("html_url", "");
            state.available_release_name = rel.value("name", "");
            state.available_release_body = rel.value("body", "");
            state.available_release_published_at = rel.value("published_at", "");
            BOOST_LOG(info) << "Update check: latest stable tag="sv << state.available_version;
          }
          if (config::sunshine.notify_pre_releases && is_prerelease && state.available_prerelease_version.empty()) {
            state.available_prerelease_version = rel.value("tag_name", "");
            state.available_prerelease_url = rel.value("html_url", "");
            state.available_prerelease_name = rel.value("name", "");
            state.available_prerelease_body = rel.value("body", "");
            state.available_prerelease_published_at = rel.value("published_at", "");
            BOOST_LOG(info) << "Update check: latest prerelease tag="sv << state.available_prerelease_version;
          }
          if (!state.available_version.empty() && (!config::sunshine.notify_pre_releases || !state.available_prerelease_version.empty())) {
            break;  // got what we need
          }
        }
      }
      state.last_check_time = std::chrono::steady_clock::now();
      // Compare with current build version
      std::string current = PROJECT_VERSION;
      bool should_run_update_command = false;

      // Only consider prereleases when configured to do so. If a newer prerelease exists,
      // notify about it. Do not fall back to stable releases under any circumstances.
      if (config::sunshine.notify_pre_releases &&
          !state.available_prerelease_version.empty() &&
          // prerelease must be newer than installed
          version_greater(state.available_prerelease_version, current)) {
        notify_new_version(state.available_prerelease_version, true);
        should_run_update_command = true;
      } else {
        BOOST_LOG(info) << "Update check: no newer prerelease found (current="sv << current
                        << ", prerelease="sv << state.available_prerelease_version << ')';
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
    std::string target_version = state.available_version.empty() ? state.available_prerelease_version : state.available_version;
    // If we don't yet know a target version, still run the command as a best-effort.
    if (config::sunshine.update_command_once_per_version && state.last_update_command_version == target_version) {
      return false;
    }
    try {
      auto env = boost::this_process::environment();
      // Provide metadata so scripts can make smarter decisions
      env["SUNSHINE_VERSION_CURRENT"] = PROJECT_VERSION;
      env["SUNSHINE_VERSION_AVAILABLE"] = state.available_version;
      env["SUNSHINE_VERSION_PRERELEASE"] = state.available_prerelease_version;
      env["SUNSHINE_UPDATE_CHANNEL"] = (!state.available_version.empty() ? "stable" : (!state.available_prerelease_version.empty() ? "prerelease" : "none"));
      env["SUNSHINE_RELEASE_URL"] = (!state.available_version.empty() ? state.available_release_url : state.available_prerelease_url);
      env["SUNSHINE_RELEASE_NAME"] = (!state.available_version.empty() ? state.available_release_name : state.available_prerelease_name);
      env["SUNSHINE_RELEASE_BODY"] = (!state.available_version.empty() ? state.available_release_body : state.available_prerelease_body);
      env["SUNSHINE_RELEASE_PUBLISHED_AT"] = (!state.available_version.empty() ? state.available_release_published_at : state.available_prerelease_published_at);
      std::error_code ec;
      boost::filesystem::path working_dir;  // empty path (current directory)
      auto child = platf::run_command(false, true, config::sunshine.update_command, working_dir, env, nullptr, ec, nullptr);
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
