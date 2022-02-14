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

#include <Simple-WebSocket-Server/crypto.hpp>
#include <Simple-WebSocket-Server/server_wss.hpp>
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

using namespace std::literals;

namespace confighttp {
namespace fs = std::filesystem;
namespace pt = boost::property_tree;

using wss_server_t = SimpleWeb::SocketServer<SimpleWeb::WSS>;
using args_t       = SimpleWeb::CaseInsensitiveMultimap;

confighttp::wss_server_t *server;

bool get_apps(pt::ptree data, pt::ptree* response) {
  std::string content = read_file(config::stream.file_apps.c_str());
  (*response).put("content", content);
  return true;
}

bool save_app(pt::ptree data, pt::ptree* response) {
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
    if (newNode == true) {
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
    (*response).put("error", "Invalid Input JSON");
    return false;
  }
}

bool delete_app(pt::ptree data, pt::ptree* response) {
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
      } else {
        newApps.push_back(std::make_pair("", kv.second));
      }
    }
    if (success == false) {
      (*response).put("error", "No such app ID");
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
    (*response).put("error", "Invalid File JSON");
    return false;
  }
}

bool get_config(pt::ptree data, pt::ptree* response) {
  (*response).put("platform", SUNSHINE_PLATFORM);

  auto vars = config::parse_config(read_file(config::sunshine.config_file.c_str()));

  for(auto &[name, value] : vars) {
    (*response).put(std::move(name), std::move(value));
  }
  return true;
}

bool save_config(pt::ptree data, pt::ptree* response) {

  std::stringstream contentStream;
  std::stringstream configStream;
  contentStream << data.get<std::string>("config");
  try {
    //TODO: Input Validation. Maybe this can be done using config::apply_config?
    pt::ptree inputTree;
    pt::read_json(contentStream, inputTree);
    for(const auto &kv : inputTree) {
      std::string value = inputTree.get<std::string>(kv.first);
      if(value.length() == 0 || value.compare("null") == 0) continue;

      configStream << kv.first << " = " << value << std::endl;
    }
    write_file(config::sunshine.config_file.c_str(), configStream.str());
    return true;
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "SaveConfig: "sv << e.what();
    (*response).put("error", "exception");
    (*response).put("exception", e.what());
  }
  return false;
}

bool save_pin(pt::ptree data, pt::ptree* response) {
  try {
    //TODO: Input Validation
    std::string pin = data.get<std::string>("pin");
    (*response).put("status", nvhttp::pin(pin));
    return true;
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "SavePin: "sv << e.what();
    (*response).put("error", "exception");
    (*response).put("exception", e.what());
  }
  return false;
}

bool unpair_all(pt::ptree data, pt::ptree* response) {
  nvhttp::erase_all_clients();
  return true;
}

bool close_app(pt::ptree data, pt::ptree* response) {
  proc::proc.terminate();
  return true;
}

bool request_pin() {
  auto out_message = std::make_shared<wss_server_t::OutMessage>();
  pt::ptree output;
  output.put("type", "request_pin");

  std::ostringstream data;
  pt::write_json(data, output);
  *out_message << data.str();

  for (auto &a_connection : server->get_connections())
      a_connection->send(out_message);

  return true;
}


void start() {
  auto shutdown_event = mail::man->event<bool>(mail::shutdown);

  auto port_https = map_port(PORT_HTTPS);

  server = new wss_server_t(config::nvhttp.cert, config::nvhttp.pkey);
  server->config.reuse_address  = true;
  server->config.address        = "0.0.0.0"s;
  server->config.port           = port_https;

  std::map<std::string, std::function<bool(pt::ptree, pt::ptree*)>> allowedRequests = {
    { "get_apps"s, get_apps },
    { "save_app"s, save_app },
    { "delete_app"s, delete_app },
    { "get_config"s, get_config },
    { "save_config"s, save_config },
    { "save_pin"s, save_pin },
    { "unpair_all"s, unpair_all },
    { "close_app"s, close_app },
  };
  
  auto &echo = server->endpoint["^/v1/?$"];
  echo.on_handshake = [](std::shared_ptr<wss_server_t::Connection> connection, SimpleWeb::CaseInsensitiveMultimap & /*response_header*/) {
    auto query = connection->query_string;
    auto ip_type = net::from_address(connection->remote_endpoint_address());

    if(ip_type <= http::origin_web_api_allowed) {
      if (query.length() > "token="sv.length()) {
        auto token = query.substr(6);
        if (token == config::sunshine.token) {
          BOOST_LOG(info) << "WebAPI: connection -- accepted"s;
          return SimpleWeb::StatusCode::information_switching_protocols; // Upgrade to websocket
        }
      }
    }
    BOOST_LOG(info) << "WebAPI: connection -- forbidden"s;
    return SimpleWeb::StatusCode::client_error_forbidden;
  };
  echo.on_message = [&allowedRequests](std::shared_ptr<wss_server_t::Connection> connection, std::shared_ptr<wss_server_t::InMessage> in_message) {
    pt::ptree inputTree, outputTree;
    bool result = false;
    try {
      std::stringstream ss;
      ss << in_message->string();
      pt::read_json(ss, inputTree);
      if (inputTree.count("type") > 0) {
        auto reqeuestItr = allowedRequests.find(inputTree.get<std::string>("type"));
        if (reqeuestItr != allowedRequests.end()) {
          result = (reqeuestItr->second)(inputTree.get_child("data"), &outputTree);
          outputTree.put("type", inputTree.get<std::string>("type"));
        } else {
          outputTree.put("error", "This request doesn't exist.");
        }
      }
    } 
    catch (std::exception &e) {
      outputTree.put("error", "exception");
      outputTree.put("exception", e.what());
    }
    outputTree.put("result", result);
    auto out_message = std::make_shared<wss_server_t::OutMessage>();
    std::ostringstream data;
    pt::write_json(data, outputTree);
    *out_message << data.str();
    connection->send(out_message);
  };

  std::thread server_thread([&port_https, &shutdown_event]() {
    // Start server
    try {
      server->start([](unsigned short port) {
        BOOST_LOG(info) << "Configuration API available at [wss://localhost:"sv << port << "/v1]";
        BOOST_LOG(info) << "Configuration API Token ["sv << config::sunshine.token << "]";
      });
    }
    catch(boost::system::system_error &err) {
      BOOST_LOG(fatal) << "Couldn't bind http server to ports ["sv << port_https << "]: "sv << err.what();

      shutdown_event->raise(true);
      return;
    }
  });

  // Wait for any event
  shutdown_event->view();

  server->stop();

  server_thread.join();
}
} // namespace confighttp
