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
#include <Simple-Web-Server/server_https.hpp>
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

namespace confighttp {
using namespace std::literals;
constexpr auto PORT_HTTPS = 47990;

namespace fs = std::filesystem;
namespace pt = boost::property_tree;

using https_server_t = SimpleWeb::Server<SimpleWeb::HTTPS>;

using args_t       = SimpleWeb::CaseInsensitiveMultimap;
using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
using req_https_t  = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;

enum class op_e {
  ADD,
  REMOVE
};

void print_req(const req_https_t &request) {
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

void send_unauthorized(resp_https_t response, req_https_t request) {
  auto address = request->remote_endpoint_address();
  BOOST_LOG(info) << '[' << address << "] -- denied"sv;
  const SimpleWeb::CaseInsensitiveMultimap headers {
    { "WWW-Authenticate", R"(Basic realm="Sunshine Gamestream Host", charset="UTF-8")" }
  };
  response->write(SimpleWeb::StatusCode::client_error_unauthorized, headers);
}

bool authenticate(resp_https_t response, req_https_t request) {
  auto address = request->remote_endpoint_address();
  auto ip_type = net::from_address(address);

  if(ip_type > http::origin_pin_allowed) {
    BOOST_LOG(info) << '[' << address << "] -- denied"sv;
    response->write(SimpleWeb::StatusCode::client_error_forbidden);
    return false;
  }

  auto fg = util::fail_guard([&]() {
    send_unauthorized(response, request);
  });

  auto auth = request->header.find("authorization");
  if(auth == request->header.end()) {
    return false;
  }

  auto &rawAuth = auth->second;
  auto authData = SimpleWeb::Crypto::Base64::decode(rawAuth.substr("Basic "sv.length()));

  int index = authData.find(':');
  if(index >= authData.size() - 1) {
    return false;
  }

  auto username = authData.substr(0, index);
  auto password = authData.substr(index + 1);
  auto hash     = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();

  if(username != config::sunshine.username || hash != config::sunshine.password) {
    return false;
  }

  fg.disable();
  return true;
}

void not_found(resp_https_t response, req_https_t request) {
  pt::ptree tree;
  tree.put("root.<xmlattr>.status_code", 404);

  std::ostringstream data;

  pt::write_xml(data, tree);
  response->write(data.str());

  *response << "HTTP/1.1 404 NOT FOUND\r\n"
            << data.str();
}

void getIndexPage(resp_https_t response, req_https_t request) {
  if(!authenticate(response, request)) return;

  print_req(request);

  std::string header  = read_file(WEB_DIR "header.html");
  std::string content = read_file(WEB_DIR "index.html");
  response->write(header + content);
}

void getPinPage(resp_https_t response, req_https_t request) {
  if(!authenticate(response, request)) return;

  print_req(request);

  std::string header  = read_file(WEB_DIR "header.html");
  std::string content = read_file(WEB_DIR "pin.html");
  response->write(header + content);
}

void getAppsPage(resp_https_t response, req_https_t request) {
  if(!authenticate(response, request)) return;

  print_req(request);

  std::string header  = read_file(WEB_DIR "header.html");
  std::string content = read_file(WEB_DIR "apps.html");
  response->write(header + content);
}

void getClientsPage(resp_https_t response, req_https_t request) {
  if(!authenticate(response, request)) return;

  print_req(request);

  std::string header  = read_file(WEB_DIR "header.html");
  std::string content = read_file(WEB_DIR "clients.html");
  response->write(header + content);
}

void getConfigPage(resp_https_t response, req_https_t request) {
  if(!authenticate(response, request)) return;

  print_req(request);

  std::string header  = read_file(WEB_DIR "header.html");
  std::string content = read_file(WEB_DIR "config.html");
  response->write(header + content);
}

void getPasswordPage(resp_https_t response, req_https_t request) {
  if(!authenticate(response, request)) return;

  print_req(request);

  std::string header  = read_file(WEB_DIR "header.html");
  std::string content = read_file(WEB_DIR "password.html");
  response->write(header + content);
}

void getApps(resp_https_t response, req_https_t request) {
  if(!authenticate(response, request)) return;

  print_req(request);

  std::string content = read_file(config::stream.file_apps.c_str());
  response->write(content);
}

void saveApp(resp_https_t response, req_https_t request) {
  if(!authenticate(response, request)) return;

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

  BOOST_LOG(fatal) << config::stream.file_apps;
  try {
    //TODO: Input Validation
    pt::read_json(ss, inputTree);
    pt::read_json(config::stream.file_apps, fileTree);

    if(inputTree.get_child("prep-cmd").empty()) {
      inputTree.erase("prep-cmd");
    }

    if(inputTree.get_child("detached").empty()) {
      inputTree.erase("detached");
    }

    auto &apps_node = fileTree.get_child("apps"s);
    int index       = inputTree.get<int>("index");

    inputTree.erase("index");

    if(index == -1) {
      apps_node.push_back(std::make_pair("", inputTree));
    }
    else {
      //Unfortuantely Boost PT does not allow to directly edit the array, copy should do the trick
      pt::ptree newApps;
      int i = 0;
      for(const auto &kv : apps_node) {
        if(i == index) {
          newApps.push_back(std::make_pair("", inputTree));
        }
        else {
          newApps.push_back(std::make_pair("", kv.second));
        }
        i++;
      }
      fileTree.erase("apps");
      fileTree.push_back(std::make_pair("apps", newApps));
    }
    pt::write_json(config::stream.file_apps, fileTree);
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "SaveApp: "sv << e.what();

    outputTree.put("status", "false");
    outputTree.put("error", "Invalid Input JSON");
    return;
  }

  outputTree.put("status", "true");
  proc::refresh(config::stream.file_apps);
}

void deleteApp(resp_https_t response, req_https_t request) {
  if(!authenticate(response, request)) return;

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
    int index       = stoi(request->path_match[1]);

    if(index < 0) {
      outputTree.put("status", "false");
      outputTree.put("error", "Invalid Index");
      return;
    }
    else {
      //Unfortuantely Boost PT does not allow to directly edit the array, copy should do the trick
      pt::ptree newApps;
      int i = 0;
      for(const auto &kv : apps_node) {
        if(i++ != index) {
          newApps.push_back(std::make_pair("", kv.second));
        }
      }
      fileTree.erase("apps");
      fileTree.push_back(std::make_pair("apps", newApps));
    }
    pt::write_json(config::stream.file_apps, fileTree);
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "DeleteApp: "sv << e.what();
    outputTree.put("status", "false");
    outputTree.put("error", "Invalid File JSON");
    return;
  }

  outputTree.put("status", "true");
  proc::refresh(config::stream.file_apps);
}

void getConfig(resp_https_t response, req_https_t request) {
  if(!authenticate(response, request)) return;

  print_req(request);

  pt::ptree outputTree;
  auto g = util::fail_guard([&]() {
    std::ostringstream data;

    pt::write_json(data, outputTree);
    response->write(data.str());
  });

  outputTree.put("status", "true");
  outputTree.put("platform", SUNSHINE_PLATFORM);

  auto vars = config::parse_config(read_file(config::sunshine.config_file.c_str()));

  for(auto &[name, value] : vars) {
    outputTree.put(std::move(name), std::move(value));
  }
}

void saveConfig(resp_https_t response, req_https_t request) {
  if(!authenticate(response, request)) return;

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
    //TODO: Input Validation
    pt::read_json(ss, inputTree);
    for(const auto &kv : inputTree) {
      std::string value = inputTree.get<std::string>(kv.first);
      if(value.length() == 0 || value.compare("null") == 0) continue;

      configStream << kv.first << " = " << value << std::endl;
    }
    write_file(config::sunshine.config_file.c_str(), configStream.str());
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "SaveConfig: "sv << e.what();
    outputTree.put("status", "false");
    outputTree.put("error", e.what());
    return;
  }
}

