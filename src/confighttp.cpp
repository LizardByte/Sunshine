//
// Created by TheElixZammuto on 2021-05-09.
//

#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include "process.h"

#include <fstream>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/json.hpp>

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
namespace fs   = std::filesystem;
namespace json = boost::json;

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
bool get_apps(const json::object &data, json::object &response) {

  json::array apps;
  for(auto &app : proc::proc.get_apps()) {
    apps.push_back(json::value_from(app));
  }

  response.emplace("apps", apps);
  return true;
}

bool save_app(const json::object &data, json::object &response) {
  if(!data.contains("id") ||
     !data.contains("name")) {
    response["error"] = "Provided app has missing fields.";
    return false;
  }
  try {
    auto id      = (int)data.at("id").as_int64();
    auto new_app = json::value_to<proc::ctx_t>(json::value(data));
    new_app.id   = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    proc::proc.add_app(id, new_app);
    proc::save(config::stream.file_apps);

    response["app_id"] = new_app.id;
    return true;
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "Failed to save a new app: " << e.what();
    response["error"] = "Failed to save a new app.";
    return false;
  }
}

bool delete_app(json::object &data, json::object &response) {
  if(!data.at("id").is_int64()) {
    response["error"] = "Invalid app id";
    return false;
  }

  try {
    auto id = data.at("id").as_int64();
    proc::proc.remove_app(id);
    proc::save(config::stream.file_apps);
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "Failed to remove an app: " << e.what();
    response["error"] = "Failed to save an app.";
    return false;
  }
  return true;
}

bool get_config(json::object &data, json::object &response) {
  auto vars = config::parse_config(read_file(config::sunshine.config_file.c_str()));

  response = config::config_to_json(std::move(vars));
  return true;
}

bool get_api_version(json::object &data, json::object &response) {
  response["version"]        = PROJECT_VER;
  response["setup_required"] = !http::creds_file_exists();
  response["platform"]       = SUNSHINE_PLATFORM;
  return true;
}

bool get_config_schema(json::object &data, json::object &response) {

  for(auto &[name, val] : config::property_schema) {
    json::object prop_info, limits;
    try {
      config::config_prop any_prop = val.first;
      prop_info["name"]            = any_prop.name;
      prop_info["description"]     = any_prop.description;
      prop_info["required"]        = any_prop.required;
      prop_info["type"]            = config::to_config_prop_string(any_prop.prop_type);

      val.second->to(limits);
      prop_info["limits"] = limits;
    }
    catch(const std::exception &e) {
      response["error"] = e.what();
      return false;
    }
    response[name] = prop_info;
  }
  return true;
}

bool save_config(json::object &data, json::object &response) {
  if(!data.at("config").is_string()) {
    response["error"] = "Config should be a valid JSON string";
    return false;
  }

  try {
    json::object config = json::parse(data.at("config").as_string()).as_object();
    config::save_config(std::move(config));
    return true;
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "SaveConfig: "sv << e.what();
    response["error"]     = "Failed to save config";
    response["exception"] = e.what();
  }
  return false;
}

bool save_pin(json::object &data, json::object &response) {
  try {
    auto pin           = std::string { data.at("pin").as_string() };
    response["status"] = nvhttp::pin(pin);
    return true;
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "SavePin: "sv << e.what();
    response["error"]     = "Failed to input pin";
    response["exception"] = e.what();
  }
  return false;
}

bool unpair_all(json::object &data, json::object &response) {
  nvhttp::erase_all_clients();
  return true;
}

bool close_app(json::object &data, json::object &response) {
  proc::proc.terminate();
  return true;
}

void sendSSEEvent(sse_event_type eventType) {

  std::string event_name;
  switch (eventType) {
  case NEW_LAUNCH_SESSION:
    event_name = "new_session";
    break;

  case REQUEST_PIN:
    event_name = "request_pin";
    break;
  }

  json::value v { { "type", event_name } };

  last_sse_event = json::serialize(v);
  sse_event_awaiter.notify_all();
}


bool update_credentials(json::object &data, json::object &response) {

  try {
    auto newUsername = std::string { data.at("newUsername").as_string() };
    auto newPassword = std::string { data.at("newPassword").as_string() };

    if(newUsername.length() < 4) {
      response["error"] = "Your username should have at least 4 characters.";
      return false;
    }
    if(newPassword.length() < 4) {
      response["error"] = "Your password should have at least 4 characters.";
      return false;
    }

    http::save_user_creds(config::sunshine.credentials_file, newUsername, newPassword);
    http::reload_user_creds(config::sunshine.credentials_file);

    return true;
  }
  catch(std::exception &e) {
    response["error"] = "Invalid input JSON";
    return false;
  }
}

