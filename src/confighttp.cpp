/**
 * @file src/confighttp.cpp
 * @brief Definitions for the Web UI Config HTTP server.
 *
 * @todo Authentication, better handling of routes common to nvhttp, cleanup
 */
#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include "process.h"

#include <filesystem>
#include <set>
#include <ctime>
#include <cstdio>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <boost/algorithm/string.hpp>

#include <boost/asio/ssl/context.hpp>

#include <boost/filesystem.hpp>

#include <Simple-Web-Server/crypto.hpp>
#include <Simple-Web-Server/server_https.hpp>
#include <boost/asio/ssl/context_base.hpp>

#include "config.h"
#include "confighttp.h"
#include "crypto.h"
#include "display_device/session.h"
#include "file_handler.h"
#include "globals.h"
#include "httpcommon.h"
#include "logging.h"
#include "network.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "rtsp.h"
#include "src/display_device/display_device.h"
#include "src/display_device/to_string.h"
#include "utility.h"
#include "uuid.h"
#include "version.h"

using namespace std::literals;

namespace confighttp {
  namespace fs = std::filesystem;
  namespace pt = boost::property_tree;

  using https_server_t = SimpleWeb::Server<SimpleWeb::HTTPS>;

  using args_t = SimpleWeb::CaseInsensitiveMultimap;
  using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
  using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;

  enum class op_e {
    ADD,  ///< Add client
    REMOVE  ///< Remove client
  };

  void
  print_req(const req_https_t &request) {
    BOOST_LOG(debug) << "METHOD :: "sv << request->method;
    BOOST_LOG(debug) << "DESTINATION :: "sv << request->path;

    for (auto &[name, val] : request->header) {
      BOOST_LOG(debug) << name << " -- " << (name == "Authorization" ? "CREDENTIALS REDACTED" : val);
    }

    BOOST_LOG(debug) << " [--] "sv;

    for (auto &[name, val] : request->parse_query_string()) {
      BOOST_LOG(debug) << name << " -- " << val;
    }

    BOOST_LOG(debug) << " [--] "sv;
  }

  void
  send_unauthorized(resp_https_t response, req_https_t request) {
    auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
    BOOST_LOG(error) << "Web UI: ["sv << address << "] -- not authorized"sv;
    const SimpleWeb::CaseInsensitiveMultimap headers {
      { "WWW-Authenticate", R"(Basic realm="Sunshine Gamestream Host", charset="UTF-8")" }
    };
    response->write(SimpleWeb::StatusCode::client_error_unauthorized, headers);
  }

  void
  send_redirect(resp_https_t response, req_https_t request, const char *path) {
    auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
    BOOST_LOG(error) << "Web UI: ["sv << address << "] -- not authorized, redirect"sv;
    const SimpleWeb::CaseInsensitiveMultimap headers {
      { "Location", path }
    };
    response->write(SimpleWeb::StatusCode::redirection_temporary_redirect, headers);
  }

  bool
  authenticate(resp_https_t response, req_https_t request) {
    auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
    auto ip_type = net::from_address(address);

    if (ip_type > http::origin_web_ui_allowed) {
      BOOST_LOG(error) << "Web UI: ["sv << address << "] -- denied"sv;
      response->write(SimpleWeb::StatusCode::client_error_forbidden);
      return false;
    }

    // If credentials are shown, redirect the user to a /welcome page
    if (config::sunshine.username.empty()) {
      send_redirect(response, request, "/welcome");
      return false;
    }

    if (ip_type == net::PC) {
      return true;
    }

    auto fg = util::fail_guard([&]() {
      send_unauthorized(response, request);
    });

    auto auth = request->header.find("authorization");
    if (auth == request->header.end()) {
      return false;
    }

    auto &rawAuth = auth->second;
    auto authData = SimpleWeb::Crypto::Base64::decode(rawAuth.substr("Basic "sv.length()));

    int index = authData.find(':');
    if (index >= authData.size() - 1) {
      return false;
    }

    auto username = authData.substr(0, index);
    auto password = authData.substr(index + 1);
    auto hash = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();

    if (!boost::iequals(username, config::sunshine.username) || hash != config::sunshine.password) {
      return false;
    }

    fg.disable();
    return true;
  }

