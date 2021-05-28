//
// Created by TheElixZammuto on 2021-05-09.
// TODO: Authentication, better handling of routes common to nvhttp, cleanup

#include "process.h"

#include <filesystem>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <boost/asio/ssl/context.hpp>

#include <Simple-Web-Server/server_http.hpp>
#include <Simple-Web-Server/crypto.hpp>
#include <boost/asio/ssl/context_base.hpp>

#include "config.h"
#include "utility.h"
#include "rtsp.h"
#include "crypto.h"
#include "confighttp.h"
#include "platform/common.h"
#include "httpcommon.h"
#include "network.h"
#include "nvhttp.h"
#include "uuid.h"
#include "main.h"

std::string read_file(std::string path);

namespace confighttp
{
using namespace std::literals;
constexpr auto PORT_HTTP = 47990;

namespace fs = std::filesystem;
namespace pt = boost::property_tree;

using https_server_t = SimpleWeb::Server<SimpleWeb::HTTPS>;

using args_t = SimpleWeb::CaseInsensitiveMultimap;
using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;

enum class op_e
{
  ADD,
  REMOVE
};

void send_unauthorized(resp_https_t response, req_https_t request)
{
  auto address = request->remote_endpoint_address();
  BOOST_LOG(info) << '[' << address << "] -- denied"sv;
  const SimpleWeb::CaseInsensitiveMultimap headers {
    {"WWW-Authenticate",R"(Basic realm="Sunshine Gamestream Host", charset="UTF-8")"}
  };
  response->write(SimpleWeb::StatusCode::client_error_unauthorized,headers);
}

bool authenticate(resp_https_t response, req_https_t request)
{
  auto address = request->remote_endpoint_address();
  auto ip_type = net::from_address(address);
  if(ip_type > http::origin_pin_allowed) {
    BOOST_LOG(info) << '[' << address << "] -- denied"sv;
    response->write(SimpleWeb::StatusCode::client_error_forbidden);
    return false;
  }
  auto auth = request->header.find("authorization");
  if(auth == request->header.end() ){
    send_unauthorized(response,request);
    return false;
  }
  std::string rawAuth = auth->second;
  std::string authData = rawAuth.substr("Basic "sv.length());
  authData = SimpleWeb::Crypto::Base64::decode(authData);
  int index = authData.find(':');
  std::string username = authData.substr(0,index);
  std::string password = authData.substr(index + 1);
  std::string hash = crypto::hash_hexstr(password + config::sunshine.salt);
  if(username == config::sunshine.username && hash == config::sunshine.password) return true;
  send_unauthorized(response,request);
  return false;
}

template <class T>
void not_found(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request)
{
  pt::ptree tree;
  tree.put("root.<xmlattr>.status_code", 404);

  std::ostringstream data;

  pt::write_xml(data, tree);
  response->write(data.str());

  *response << "HTTP/1.1 404 NOT FOUND\r\n"
            << data.str();
}

void getIndexPage(resp_https_t response, req_https_t request)
{
  if(!authenticate(response,request))return;
  std::string header = read_file(WEB_DIR "header.html");
  std::string content = read_file(WEB_DIR "index.html");
  response->write(header + content);
}

template <class T>
void getPinPage(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request)
{
  if(!authenticate(response,request))return;
  std::string header = read_file(WEB_DIR "header.html");
  std::string content = read_file(WEB_DIR "pin.html");
  response->write(header + content);
}

template <class T>
void getAppsPage(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request)
{
  if(!authenticate(response,request))return;
  std::string header = read_file(WEB_DIR "header.html");
  std::string content = read_file(WEB_DIR "apps.html");
  response->write(header + content);
}

template <class T>
void getClientsPage(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request)
{
  if(!authenticate(response,request))return;
  std::string header = read_file(WEB_DIR "header.html");
  std::string content = read_file(WEB_DIR "clients.html");
  response->write(header + content);
}

template <class T>
void getConfigPage(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request)
{
  if(!authenticate(response,request))return;
  std::string header = read_file(WEB_DIR "header.html");
  std::string content = read_file(WEB_DIR "config.html");
  response->write(header + content);
}

void getApps(resp_https_t response, req_https_t request)
{
  if(!authenticate(response,request))return;
  std::string content = read_file(SUNSHINE_ASSETS_DIR "/" APPS_JSON);
  response->write(content);
}

void saveApp(resp_https_t response, req_https_t request)
{
  if(!authenticate(response,request))return;
  std::stringstream ss;
  ss << request->content.rdbuf();
  pt::ptree outputTree;
  auto g = util::fail_guard([&]() {
    std::ostringstream data;

    pt::write_json(data, outputTree);
    response->write(data.str());
  });
  pt::ptree inputTree, fileTree;
  try
  {
    //TODO: Input Validation
    pt::read_json(ss, inputTree);
    pt::read_json(SUNSHINE_ASSETS_DIR "/" APPS_JSON, fileTree);
    auto &apps_node = fileTree.get_child("apps"s);
    int index = inputTree.get<int>("index");
    BOOST_LOG(info) << inputTree.get_child("prep-cmd").empty();
    if (inputTree.get_child("prep-cmd").empty())
      inputTree.erase("prep-cmd");
    inputTree.erase("index");
    if (index == -1)
    {
      apps_node.push_back(std::make_pair("", inputTree));
    }
    else
    {
      //Unfortuantely Boost PT does not allow to directly edit the array, copt should do the trick
      pt::ptree newApps;
      int i = 0;
      for (const auto &kv : apps_node)
      {
        if (i == index)
        {
          newApps.push_back(std::make_pair("", inputTree));
        }
        else
        {
          newApps.push_back(std::make_pair("", kv.second));
        }
        i++;
      }
      fileTree.erase("apps");
      fileTree.push_back(std::make_pair("apps", newApps));
    }
    pt::write_json(SUNSHINE_ASSETS_DIR "/" APPS_JSON, fileTree);
    outputTree.put("status", "true");
    proc::refresh(SUNSHINE_ASSETS_DIR "/" APPS_JSON);
  }
  catch (std::exception &e)
  {
    BOOST_LOG(warning) << e.what();
    outputTree.put("status", "false");
    outputTree.put("error", "Invalid Input JSON");
    return;
  }
}

void deleteApp(resp_https_t response, req_https_t request)
{
  if(!authenticate(response,request))return;
  pt::ptree outputTree;
  auto g = util::fail_guard([&]() {
    std::ostringstream data;

    pt::write_json(data, outputTree);
    response->write(data.str());
  });
  pt::ptree fileTree;
  try
  {
    pt::read_json(config::stream.file_apps, fileTree);
    auto &apps_node = fileTree.get_child("apps"s);
    int index = stoi(request->path_match[1]);
    BOOST_LOG(info) << index;
    if (index <= 0)
    {
      outputTree.put("status", "false");
      outputTree.put("error", "Invalid Index");
      return;
    }
    else
    {
      //Unfortuantely Boost PT does not allow to directly edit the array, copy should do the trick
      pt::ptree newApps;
      int i = 0;
      for (const auto &kv : apps_node)
      {
        if (i != index)
        {
          newApps.push_back(std::make_pair("", kv.second));
        }
        i++;
      }
      fileTree.erase("apps");
      fileTree.push_back(std::make_pair("apps", newApps));
    }
    pt::write_json(SUNSHINE_ASSETS_DIR "/" APPS_JSON, fileTree);
    outputTree.put("status", "true");
    proc::refresh(SUNSHINE_ASSETS_DIR "/" APPS_JSON);
  }
  catch (std::exception &e)
  {
    BOOST_LOG(warning) << e.what();
    outputTree.put("status", "false");
    outputTree.put("error", "Invalid File JSON");
    return;
  }
}

void getConfig(resp_https_t response, req_https_t request)
{
  if(!authenticate(response,request))return;
  pt::ptree outputTree;
  auto g = util::fail_guard([&]() {
    std::ostringstream data;

    pt::write_json(data, outputTree);
    response->write(data.str());
  });
  try
  {
    outputTree.put("status","true");
    outputTree.put("platform",SUNSHINE_PLATFORM);
    const char *config_file = SUNSHINE_ASSETS_DIR "/sunshine.conf";
    std::ifstream in { config_file };

    if(!in.is_open()) {
      std::cout << "Error: Couldn't open "sv << config_file << std::endl;
    }

    auto vars = config::parse_config(std::string {
      // Quick and dirty
      std::istreambuf_iterator<char>(in),
      std::istreambuf_iterator<char>()
    });

    for(auto &[name,value] : vars) {
      outputTree.put(std::move(name), std::move(value));
    }
  }
  catch (std::exception &e)
  {
    BOOST_LOG(warning) << e.what();
    outputTree.put("status", "false");
    outputTree.put("error", "Invalid File JSON");
    return;
  }
}

void saveConfig(resp_https_t response, req_https_t request)
{
  if(!authenticate(response,request))return;
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
  try
  {
    //TODO: Input Validation
    pt::read_json(ss, inputTree);
    for (const auto &kv : inputTree)
    {
      std::string value = inputTree.get<std::string>(kv.first);
      if(value.length() == 0 || value.compare("null") == 0)continue;
      configStream << kv.first << " = " << value << std::endl;
    }
    http::write_file(SUNSHINE_ASSETS_DIR "/sunshine.conf",configStream.str());
  }
  catch (std::exception &e)
  {
    BOOST_LOG(warning) << e.what();
    outputTree.put("status", "false");
    outputTree.put("error", e.what());
    return;
  }
}

void start(std::shared_ptr<safe::signal_t> shutdown_event)
{
  auto ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tls);
  ctx->use_certificate_chain_file(config::nvhttp.cert);
  ctx->use_private_key_file(config::nvhttp.pkey, boost::asio::ssl::context::pem);
  https_server_t http_server{ctx, 0};
  http_server.default_resource = not_found<SimpleWeb::HTTPS>;
  http_server.resource["^/$"]["GET"] = getIndexPage;
  http_server.resource["^/pin$"]["GET"] = getPinPage<SimpleWeb::HTTPS>;
  http_server.resource["^/apps$"]["GET"] = getAppsPage<SimpleWeb::HTTPS>;
  http_server.resource["^/api/apps$"]["GET"] = getApps;
  http_server.resource["^/api/apps$"]["POST"] = saveApp;
  http_server.resource["^/api/config$"]["GET"] = getConfig;
  http_server.resource["^/api/config$"]["POST"] = saveConfig;
  http_server.resource["^/api/apps/([0-9]+)$"]["DELETE"] = deleteApp;
  http_server.resource["^/clients$"]["GET"] = getClientsPage<SimpleWeb::HTTPS>;
  http_server.resource["^/config$"]["GET"] = getConfigPage<SimpleWeb::HTTPS>;
  http_server.resource["^/pin/([0-9]+)$"]["GET"] = nvhttp::pin<SimpleWeb::HTTPS>;
  http_server.config.reuse_address = true;
  http_server.config.address = "0.0.0.0"s;
  http_server.config.port = PORT_HTTP;