bool upload_cover(json::object &data, json::object &response) {

  if(!data.contains("key") || !data.at("key").is_string()) {
    response["error"] = "Cover key is required";
    return false;
  }
  auto key = std::string { data.at("key").as_string() };

  std::string url;
  if(data.contains("url") &&
     data.at("url").is_string()) url = std::string { data.at("url").as_string() };

  const std::string coverdir = platf::appdata().string() + "/covers/";
  if(!boost::filesystem::exists(coverdir)) {
    boost::filesystem::create_directory(coverdir);
  }

  std::basic_string path = coverdir + http::url_escape(key) + ".png";
  if(!url.empty()) {
    if(http::url_get_host(url) != "images.igdb.com") {
      response["error"] = "Only images.igdb.com is allowed";
      return false;
    }
    if(!http::download_file(url, path)) {
      response["error"] = "Failed to download cover";
      return false;
    }
  }
  else {
    if(data.contains("data") && data.at("data").is_string()) {
      std::string binaryStr = std::string { data.at("data").as_string() };
      auto binaryImage      = SimpleWeb::Crypto::Base64::decode(binaryStr);
      std::ofstream imgfile(path);
      imgfile.write(binaryImage.data(), (int)binaryStr.size());
    }
    else {
      response["error"] = "Data needs to be a binary string";
      return false;
    }
  }

  response["path"] = path;
  return true;
}

std::map<std::string, std::pair<req_type, std::function<bool(json::object &, json::object &)>>> allowedRequests = {
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

void handleApiSSE(const resp_http_t &response, const req_http_t &request) {
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

  json::object input;
  json::object outputJson;
  bool result = false;
  try {
    if(request->content.size() > 0) {
      input = json::parse(request->content.string()).as_object();
    }
  }
  catch(std::exception &e) {
    BOOST_LOG(info) << "Web API: Invalid input JSON\n";
  }

  auto req = allowedRequests.find(req_name);
  if(req != allowedRequests.end()) {
    if(req->second.first == requestMethod) {
      result = req->second.second(input, outputJson);
    }
    else {
      outputJson["error"] = "Invalid request method";
    }
  }

  outputJson["result"]        = result;
  outputJson["authenticated"] = authResult == 0;
  response->write(json::serialize(outputJson), { { "Access-Control-Allow-Origin", "*" }, { "Access-Control-Allow-Headers", "Authorization" } });
}

void appasset(const resp_http_t &response, const req_http_t &request) {
  if(!checkRequestOrigin(request)) return;
  print_req(request);

  auto appid     = request->path_match[1].str();
  auto app_image = proc::proc.get_app_image(util::from_view(appid));

  std::ifstream in(app_image, std::ios::binary);
  response->write(SimpleWeb::StatusCode::success_ok, in, { { "Content-Type", "image/png" } });
}

void handleCors(const resp_http_t &response, const req_http_t &request) {
  auto allowHeaders = "Authorization"s;
  response->write({ { "Access-Control-Allow-Origin", "*" }, { "Access-Control-Allow-Headers", allowHeaders } });
}

void serveUI(const resp_http_t &response, const req_http_t &request) {
  print_req(request);

  SimpleWeb::CaseInsensitiveMultimap headers;
  std::ios::openmode fileOpenMode;

  std::string path = WEB_DIR + request->path;
  if(request->path == "/") {
    path = WEB_DIR "/index.html";
  }
  else if(boost::algorithm::iends_with(request->path, ".ttf")) {
    fileOpenMode = std::ios::binary;
    headers.emplace("Content-Type", "font/ttf");
  }
  else if(boost::algorithm::iends_with(request->path, ".woff2")) {
    fileOpenMode = std::ios::binary;
    headers.emplace("Content-Type", "font/woff2");
  }
  else if(boost::algorithm::iends_with(request->path, ".jpg")) {
    fileOpenMode = std::ios::binary;
    headers.emplace("Content-Type", "image/jpeg");
  }
  else if(boost::algorithm::iends_with(request->path, ".svg")) {
    headers.emplace("Content-Type", "image/svg+xml");
  }
  else if(boost::algorithm::iends_with(request->path, ".js")) {
    headers.emplace("Content-Type", "application/javascript");
  }
  else if(boost::algorithm::iends_with(request->path, ".css")) {
    headers.emplace("Content-Type", "text/css");
  }
  else {
    path = path + ".html";
  }

  std::string relative = std::filesystem::relative(path, WEB_DIR).string();
  if((relative.size() == 1 || (relative[0] != '.' && relative[1] != '.')) &&
     std::filesystem::exists(path) &&
     !std::filesystem::is_directory(path)) {
    std::ifstream in(path.c_str(), fileOpenMode);
    response->write(SimpleWeb::StatusCode::success_ok, in, headers);
  }
  else {
    response->write(SimpleWeb::StatusCode::client_error_not_found);
  }
}

void start() {
  auto shutdown_event = mail::man->event<bool>(mail::shutdown);

  auto port_http = map_port(PORT_HTTP);

  http_server_t server;
  server.resource["^/api/([a-z_]+)$"]["OPTIONS"] = handleCors;
  server.resource["^/api/([a-z_]+)$"]["POST"]    = handleApiRequest;
  server.resource["^/api/([a-z_]+)$"]["GET"]     = handleApiRequest;
  server.resource["^/appasset/([0-9]+)$"]["GET"] = appasset;
  server.default_resource["GET"]                 = serveUI;
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