  void
  not_found(resp_https_t response, req_https_t request) {
    pt::ptree tree;
    tree.put("root.<xmlattr>.status_code", 404);

    std::ostringstream data;

    pt::write_xml(data, tree);
    response->write(data.str());

    *response << "HTTP/1.1 404 NOT FOUND\r\n"
              << data.str();
  }

  void
  getHtmlPage(resp_https_t response, req_https_t request, const std::string& pageName, bool requireAuth = true) {
    if (requireAuth && !authenticate(response, request)) return;

    print_req(request);

    std::string content = file_handler::read_file((std::string(WEB_DIR) + pageName).c_str());
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/html; charset=utf-8");
    if (pageName == "apps.html") {
      headers.emplace("Access-Control-Allow-Origin", "https://images.igdb.com/");
    }
    response->write(content, headers);
  }

  void
  getIndexPage(resp_https_t response, req_https_t request) {
    getHtmlPage(response, request, "index.html");
  }

  void
  getPinPage(resp_https_t response, req_https_t request) {
    getHtmlPage(response, request, "pin.html");
  }

  void
  getAppsPage(resp_https_t response, req_https_t request) {
    getHtmlPage(response, request, "apps.html");
  }

  void
  getClientsPage(resp_https_t response, req_https_t request) {
    getHtmlPage(response, request, "clients.html");
  }

  void
  getConfigPage(resp_https_t response, req_https_t request) {
    getHtmlPage(response, request, "config.html");
  }

  void
  getPasswordPage(resp_https_t response, req_https_t request) {
    getHtmlPage(response, request, "password.html");
  }

  void
  getWelcomePage(resp_https_t response, req_https_t request) {
    getHtmlPage(response, request, "welcome.html", false);
    if (!config::sunshine.username.empty()) {
      send_redirect(response, request, "/");
    }
  }

  void
  getTroubleshootingPage(resp_https_t response, req_https_t request) {
    getHtmlPage(response, request, "troubleshooting.html");
  }

  /**
   * 处理静态资源文件
   */
  void
  getStaticResource(resp_https_t response, req_https_t request, const std::string& path, const std::string& contentType) {
    print_req(request);

    std::ifstream in(path, std::ios::binary);
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", contentType);
    response->write(SimpleWeb::StatusCode::success_ok, in, headers);
  }

  void
  getFaviconImage(resp_https_t response, req_https_t request) {
    getStaticResource(response, request, WEB_DIR "images/sunshine.ico", "image/x-icon");
  }

  void
  getSunshineLogoImage(resp_https_t response, req_https_t request) {
    getStaticResource(response, request, WEB_DIR "images/logo-sunshine-45.png", "image/png");
  }

  void
  getBoxArt(resp_https_t response, req_https_t request) {
    print_req(request);

    // 从请求路径中提取图片文件名
    std::string path = request->path;
    if (path.find("/boxart/") == 0) {
      path = path.substr(7); // 移除"/boxart"前缀
    }

    // 构建完整的图片文件路径
    std::string imagePath = SUNSHINE_ASSETS_DIR "" + path;

    // 检查文件是否存在
    if (!fs::exists(imagePath)) {
      // 如果图片不存在,返回默认图片
      imagePath = SUNSHINE_ASSETS_DIR "box.png";
    }

    // 获取文件扩展名确定Content-Type
    std::string ext = fs::path(imagePath).extension().string().substr(1);
    auto mimeType = mime_types.find(ext);
    std::string contentType = "image/png"; // 默认类型

    if (mimeType != mime_types.end()) {
      contentType = mimeType->second;
    }

    // 返回图片资源
    std::ifstream in(imagePath, std::ios::binary);
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", contentType);
    response->write(SimpleWeb::StatusCode::success_ok, in, headers);
  }

  bool
  isChildPath(fs::path const &base, fs::path const &query) {
    auto relPath = fs::relative(base, query);
    return *(relPath.begin()) != fs::path("..");
  }