void savePassword(resp_https_t response, req_https_t request) {
  if(!authenticate(response, request)) return;

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
    //TODO: Input Validation
    pt::read_json(ss, inputTree);
    auto username        = inputTree.get<std::string>("currentUsername");
    auto newUsername     = inputTree.get<std::string>("newUsername");
    auto password        = inputTree.get<std::string>("currentPassword");
    auto newPassword     = inputTree.get<std::string>("newPassword");
    auto confirmPassword = inputTree.get<std::string>("confirmNewPassword");
    if(newUsername.length() == 0) newUsername = username;

    auto hash = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();
    if(username == config::sunshine.username && hash == config::sunshine.password) {
      if(newPassword != confirmPassword) {
        outputTree.put("status", false);
        outputTree.put("error", "Password Mismatch");
      }

      http::save_user_creds(config::sunshine.credentials_file, newUsername, newPassword);
      http::reload_user_creds(config::sunshine.credentials_file);
      outputTree.put("status", true);
    }
    else {
      outputTree.put("status", false);
      outputTree.put("error", "Invalid Current Credentials");
    }
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "SavePassword: "sv << e.what();
    outputTree.put("status", false);
    outputTree.put("error", e.what());
    return;
  }
}

void savePin(resp_https_t response, req_https_t request) {
  if(!authenticate(response, request)) return;

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
    //TODO: Input Validation
    pt::read_json(ss, inputTree);
    std::string pin = inputTree.get<std::string>("pin");
    outputTree.put("status", nvhttp::pin(pin));
  }
  catch(std::exception &e) {
    BOOST_LOG(warning) << "SavePin: "sv << e.what();
    outputTree.put("status", false);
    outputTree.put("error", e.what());
    return;
  }
}

