//
// Created by TheElixZammuto on 2021-05-09.
// TODO: Authentication, better handling of routes common to nvhttp, cleanup

#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include "process.h"

#include <filesystem>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <boost/asio/ssl/context.hpp>

#include <Simple-Web-Server/crypto.hpp>
#include <Simple-Web-Server/server_http.hpp>
#include <boost/asio/ssl/context_base.hpp>

#include "config.h"
#include "confighttp.h"
#include "crypto.h"
#include "httpcommon.h"
#include "main.h"
#include "network.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "rtsp.h"
#include "utility.h"
#include "uuid.h"
#include "version.h"
#include <boost/locale.hpp>

using namespace std::literals;

namespace confighttp {
namespace fs = std::filesystem;
namespace pt = boost::property_tree;

using http_server_t = SimpleWeb::Server<SimpleWeb::HTTP>;

using args_t       = SimpleWeb::CaseInsensitiveMultimap;
using resp_http_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response>;
using req_http_t  = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request>;

class token_info {
public:
  std::string userAgent;
  time_t expiresIn;
};

enum class req_type {
  POST,
  GET
};

std::mutex mtx;
std::condition_variable sse_event_awaiter;
std::string last_sse_event;
std::map<std::string, token_info> accessTokens;

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
bool get_apps(pt::ptree data, pt::ptree &response) {
  std::string content = read_file(config::stream.file_apps.c_str());
  response.put("content", content);
  return true;
}

bool save_app(pt::ptree data, pt::ptree &response) {
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
    if(newNode == true) {
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

bool delete_app(pt::ptree data, pt::ptree &response) {
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
    if(success == false) {
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

bool get_config(pt::ptree data, pt::ptree &response) {
  response.put("platform", SUNSHINE_PLATFORM);

  auto vars = config::parse_config(read_file(config::sunshine.config_file.c_str()));

  for(auto &[name, value] : vars) {
    response.put(std::move(name), std::move(value));
  }
  return true;
}

bool get_api_version(pt::ptree data, pt::ptree &response) {
  response.put("version", PROJECT_VER);
  return true;
}

bool get_config_schema(pt::ptree data, pt::ptree &response) {

  for(auto &[name, val] : config::property_schema) {
    pt::ptree prop_info, limits;
    try {
      config::config_prop any_prop = val.first;
      prop_info.put("name", any_prop.name);
      prop_info.put("translated_name", boost::locale::translate(any_prop.name));
      prop_info.put("description", any_prop.description);
      val.second->to(limits);
      prop_info.push_back(std::make_pair("limits", limits));
      prop_info.put("translated_description", boost::locale::translate(any_prop.description));
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

bool save_config(pt::ptree data, pt::ptree &response) {
  std::stringstream contentStream, configStream;
  contentStream << data.get<std::string>("config");
  try {
    pt::ptree inputTree;
    pt::read_json(contentStream, inputTree);

    std::unordered_map<std::string, std::string> vars;
    for(const auto &kv : inputTree) {
      std::string value = inputTree.get<std::string>(kv.first);
      if(value.length() == 0 || value.compare("null") == 0) continue;

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

bool save_pin(pt::ptree data, pt::ptree &response) {
  try {
    std::string pin = data.get<std::string>("pin");
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

bool unpair_all(pt::ptree data, pt::ptree &response) {
  nvhttp::erase_all_clients();
  return true;
}

bool close_app(pt::ptree data, pt::ptree &response) {
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

bool change_password(pt::ptree data, pt::ptree &response) {
  auto oldPasswordHash = data.get_optional<std::string>("oldPassword").get_value_or("");
  auto newPasswordHash = data.get_optional<std::string>("password").get_value_or("");

  if(oldPasswordHash.length() < 64 || newPasswordHash.length() < 64) {
    response.put("error", "Invalid hash length.");
    return false;
  }

  int result = http::renew_credentials(oldPasswordHash, newPasswordHash);

  if(result == -3) {
    response.put("error", "credentials_file_missing");
    return false;
  }
  if(result == -4) {
    response.put("error", "invalid_old_password");
    return false;
  }
  if(result == 0) {
    response.put("error", "save_credentials_failed");
    return false;
  }
  return true;
}

std::map<std::string, std::pair<req_type, std::function<bool(pt::ptree, pt::ptree &)>>> allowedRequests = {
  { "get_apps"s, { req_type::GET, get_apps } },
  { "api_version"s, { req_type::GET, get_api_version } },
  { "get_config"s, { req_type::GET, get_config } },
  { "save_app"s, { req_type::POST, save_app } },
  { "delete_app"s, { req_type::POST, delete_app } },
  { "save_config"s, { req_type::POST, save_config } },
  { "save_pin"s, { req_type::POST, save_pin } },
  { "change_password"s, { req_type::POST, change_password } },
  { "unpair_all"s, { req_type::POST, unpair_all } },
  { "close_app"s, { req_type::POST, close_app } },
  { "get_config_schema"s, { req_type::GET, get_config_schema } }
};

bool checkRequestOrigin(req_http_t request) {
  auto address = request->remote_endpoint_address();
  auto ip_type = net::from_address(address);

  // Only allow HTTP requests from localhost.
  if(ip_type > net::net_e::PC) {
    BOOST_LOG(info) << "Web API: ["sv << address << "] -- denied"sv;
    return false;
  }

  return true;
}

void handleApiSSE(resp_http_t response, req_http_t request) {
  if(checkRequestOrigin(request) == false) return;

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
      std::promise<bool> error;
      std::unique_lock<std::mutex> lck(mtx);
      if(sse_event_awaiter.wait_for(lck, std::chrono::minutes(2)) == std::cv_status::timeout) {
        *response << "event: ping\r\n\r\n";
      }
      else {
        *response << "data: " << last_sse_event << "\r\n\r\n";
      }
      response->send([&error](const SimpleWeb::error_code &ec) {
        error.set_value(static_cast<bool>(ec));
      });
      if(error.get_future().get()) // If ec above indicates error
        break;
    }
  });
  work_thread.detach();
}

int checkAuthentication(req_http_t request, bool tokenRequired) {
  auto authorizationItr = request->header.find("Authorization");
  auto userAgentItr     = request->header.find("User-Agent");
  if((tokenRequired == true && authorizationItr == request->header.end()) || userAgentItr == request->header.end()) {
    return -2;
  }

  if(tokenRequired == false) {
    auto client_hex = request->content.string();
    return http::load_credentials(client_hex) ? 0 : -1;
  }
  else {
    auto token        = authorizationItr->second.substr("Bearer "sv.length());
    auto tokenInfoItr = accessTokens.find(token);
    if(tokenInfoItr == accessTokens.end()) {
      return -1;
    }
    auto currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    if(currentTime >= tokenInfoItr->second.expiresIn || tokenInfoItr->second.userAgent != userAgentItr->second) {
      return -1;
    }
    return 0;
  }
}

void handleApiAuthentication(resp_http_t response, req_http_t request) {
  if(checkRequestOrigin(request) == false) return;

  int authResult = checkAuthentication(request, false);
  if(authResult == -2) {
    response->write(SimpleWeb::StatusCode::client_error_bad_request, { { "Access-Control-Allow-Origin", "*" } , { "Access-Control-Allow-Headers", "Authorization" } });
    return;
  }
  else if(authResult == -1) {
    response->write(SimpleWeb::StatusCode::client_error_unauthorized, { { "Access-Control-Allow-Origin", "*" } , { "Access-Control-Allow-Headers", "Authorization" } });
    return;
  }
  auto userAgentItr = request->header.find("User-Agent");

  std::string token = crypto::rand_alphabet(64);
  time_t expiresIn  = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() + config::sunshine.tokenLifetime);
  token_info info { userAgentItr->second, expiresIn };
  accessTokens.insert({ token, info });
  response->write(token, { { "Access-Control-Allow-Origin", "*" }, { "Access-Control-Allow-Headers", "Authorization" } });
}

void handleApiRequest(resp_http_t response, req_http_t request) {
  print_req(request);
  auto locale     = request->path_match[1].str();
  auto req_name   = request->path_match[2].str();
  auto authResult = checkAuthentication(request, true);
  if(req_name != "api_version"s) {
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

void appasset(resp_http_t response, req_http_t request) {
  if(checkRequestOrigin(request) == false) return;
  print_req(request);

  auto appid     = request->path_match[1].str();
  auto app_image = proc::proc.get_app_image(util::from_view(appid));

  std::ifstream in(app_image, std::ios::binary);
  response->write(SimpleWeb::StatusCode::success_ok, in, { { "Content-Type", "image/png" } });
}

void handleCors(resp_http_t response, req_http_t request) {
  auto corsHeaders = request->header.find("access-control-request-headers");

  auto allowHeaders = "Authorization"s;
  if (corsHeaders != request->header.end()) {
    allowHeaders = corsHeaders->second;
  }
  response->write({ { "Access-Control-Allow-Origin", "*" }, { "Access-Control-Allow-Headers", allowHeaders } });
}

void start() {
  auto shutdown_event = mail::man->event<bool>(mail::shutdown);

  auto port_http = map_port(PORT_HTTP);

  http_server_t server;
  server.resource["^/api/events$"]["GET"]                  = handleApiSSE;
  server.resource["^/api/authenticate$"]["POST"]           = handleApiAuthentication;
  server.resource["^/api/authenticate$"]["OPTIONS"]        = handleCors;
  server.resource["^/api/([a-z_]+)/([a-z_]+)$"]["OPTIONS"] = handleCors;
  server.resource["^/api/([a-z_]+)/([a-z_]+)$"]["POST"]    = handleApiRequest;
  server.resource["^/api/([a-z_]+)/([a-z_]+)$"]["GET"]     = handleApiRequest;
  server.resource["^/appasset/([0-9]+)$"]["GET"]           = appasset;
  server.config.reuse_address                              = true;
  server.config.address                                    = "0.0.0.0"s;
  server.config.port                                       = port_http;
  server.config.timeout_content                            = 0;

  try {
    server.bind();
    BOOST_LOG(info) << "Configuration API available at [http://localhost:"sv << port_http << "]";
  }
  catch(boost::system::system_error &err) {
    BOOST_LOG(fatal) << "Couldn't bind http server to ports ["sv << port_http << "]: "sv << err.what();

    shutdown_event->raise(true);
    return;
  }
  auto accept_and_run = [&](auto *server) {
    try {
      server->accept_and_run();
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