  void
  getNodeModules(resp_https_t response, req_https_t request) {
    print_req(request);
    fs::path webDirPath(WEB_DIR);
    fs::path nodeModulesPath(webDirPath / "assets");

    // .relative_path is needed to shed any leading slash that might exist in the request path
    auto filePath = fs::weakly_canonical(webDirPath / fs::path(request->path).relative_path());

    // Don't do anything if file does not exist or is outside the assets directory
    if (!isChildPath(filePath, nodeModulesPath)) {
      BOOST_LOG(warning) << "Someone requested a path " << filePath << " that is outside the assets folder";
      response->write(SimpleWeb::StatusCode::client_error_bad_request, "Bad Request");
    }
    else if (!fs::exists(filePath)) {
      response->write(SimpleWeb::StatusCode::client_error_not_found);
    }
    else {
      auto relPath = fs::relative(filePath, webDirPath);
      // get the mime type from the file extension mime_types map
      // remove the leading period from the extension
      auto mimeType = mime_types.find(relPath.extension().string().substr(1));
      // check if the extension is in the map at the x position
      if (mimeType != mime_types.end()) {
        // if it is, set the content type to the mime type
        SimpleWeb::CaseInsensitiveMultimap headers;
        headers.emplace("Content-Type", mimeType->second);
        std::ifstream in(filePath.string(), std::ios::binary);
        response->write(SimpleWeb::StatusCode::success_ok, in, headers);
      }
      // do not return any file if the type is not in the map
    }
  }

  void
  getApps(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::string content = file_handler::read_file(config::stream.file_apps.c_str());
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    response->write(content, headers);
  }

  void
  getLogs(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::string content = file_handler::read_file(config::sunshine.log_file.c_str());
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/plain");
    response->write(SimpleWeb::StatusCode::success_ok, content, headers);
  }

  void
  saveApp(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();

    pt::ptree outputTree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    pt::ptree inputTree, fileTree;

    BOOST_LOG(info) << config::stream.file_apps;
    try {
      // TODO: Input Validation
      pt::read_json(ss, inputTree);
      pt::read_json(config::stream.file_apps, fileTree);

      auto &apps_node = fileTree.get_child("apps"s);
      auto &input_apps_node = inputTree.get_child("apps"s);
      auto &input_edit_node = inputTree.get_child("editApp"s);

      if (input_edit_node.empty()) {
        fileTree.erase("apps");
        fileTree.push_back(std::make_pair("apps", input_apps_node));
      }
      else {
        if (input_edit_node.get_child("prep-cmd").empty()) {
          input_edit_node.erase("prep-cmd");
        }

        if (input_edit_node.get_child("detached").empty()) {
          input_edit_node.erase("detached");
        }

        int index = input_edit_node.get<int>("index");
        input_edit_node.erase("index");

        if (index == -1) {
          apps_node.push_back(std::make_pair("", input_edit_node));
        }
        else {
          // Unfortunately Boost PT does not allow to directly edit the array, copy should do the trick
          pt::ptree newApps;
          int i = 0;
          for (auto &[_, app_node] : input_apps_node) {
            newApps.push_back(std::make_pair("", i == index ? input_edit_node : app_node));
            i++;
          }
          fileTree.erase("apps");
          fileTree.push_back(std::make_pair("apps", newApps));
        }
      }

      pt::write_json(config::stream.file_apps, fileTree);
    }
    catch (std::exception &e) {
      BOOST_LOG(warning) << "SaveApp: "sv << e.what();

      outputTree.put("status", "false");
      outputTree.put("error", "Invalid Input JSON");
      return;
    }

    outputTree.put("status", "true");
    proc::refresh(config::stream.file_apps);
  }