void start() {
  auto shutdown_event = mail::man->event<bool>(mail::shutdown);

  auto ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tls);
  ctx->use_certificate_chain_file(config::nvhttp.cert);
  ctx->use_private_key_file(config::nvhttp.pkey, boost::asio::ssl::context::pem);
  https_server_t server { ctx, 0 };
  server.default_resource                           = not_found;
  server.resource["^/$"]["GET"]                     = getIndexPage;
  server.resource["^/pin$"]["GET"]                  = getPinPage;
  server.resource["^/apps$"]["GET"]                 = getAppsPage;
  server.resource["^/clients$"]["GET"]              = getClientsPage;
  server.resource["^/config$"]["GET"]               = getConfigPage;
  server.resource["^/password$"]["GET"]             = getPasswordPage;
  server.resource["^/api/pin"]["POST"]              = savePin;
  server.resource["^/api/apps$"]["GET"]             = getApps;
  server.resource["^/api/apps$"]["POST"]            = saveApp;
  server.resource["^/api/config$"]["GET"]           = getConfig;
  server.resource["^/api/config$"]["POST"]          = saveConfig;
  server.resource["^/api/password$"]["POST"]        = savePassword;
  server.resource["^/api/apps/([0-9]+)$"]["DELETE"] = deleteApp;
  server.config.reuse_address                       = true;
  server.config.address                             = "0.0.0.0"s;
  server.config.port                                = PORT_HTTPS;

  try {
    server.bind();
    BOOST_LOG(info) << "Configuration UI available at [https://localhost:"sv << PORT_HTTPS << "]";
  }
  catch(boost::system::system_error &err) {
    BOOST_LOG(fatal) << "Couldn't bind http server to ports ["sv << PORT_HTTPS << "]: "sv << err.what();

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

      BOOST_LOG(fatal) << "Couldn't start Configuration HTTP server to ports ["sv << PORT_HTTPS << ", "sv << PORT_HTTPS << "]: "sv << err.what();
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