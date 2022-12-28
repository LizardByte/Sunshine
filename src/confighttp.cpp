//
// Created by TheElixZammuto on 2021-05-09.
// TODO: Authentication, better handling of routes common to nvhttp, cleanup

#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include "process.h"

#include <filesystem>

#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <Simple-Web-Server/crypto.hpp>
#include <Simple-Web-Server/server_http.hpp>

#include "config.h"
#include "confighttp.h"
#include "crypto.h"
#include "httpcommon.h"
#include "main.h"
#include "network.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "utility.h"
#include "uuid.h"
#include "version.h"

using namespace std::literals;

namespace confighttp {
namespace fs = std::filesystem;
namespace pt = boost::property_tree;

using http_server_t = SimpleWeb::Server<SimpleWeb::HTTP>;

using resp_http_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response>;
using req_http_t  = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request>;

enum class req_type {
  POST,
  GET
};

std::mutex mtx;
std::condition_variable sse_event_awaiter;
std::string last_sse_event;

void print_req(const req_http_t &request) {
  BOOST_LOG(debug) << "METHOD :: "sv << request->method;
  BOOST_LOG(debug) << "DESTINATION :: "sv << request->path;

  for(auto &[name, val] : request->header) {
    BOOST_LOG(debug) << name << " -- " << val;
  }

  BOOST_LOG(debug) << " [--] "sv;

  for(auto &[name, val] : request->parse_query_string()) {
    BOOST_LOG(debug) << name << " -- " << val;
  }

  BOOST_LOG(debug) << " [--] "sv;
}
bool get_apps(const pt::ptree &data, pt::ptree &response) {
  std::string content = read_file(config::stream.file_apps.c_str());
  response.put("content", content);
  return true;
}

bool save_app(const pt::ptree &data, pt::ptree &response) {
  pt::ptree fileTree;

  try {
    pt::read_json(config::stream.file_apps, fileTree);

    auto &apps_node = fileTree.get_child("apps"s);
    auto id         = data.get<std::string>("id");

    pt::ptree newApps;
    bool newNode = true;
    for(const auto &kv : apps_node) {
      if(kv.second.get<std::string>("id") == id) {
        newApps.push_back(std::make_pair("", data));
        newNode = false;
      }
      else {
        newApps.push_back(std::make_pair("", kv.second));
      }
    }
    if(newNode) {
      newApps.push_back(std::make_pair("", data));
    }
    fileTree.erase("apps");
    fileTree.push_back(std::make_pair("apps", newApps));

    pt::write_json(config::stream.file_apps, fileTree);
    proc::refresh(config::stream.file_apps);
    return true;
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "SaveApp: "sv << e.what();
    response.put("error", "Invalid Input JSON");
    return false;
  }
}

bool delete_app(const pt::ptree &data, pt::ptree &response) {
  pt::ptree fileTree;
  try {
    pt::read_json(config::stream.file_apps, fileTree);
    auto &apps_node = fileTree.get_child("apps"s);
    auto id         = data.get<std::string>("id");

    pt::ptree newApps;
    bool success = false;
    for(const auto &kv : apps_node) {
      if(id == kv.second.get<std::string>("id")) {
        success = true;
      }
      else {
        newApps.push_back(std::make_pair("", kv.second));
      }
    }
    if(!success) {
      response.put("error", "No such app ID");
      return false;
    }
    fileTree.erase("apps");
    fileTree.push_back(std::make_pair("apps", newApps));
    pt::write_json(config::stream.file_apps, fileTree);
    proc::refresh(config::stream.file_apps);
    return true;
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "DeleteApp: "sv << e.what();
    response.put("error", "Invalid File JSON");
    return false;
  }
}

bool get_config(const pt::ptree &data, pt::ptree &response) {
  auto vars = config::parse_config(read_file(config::sunshine.config_file.c_str()));

  for(auto &[name, value] : vars) {
    response.put(name, value);
  }
  return true;
}

bool get_api_version(const pt::ptree &data, pt::ptree &response) {
  response.put("version", PROJECT_VER);
  response.put("setup_required", !http::creds_file_exists());
  response.put("platform", SUNSHINE_PLATFORM);
  return true;
}

bool get_config_schema(const pt::ptree &data, pt::ptree &response) {

  for(auto &[name, val] : config::property_schema) {
    pt::ptree prop_info, limits;
    try {
      config::config_prop any_prop = val.first;
      prop_info.put("name", any_prop.name);
      prop_info.put("translated_name", any_prop.name);
      prop_info.put("description", any_prop.description);
      val.second->to(limits);
      prop_info.push_back(std::make_pair("limits", limits));
      prop_info.put("translated_description", any_prop.description);
      prop_info.put("required", any_prop.required);
      prop_info.put("type", config::to_config_prop_string(any_prop.prop_type));
    }
    catch(const std::exception &e) {
      response.put("error", e.what());
      return false;
    }
    response.push_back(std::make_pair(name, prop_info));
  }
  return true;
}

bool save_config(pt::ptree &data, pt::ptree &response) {
  std::stringstream contentStream, configStream;
  contentStream << data.get<std::string>("config");
  try {
    pt::ptree inputTree;
    pt::read_json(contentStream, inputTree);

    std::unordered_map<std::string, std::string> vars;
    for(const auto &kv : inputTree) {
      auto value = inputTree.get<std::string>(kv.first);
      if(value.length() == 0 || value == "null") continue;

      vars[kv.first] = value;
    }
    config::save_config(std::move(vars));
    return true;
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "SaveConfig: "sv << e.what();
    response.put("error", "exception");
    response.put("exception", e.what());
  }
  return false;
}

bool save_pin(pt::ptree &data, pt::ptree &response) {
  try {
    auto pin = data.get<std::string>("pin");
    response.put("status", nvhttp::pin(pin));
    return true;
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "SavePin: "sv << e.what();
    response.put("error", "exception");
    response.put("exception", e.what());
  }
  return false;
}

bool unpair_all(pt::ptree &data, pt::ptree &response) {
  nvhttp::erase_all_clients();
  return true;
}

bool close_app(pt::ptree &data, pt::ptree &response) {
  proc::proc.terminate();
  return true;
}

bool request_pin() {
  pt::ptree output;
  output.put("type", "request_pin");

  std::ostringstream data;
  pt::write_json(data, output, false);
  last_sse_event = data.str();
  sse_event_awaiter.notify_all();
  return true;
}


bool update_credentials(pt::ptree &data, pt::ptree &response) {
  auto newUsername = data.get_optional<std::string>("newUsername").get_value_or("");
  auto newPassword = data.get_optional<std::string>("newPassword").get_value_or("");

  if(newUsername.length() < 4) {
    response.put("error", "Your username should have at least 4 characters.");
    return false;
  }
  if(newPassword.length() < 4) {
    response.put("error", "Your password should have at least 4 characters.");
    return false;
  }

  http::save_user_creds(config::sunshine.credentials_file, newUsername, newPassword);
  http::reload_user_creds(config::sunshine.credentials_file);

  return true;
}

bool upload_cover(pt::ptree &data, pt::ptree &response) {

  auto key = data.get("key", "");
  if(key.empty()) {
    response.put("error", "Cover key is required");
    return false;
  }
  auto url = data.get("url", "");

  const std::string coverdir = platf::appdata().string() + "/covers/";
  if(!boost::filesystem::exists(coverdir)) {
    boost::filesystem::create_directory(coverdir);
  }

  std::basic_string path = coverdir + http::url_escape(key) + ".png";
  if(!url.empty()) {
    if(http::url_get_host(url) != "images.igdb.com") {
      response.put("error", "Only images.igdb.com is allowed");
      return false;
    }
    if(!http::download_file(url, path)) {
      response.put("error", "Failed to download cover");
      return false;
    }
  }
  else {
    auto binaryImage = SimpleWeb::Crypto::Base64::decode(data.get<std::string>("data"));

    std::ofstream imgfile(path);
    imgfile.write(binaryImage.data(), (int)data.size());
  }
  response.put("path", path);

  return true;
}

std::map<std::string, std::pair<req_type, std::function<bool(pt::ptree &, pt::ptree &)>>> allowedRequests = {
  { "get_apps"s, { req_type::GET, get_apps } },
  { "api_version"s, { req_type::GET, get_api_version } },
  { "get_config"s, { req_type::GET, get_config } },
  { "save_app"s, { req_type::POST, save_app } },
  { "delete_app"s, { req_type::POST, delete_app } },
  { "save_config"s, { req_type::POST, save_config } },
  { "save_pin"s, { req_type::POST, save_pin } },
  { "update_credentials"s, { req_type::POST, update_credentials } },
  { "unpair_all"s, { req_type::POST, unpair_all } },
  { "close_app"s, { req_type::POST, close_app } },
  { "upload_cover"s, { req_type::POST, upload_cover } },
  { "get_config_schema"s, { req_type::GET, get_config_schema } }
};

bool checkRequestOrigin(const req_http_t &request) {
  auto address = request->remote_endpoint().address().to_string();
  auto ip_type = net::from_address(address);

  // Only allow HTTP requests from localhost.
  if(ip_type > net::net_e::PC) {
    BOOST_LOG(info) << "Web API: ["sv << address << "] -- denied"sv;
    return false;
  }

  return true;
}

void handleApiSSE(resp_http_t response, const req_http_t &request) {
  if(!checkRequestOrigin(request)) return;

  std::thread work_thread([response, request] {
    response->close_connection_after_response = true;
    std::promise<bool> error;
    response->write({ { "Content-Type", "text/event-stream" }, { "Access-Control-Allow-Origin", "*" } });
    response->send([&error](const SimpleWeb::error_code &ec) {
      error.set_value(static_cast<bool>(ec));
    });
    if(error.get_future().get())
      return; // return if error on sending headers
    while(true) {
      std::promise<bool> responseError;
      std::unique_lock<std::mutex> lck(mtx);
      if(sse_event_awaiter.wait_for(lck, std::chrono::minutes(2)) == std::cv_status::timeout) {
        *response << "event: ping\r\n\r\n";
      }
      else {
        *response << "data: " << last_sse_event << "\r\n\r\n";
      }
      response->send([&responseError](const SimpleWeb::error_code &ec) {
        responseError.set_value(static_cast<bool>(ec));
      });
      if(responseError.get_future().get()) // If ec above indicates responseError
        break;
    }
  });
  work_thread.detach();
}

int checkAuthentication(const req_http_t &request) {
  auto authorizationItr = request->header.find("Authorization");
  if(authorizationItr == request->header.end()) {
    return -2;
  }

  auto authData = SimpleWeb::Crypto::Base64::decode(authorizationItr->second.substr("Basic "sv.length()));
  auto index    = authData.find(':');
  if(index >= authData.size() - 1) {
    return -2;
  }

  auto username = authData.substr(0, index);
  auto password = authData.substr(index + 1);
  auto hash     = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();

  if(username != config::sunshine.username || hash != config::sunshine.password) {
    return -1;
  }

  return 0;
}

void handleApiRequest(const resp_http_t &response, const req_http_t &request) {
  print_req(request);
  auto req_name   = request->path_match[1].str();
  auto authResult = checkAuthentication(request);

  if(req_name == "events") {
    handleApiSSE(response, request);
    return;
  }

  if(req_name != "update_credentials"s && req_name != "api_version"s) {
    if(authResult == -2) {
      response->write(SimpleWeb::StatusCode::client_error_bad_request);
      return;
    }
    else if(authResult != 0) {
      response->write(SimpleWeb::StatusCode::client_error_unauthorized);
      return;
    }
  }

  req_type requestMethod = req_type::GET;
  if(request->method == "POST"s) {
    requestMethod = req_type::POST;
  }

  pt::ptree inputTree, outputTree;
  std::stringstream contentStream;
  bool result = false;
  contentStream << request->content.string();
  try {
    pt::read_json(contentStream, inputTree);
  }
  catch(...) {
  }

  auto req = allowedRequests.find(req_name);
  if(req != allowedRequests.end()) {
    if(req->second.first == requestMethod) {
      result = req->second.second(inputTree, outputTree);
    }
    else {
      outputTree.put("error", "Invalid request method");
    }
  }
  outputTree.put("result", result);
  outputTree.put("authenticated", authResult == 0);
  std::ostringstream data;
  pt::write_json(data, outputTree, false);
  response->write(data.str(), { { "Access-Control-Allow-Origin", "*" }, { "Access-Control-Allow-Headers", "Authorization" } });
}

void appasset(resp_http_t response, const req_http_t &request) {
  if(!checkRequestOrigin(request)) return;
  print_req(request);

  auto appid     = request->path_match[1].str();
  auto app_image = proc::proc.get_app_image(util::from_view(appid));

  std::ifstream in(app_image, std::ios::binary);
  response->write(SimpleWeb::StatusCode::success_ok, in, { { "Content-Type", "image/png" } });
}

void handleCors(resp_http_t response, const req_http_t &request) {
  auto allowHeaders = "Authorization"s;
  response->write({ { "Access-Control-Allow-Origin", "*" }, { "Access-Control-Allow-Headers", allowHeaders } });
}

void start() {
  auto shutdown_event = mail::man->event<bool>(mail::shutdown);

  auto port_http = map_port(PORT_HTTP);

  http_server_t server;
  server.resource["^/api/([a-z_]+)$"]["OPTIONS"] = handleCors;
  server.resource["^/api/([a-z_]+)$"]["POST"]    = handleApiRequest;
  server.resource["^/api/([a-z_]+)$"]["GET"]     = handleApiRequest;
  server.resource["^/appasset/([0-9]+)$"]["GET"] = appasset;
  server.config.reuse_address                    = true;
  server.config.address                          = "0.0.0.0"s;
  server.config.port                             = port_http;
  server.config.timeout_content                  = 0;

  auto accept_and_run = [&](auto *server) {
    try {
      server->start([](unsigned short port) {
        BOOST_LOG(info) << "Configuration API available at [http://localhost:"sv << port << "]";
      });
    }
    catch(boost::system::system_error &err) {
      // It's possible the exception gets thrown after calling server->stop() from a different thread
      if(shutdown_event->peek()) {
        return;
      }

      BOOST_LOG(fatal) << "Couldn't start Configuration HTTP server on port ["sv << port_http << "]: "sv << err.what();
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
} // namespace confighttp