  void
  deleteApp(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    pt::ptree outputTree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_json(data, outputTree);
      response->write(data.str());
    });
    pt::ptree fileTree;
    try {
      pt::read_json(config::stream.file_apps, fileTree);
      auto &apps_node = fileTree.get_child("apps"s);
      int index = stoi(request->path_match[1]);

      if (index < 0) {
        outputTree.put("status", "false");
        outputTree.put("error", "Invalid Index");
        return;
      }
      else {
        // Unfortunately Boost PT does not allow to directly edit the array, copy should do the trick
        pt::ptree newApps;
        int i = 0;
        for (const auto &kv : apps_node) {
          if (i++ != index) {
            newApps.push_back(std::make_pair("", kv.second));
          }
        }
        fileTree.erase("apps");
        fileTree.push_back(std::make_pair("apps", newApps));
      }
      pt::write_json(config::stream.file_apps, fileTree);
    }
    catch (std::exception &e) {
      BOOST_LOG(warning) << "DeleteApp: "sv << e.what();
      outputTree.put("status", "false");
      outputTree.put("error", "Invalid File JSON");
      return;
    }

    outputTree.put("status", "true");
    proc::refresh(config::stream.file_apps);
  }

  void
  uploadCover(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    std::stringstream ss;
    std::stringstream configStream;
    ss << request->content.rdbuf();
    pt::ptree outputTree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      SimpleWeb::StatusCode code = SimpleWeb::StatusCode::success_ok;
      if (outputTree.get_child_optional("error").has_value()) {
        code = SimpleWeb::StatusCode::client_error_bad_request;
      }

      pt::write_json(data, outputTree);
      response->write(code, data.str());
    });
    pt::ptree inputTree;
    try {
      pt::read_json(ss, inputTree);
    }
    catch (std::exception &e) {
      BOOST_LOG(warning) << "UploadCover: "sv << e.what();
      outputTree.put("status", "false");
      outputTree.put("error", e.what());
      return;
    }

    auto key = inputTree.get("key", "");
    if (key.empty()) {
      outputTree.put("error", "Cover key is required");
      return;
    }
    auto url = inputTree.get("url", "");

    const std::string coverdir = platf::appdata().string() + "/covers/";
    file_handler::make_directory(coverdir);

    std::basic_string path = coverdir + http::url_escape(key) + ".png";
    if (!url.empty()) {
      if (http::url_get_host(url) != "images.igdb.com") {
        outputTree.put("error", "Only images.igdb.com is allowed");
        return;
      }
      if (!http::download_file(url, path)) {
        outputTree.put("error", "Failed to download cover");
        return;
      }
    }
    else {
      auto data = SimpleWeb::Crypto::Base64::decode(inputTree.get<std::string>("data"));

      std::ofstream imgfile(path);
      imgfile.write(data.data(), (int) data.size());
    }
    outputTree.put("path", path);
  }

  void
  getConfig(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    pt::ptree outputTree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    auto devices { display_device::enum_available_devices() };
    pt::ptree devices_nodes;
    for (const auto &[device_id, data] : devices) {
      pt::ptree devices_node;
      devices_node.put("device_id"s, device_id);
      devices_node.put("data"s, to_string(data));
      devices_nodes.push_back(std::make_pair(""s, devices_node));
    }

    auto adapters { platf::adapter_names() };
    pt::ptree adapters_nodes;
    for (const auto &adapter_name : adapters) {
      pt::ptree adapters_node;
      adapters_node.put("name"s, adapter_name);
      adapters_nodes.push_back(std::make_pair(""s, adapters_node));
    }

    outputTree.add_child("display_devices", devices_nodes);
    outputTree.add_child("adapters", adapters_nodes);
    outputTree.put("status", "true");
    outputTree.put("platform", SUNSHINE_PLATFORM);
    outputTree.put("version", PROJECT_VER);

    auto vars = config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str()));
    for (auto &[name, value] : vars) {
      outputTree.put(std::move(name), std::move(value));
    }

    outputTree.put("pair_name", nvhttp::get_pair_name());
  }

  void
  getLocale(resp_https_t response, req_https_t request) {
    // we need to return the locale whether authenticated or not

    print_req(request);

    pt::ptree outputTree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    outputTree.put("status", "true");
    outputTree.put("locale", config::sunshine.locale);
  }

  std::vector<std::string>
  split(const std::string &str, char delimiter) {
    std::vector<std::string> tokens;
    size_t start = 0, end = 0;
    while ((end = str.find(delimiter, start)) != std::string::npos) {
      tokens.push_back(str.substr(start, end - start));
      start = end + 1;
    }
    tokens.push_back(str.substr(start));
    return tokens;
  }

  bool
  saveVddSettings(std::string resArray, std::string fpsArray, std::string gpu_name) {
    pt::ptree iddOptionTree;
    pt::ptree resolutions_nodes;

    // prepare resolutions setting for vdd
    boost::regex pattern("\\[|\\]|\\s+");
    char delimiter = ',';
    std::string str = boost::regex_replace(resArray, pattern, "");
    boost::algorithm::trim(str);
    for (const auto &resolution : split(str, delimiter)) {
      auto index = resolution.find('x');
      if(index == std::string::npos) {
        return false;
      }
      pt::ptree res_node;
      res_node.put("width", resolution.substr(0, index));
      res_node.put("height", resolution.substr(index + 1));
      for (const auto &fps : split(boost::regex_replace(fpsArray, pattern, ""), delimiter)) {
        res_node.add("refresh_rate", fps);
      }
      resolutions_nodes.push_back(std::make_pair("resolution"s, res_node));
    }

    // 类似于 config.cpp 中的 path_f 函数逻辑，使用相对路径
    std::filesystem::path idd_option_path = platf::appdata().parent_path() / "tools" / "vdd" / "vdd_settings.xml";

    BOOST_LOG(info) << "VDD配置文件路径: " << idd_option_path.string();

    if (!fs::exists(idd_option_path)) {
        return false;
    }

    // 先读取现有配置文件
    pt::ptree existing_root;
    pt::ptree root;

    try {
      pt::read_xml(idd_option_path.string(), existing_root);
      // 如果现有配置文件中已有vdd_settings节点
      if (existing_root.get_child_optional("vdd_settings")) {
        // 复制现有配置
        iddOptionTree = existing_root.get_child("vdd_settings");

        // 更新需要更改的部分
        pt::ptree monitor_node;
        monitor_node.put("count", 1);

        pt::ptree gpu_node;
        gpu_node.put("friendlyname", gpu_name.empty() ? "default" : gpu_name);

        // 替换配置
        iddOptionTree.put_child("monitors", monitor_node);
        iddOptionTree.put_child("gpu", gpu_node);
        iddOptionTree.put_child("resolutions", resolutions_nodes);
      } else {
        // 如果没有vdd_settings节点，创建新的
        pt::ptree monitor_node;
        monitor_node.put("count", 1);

        pt::ptree gpu_node;
        gpu_node.put("friendlyname", gpu_name.empty() ? "default" : gpu_name);

        iddOptionTree.add_child("monitors", monitor_node);
        iddOptionTree.add_child("gpu", gpu_node);
        iddOptionTree.add_child("resolutions", resolutions_nodes);
      }
    } catch(...) {
      // 读取失败，创建新的配置
      BOOST_LOG(warning) << "读取现有VDD配置失败，创建新配置";

      pt::ptree monitor_node;
      monitor_node.put("count", 1);

      pt::ptree gpu_node;
      gpu_node.put("friendlyname", gpu_name.empty() ? "default" : gpu_name);

      iddOptionTree.add_child("monitors", monitor_node);
      iddOptionTree.add_child("gpu", gpu_node);
      iddOptionTree.add_child("resolutions", resolutions_nodes);
    }

    root.add_child("vdd_settings", iddOptionTree);
    try {
      // 使用更紧凑的XML格式设置，减少不必要的空白
      auto setting = boost::property_tree::xml_writer_make_settings<std::string>(' ', 2);
      std::ostringstream oss;
      write_xml(oss, root, setting);

      // 清理多余空行，保持XML格式整洁
      std::string xml_content = oss.str();
      boost::regex empty_lines_regex("\\n\\s*\\n");
      xml_content = boost::regex_replace(xml_content, empty_lines_regex, "\n");

      std::ofstream file(idd_option_path.string());
      file << xml_content;
      file.close();

      return true;
    }
    catch(...) {
      return false;
    }
  }

  void
  saveConfig(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::stringstream ss;
    std::stringstream configStream;
    ss << request->content.rdbuf();
    pt::ptree outputTree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    pt::ptree inputTree;

    try {
      // TODO: Input Validation
      pt::read_json(ss, inputTree);
      std::string resArray = inputTree.get<std::string>("resolutions", "[]");
      std::string fpsArray = inputTree.get<std::string>("fps", "[]");
      std::string gpu_name = inputTree.get<std::string>("adapter_name", "");

      saveVddSettings(resArray, fpsArray, gpu_name);

      for (const auto &kv : inputTree) {
        std::string value = inputTree.get<std::string>(kv.first);
        if (value.length() == 0 || value.compare("null") == 0) continue;
        configStream << kv.first << " = " << value << std::endl;
      }
      file_handler::write_file(config::sunshine.config_file.c_str(), configStream.str());
    }
    catch (std::exception &e) {
      BOOST_LOG(warning) << "SaveConfig: "sv << e.what();
      outputTree.put("status", "false");
      outputTree.put("error", e.what());
      return;
    }
  }

  void
  restart(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    // We may not return from this call
    platf::restart();
  }

  void
  boom(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);
    if (GetConsoleWindow() == NULL) {
      lifetime::exit_sunshine(ERROR_SHUTDOWN_IN_PROGRESS, true);
      return;
    }
    lifetime::exit_sunshine(0, false);
  }

  void
  resetDisplayDevicePersistence(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    pt::ptree outputTree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;
      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    display_device::session_t::get().reset_persistence();
    outputTree.put("status", true);
  }

  void
  savePassword(resp_https_t response, req_https_t request) {
    if (!config::sunshine.username.empty() && !authenticate(response, request)) return;

    print_req(request);

    std::stringstream ss;
    std::stringstream configStream;
    ss << request->content.rdbuf();

    pt::ptree inputTree, outputTree;

    auto g = util::fail_guard([&]() {
      std::ostringstream data;
      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    try {
      // TODO: Input Validation
      pt::read_json(ss, inputTree);
      auto username = inputTree.count("currentUsername") > 0 ? inputTree.get<std::string>("currentUsername") : "";
      auto newUsername = inputTree.get<std::string>("newUsername");
      auto password = inputTree.count("currentPassword") > 0 ? inputTree.get<std::string>("currentPassword") : "";
      auto newPassword = inputTree.count("newPassword") > 0 ? inputTree.get<std::string>("newPassword") : "";
      auto confirmPassword = inputTree.count("confirmNewPassword") > 0 ? inputTree.get<std::string>("confirmNewPassword") : "";
      if (newUsername.length() == 0) newUsername = username;
      if (newUsername.length() == 0) {
        outputTree.put("status", false);
        outputTree.put("error", "Invalid Username");
      }
      else {
        auto hash = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();
        if (config::sunshine.username.empty() || (boost::iequals(username, config::sunshine.username) && hash == config::sunshine.password)) {
          if (newPassword.empty() || newPassword != confirmPassword) {
            outputTree.put("status", false);
            outputTree.put("error", "Password Mismatch");
          }
          else {
            http::save_user_creds(config::sunshine.credentials_file, newUsername, newPassword);
            http::reload_user_creds(config::sunshine.credentials_file);
            outputTree.put("status", true);
          }
        }
        else {
          outputTree.put("status", false);
          outputTree.put("error", "Invalid Current Credentials");
        }
      }
    }
    catch (std::exception &e) {
      BOOST_LOG(warning) << "SavePassword: "sv << e.what();
      outputTree.put("status", false);
      outputTree.put("error", e.what());
      return;
    }
  }

  void
  savePin(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();

    pt::ptree inputTree, outputTree;

    auto g = util::fail_guard([&]() {
      std::ostringstream data;
      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    try {
      // TODO: Input Validation
      pt::read_json(ss, inputTree);
      std::string pin = inputTree.get<std::string>("pin");
      std::string name = inputTree.get<std::string>("name");
      outputTree.put("status", nvhttp::pin(pin, name));
    }
    catch (std::exception &e) {
      BOOST_LOG(warning) << "SavePin: "sv << e.what();
      outputTree.put("status", false);
      outputTree.put("error", e.what());
      return;
    }
  }

  void
  unpairAll(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    pt::ptree outputTree;

    auto g = util::fail_guard([&]() {
      std::ostringstream data;
      pt::write_json(data, outputTree);
      response->write(data.str());
    });
    nvhttp::erase_all_clients();
    proc::proc.terminate();
    outputTree.put("status", true);
  }

  void
  unpair(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();

    pt::ptree inputTree, outputTree;

    auto g = util::fail_guard([&]() {
      std::ostringstream data;
      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    try {
      // TODO: Input Validation
      pt::read_json(ss, inputTree);
      std::string uuid = inputTree.get<std::string>("uuid");
      outputTree.put("status", nvhttp::unpair_client(uuid));
    }
    catch (std::exception &e) {
      BOOST_LOG(warning) << "Unpair: "sv << e.what();
      outputTree.put("status", false);
      outputTree.put("error", e.what());
      return;
    }
  }

  void
  listClients(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    pt::ptree named_certs = nvhttp::get_all_clients();

    pt::ptree outputTree;

    outputTree.put("status", false);

    auto g = util::fail_guard([&]() {
      std::ostringstream data;
      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    outputTree.add_child("named_certs", named_certs);
    outputTree.put("status", true);
  }

  void
  closeApp(resp_https_t response, req_https_t request) {
    if (!authenticate(response, request)) return;

    print_req(request);

    pt::ptree outputTree;

    auto g = util::fail_guard([&]() {
      std::ostringstream data;
      pt::write_json(data, outputTree);
      response->write(data.str());
    });

    proc::proc.terminate();
    outputTree.put("status", true);
  }

  void
  proxySteamApi(resp_https_t response, req_https_t request) {
    // 不需要认证，Steam API是公开的
    print_req(request);

    // 提取请求路径，移除/steam-api前缀
    std::string path = request->path;
    if (path.find("/steam-api") == 0) {
      path = path.substr(10); // 移除"/steam-api"前缀
    }

    // 构建目标URL
    std::string targetUrl = "https://api.steampowered.com" + path;
    
    // 添加查询参数
    if (!request->query_string.empty()) {
      targetUrl += "?" + request->query_string;
    }

    BOOST_LOG(info) << "Steam API代理请求: " << targetUrl;

    // 使用http模块下载数据
    std::string tempFile = platf::appdata().string() + "/temp_steam_api_" + std::to_string(std::time(nullptr));
    
    try {
      if (http::download_file(targetUrl, tempFile)) {
        // 读取文件内容
        std::ifstream file(tempFile, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        
        // 设置响应头
        SimpleWeb::CaseInsensitiveMultimap headers;
        headers.emplace("Content-Type", "application/json");
        headers.emplace("Access-Control-Allow-Origin", "*");
        headers.emplace("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        headers.emplace("Access-Control-Allow-Headers", "Content-Type, Authorization");
        
        response->write(SimpleWeb::StatusCode::success_ok, content, headers);
        
        // 清理临时文件
        std::remove(tempFile.c_str());
      } else {
        BOOST_LOG(error) << "Steam API请求失败: " << targetUrl;
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Steam API请求失败");
      }
    } catch (const std::exception& e) {
      BOOST_LOG(error) << "Steam API代理异常: " << e.what();
      response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Steam API代理异常");
      
      // 清理临时文件
      std::remove(tempFile.c_str());
    }
  }

  void
  proxySteamStore(resp_https_t response, req_https_t request) {
    // 不需要认证，Steam Store API是公开的
    print_req(request);

    // 提取请求路径，移除/steam-store前缀
    std::string path = request->path;
    if (path.find("/steam-store") == 0) {
      path = path.substr(12); // 移除"/steam-store"前缀
    }

    // 构建目标URL
    std::string targetUrl = "https://store.steampowered.com" + path;
    
    // 添加查询参数
    if (!request->query_string.empty()) {
      targetUrl += "?" + request->query_string;
    }

    BOOST_LOG(info) << "Steam Store代理请求: " << targetUrl;

    // 使用http模块下载数据
    std::string tempFile = platf::appdata().string() + "/temp_steam_store_" + std::to_string(std::time(nullptr));
    
    try {
      if (http::download_file(targetUrl, tempFile)) {
        // 读取文件内容
        std::ifstream file(tempFile, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        
        // 设置响应头
        SimpleWeb::CaseInsensitiveMultimap headers;
        headers.emplace("Content-Type", "application/json");
        headers.emplace("Access-Control-Allow-Origin", "*");
        headers.emplace("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        headers.emplace("Access-Control-Allow-Headers", "Content-Type, Authorization");
        
        response->write(SimpleWeb::StatusCode::success_ok, content, headers);
        
        // 清理临时文件
        std::remove(tempFile.c_str());
      } else {
        BOOST_LOG(error) << "Steam Store请求失败: " << targetUrl;
        response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Steam Store请求失败");
      }
    } catch (const std::exception& e) {
      BOOST_LOG(error) << "Steam Store代理异常: " << e.what();
      response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Steam Store代理异常");
      
      // 清理临时文件
      std::remove(tempFile.c_str());
    }
  }

  void
  start() {
    auto shutdown_event = mail::man->event<bool>(mail::shutdown);

    auto port_https = net::map_port(PORT_HTTPS);
    auto address_family = net::af_from_enum_string(config::sunshine.address_family);

    https_server_t server { config::nvhttp.cert, config::nvhttp.pkey };
    server.default_resource["GET"] = not_found;
    server.resource["^/$"]["GET"] = getIndexPage;
    server.resource["^/pin/?$"]["GET"] = getPinPage;
    server.resource["^/apps/?$"]["GET"] = getAppsPage;
    server.resource["^/clients/?$"]["GET"] = getClientsPage;
    server.resource["^/config/?$"]["GET"] = getConfigPage;
    server.resource["^/password/?$"]["GET"] = getPasswordPage;
    server.resource["^/welcome/?$"]["GET"] = getWelcomePage;
    server.resource["^/troubleshooting/?$"]["GET"] = getTroubleshootingPage;
    server.resource["^/api/pin$"]["POST"] = savePin;
    server.resource["^/api/apps$"]["GET"] = getApps;
    server.resource["^/api/logs$"]["GET"] = getLogs;
    server.resource["^/api/apps$"]["POST"] = saveApp;
    server.resource["^/api/config$"]["GET"] = getConfig;
    server.resource["^/api/config$"]["POST"] = saveConfig;
    server.resource["^/api/configLocale$"]["GET"] = getLocale;
    server.resource["^/api/restart$"]["POST"] = restart;
    server.resource["^/api/restart$"]["GET"] = restart;
    server.resource["^/api/boom$"]["GET"] = boom;
    server.resource["^/api/reset-display-device-persistence$"]["POST"] = resetDisplayDevicePersistence;
    server.resource["^/api/password$"]["POST"] = savePassword;
    server.resource["^/api/apps/([0-9]+)$"]["DELETE"] = deleteApp;
    server.resource["^/api/clients/unpair-all$"]["POST"] = unpairAll;
    server.resource["^/api/clients/list$"]["GET"] = listClients;
    server.resource["^/api/clients/list$"]["POST"] = saveConfig;
    server.resource["^/api/clients/unpair$"]["POST"] = unpair;
    server.resource["^/api/apps/close$"]["POST"] = closeApp;
    server.resource["^/api/covers/upload$"]["POST"] = uploadCover;
    server.resource["^/steam-api/.+$"]["GET"] = proxySteamApi;
    server.resource["^/steam-store/.+$"]["GET"] = proxySteamStore;
    server.resource["^/images/sunshine.ico$"]["GET"] = getFaviconImage;
    server.resource["^/images/logo-sunshine-45.png$"]["GET"] = getSunshineLogoImage;
    server.resource["^/boxart/.+$"]["GET"] = getBoxArt;
    server.resource["^/assets\\/.+$"]["GET"] = getNodeModules;
    server.config.reuse_address = true;
    server.config.address = net::af_to_any_address_string(address_family);
    server.config.port = port_https;

    auto accept_and_run = [&](auto *server) {
      try {
        server->start([](unsigned short port) {
          BOOST_LOG(info) << "Configuration UI available at [https://localhost:"sv << port << "]";
        });
      }
      catch (boost::system::system_error &err) {
        // It's possible the exception gets thrown after calling server->stop() from a different thread
        if (shutdown_event->peek()) {
          return;
        }

        BOOST_LOG(fatal) << "Couldn't start Configuration HTTPS server on port ["sv << port_https << "]: "sv << err.what();
        shutdown_event->raise(true);
        return;
      }
    };
    std::thread tcp { accept_and_run, &server };

    // Wait for any event
    shutdown_event->view();

    server.stop();

    tcp.join();
  }
}  // namespace confighttp