  try
  {
    http_server.bind();
    BOOST_LOG(info) << "Configuration UI available at [https://localhost:"sv << PORT_HTTP << "]";
  }
  catch (boost::system::system_error &err)
  {
    BOOST_LOG(fatal) << "Couldn't bind http server to ports ["sv << PORT_HTTP << "]: "sv << err.what();

    shutdown_event->raise(true);
    return;
  }
  auto accept_and_run = [&](auto *http_server) {
    try
    {
      http_server->accept_and_run();
    }
    catch (boost::system::system_error &err)
    {
      // It's possible the exception gets thrown after calling http_server->stop() from a different thread
      if (shutdown_event->peek())
      {
        return;
      }

      BOOST_LOG(fatal) << "Couldn't start Configuration HTTP server to ports ["sv << PORT_HTTP << ", "sv << PORT_HTTP << "]: "sv << err.what();
      shutdown_event->raise(true);
      return;
    }
  };
  std::thread tcp{accept_and_run, &http_server};

  // Wait for any event
  shutdown_event->view();

  http_server.stop();

  tcp.join();
}
}

std::string read_file(std::string path)
{
  std::ifstream in(path);

  std::string input;
  std::string base64_cert;

  //FIXME:  Being unable to read file could result in infinite loop
  while (!in.eof())
  {
    std::getline(in, input);
    base64_cert += input + '\n';
  }

  return base64_cert;
}