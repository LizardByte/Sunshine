//
// Created by loki on 6/3/19.
//

#include <iostream>
#include <filesystem>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <boost/asio/ssl/context.hpp>

#include <Simple-Web-Server/server_http.hpp>
#include <Simple-Web-Server/server_https.hpp>

#include "uuid.h"
#include "config.h"
#include "utility.h"
#include "stream.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "process.h"


namespace nvhttp {
using namespace std::literals;
constexpr auto PORT_HTTP  = 47989;
constexpr auto PORT_HTTPS = 47984;

constexpr auto VERSION     = "7.1.400.0";
constexpr auto GFE_VERSION = "2.0.0.1";

namespace fs = std::filesystem;
namespace pt = boost::property_tree;

std::string read_file(const char *path);

std::string local_ip;
using https_server_t = SimpleWeb::Server<SimpleWeb::HTTPS>;
using http_server_t  = SimpleWeb::Server<SimpleWeb::HTTP>;

struct conf_intern_t {
  std::string servercert;
  std::string pkey;
} conf_intern;

struct client_t {
  std::string uniqueID;
  std::string cert;
};

struct pair_session_t {
  client_t client;

  std::unique_ptr<crypto::aes_t> cipher_key;
  std::vector<uint8_t> clienthash;

  std::string serversecret;
  std::string serverchallenge;

  struct {
    util::Either<
      std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response>,
      std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>
    > response;
    std::string salt;
  } async_insert_pin;
};

// uniqueID, session
std::unordered_map<std::string, pair_session_t> map_id_sess;
std::unordered_map<std::string, client_t> map_id_client;

using args_t = SimpleWeb::CaseInsensitiveMultimap;

enum class op_e {
  ADD,
  REMOVE
};

void save_devices() {
  pt::ptree root;

  auto &nodes = root.add_child("root.devices", pt::ptree {});
  for(auto &[_,client] : map_id_client) {
    pt::ptree node;

    node.put("uniqueid"s, client.uniqueID);
    node.put("cert"s, client.cert);

    nodes.push_back(std::make_pair("", node));
  }

  pt::write_json(config::nvhttp.file_devices, root);
}

void load_devices() {
  auto file_devices = fs::current_path() / config::nvhttp.file_devices;

  if(!fs::exists(file_devices)) {
    return;
  }

  pt::ptree root;
  try {
    pt::read_json(config::nvhttp.file_devices, root);
  } catch (std::exception &e) {
    std::cout << e.what() << std::endl;

    return;
  }

  auto nodes = root.get_child("root.devices");

  for(auto &[_,node] : nodes) {
    auto uniqID = node.get<std::string>("uniqueid");
    auto &client = map_id_client.emplace(uniqID, client_t {}).first->second;

    client.uniqueID = uniqID;
    client.cert = node.get<std::string>("cert");
  }
}

void update_id_client(client_t &client, op_e op) {
  switch(op) {
    case op_e::ADD:
    {
      auto uniqID = client.uniqueID;
      map_id_client.emplace(std::move(uniqID), std::move(client));
    }
      break;
    case op_e::REMOVE:
      map_id_client.erase(client.uniqueID);
      break;
  }

  save_devices();
}

void getservercert(pair_session_t &sess, pt::ptree &tree, const std::string &pin) {
  auto salt = util::from_hex<std::array<uint8_t, 16>>(sess.async_insert_pin.salt, true);

  auto key = crypto::gen_aes_key(*salt, pin);
  sess.cipher_key = std::make_unique<crypto::aes_t>(key);

  tree.put("root.paired", 1);
  tree.put("root.plaincert", util::hex_vec(conf_intern.servercert, true));
  tree.put("root.<xmlattr>.status_code", 200);
}
void serverchallengeresp(pair_session_t &sess, pt::ptree &tree, const args_t &args) {
  auto encrypted_response = util::from_hex_vec(args.at("serverchallengeresp"s), true);

  std::vector<uint8_t> decrypted;
  crypto::cipher_t cipher(*sess.cipher_key);
  cipher.padding = false;

  cipher.decrypt(encrypted_response, decrypted);

  sess.clienthash = std::move(decrypted);

  auto serversecret = sess.serversecret;
  auto sign = crypto::sign256(crypto::pkey(conf_intern.pkey), serversecret);

  serversecret.insert(std::end(serversecret), std::begin(sign), std::end(sign));

  tree.put("root.pairingsecret", util::hex_vec(serversecret, true));
  tree.put("root.paired", 1);
  tree.put("root.<xmlattr>.status_code", 200);
}

void clientchallenge(pair_session_t &sess, pt::ptree &tree, const args_t &args) {
  auto challenge = util::from_hex_vec(args.at("clientchallenge"s), true);

  crypto::cipher_t cipher(*sess.cipher_key);
  cipher.padding = false;

  std::vector<uint8_t> decrypted;
  cipher.decrypt(challenge, decrypted);

  auto x509 = crypto::x509(conf_intern.servercert);
  auto sign = crypto::signature(x509);
  auto serversecret = crypto::rand(16);

  decrypted.insert(std::end(decrypted), std::begin(sign), std::end(sign));
  decrypted.insert(std::end(decrypted), std::begin(serversecret), std::end(serversecret));

  auto hash = crypto::hash({ (char*)decrypted.data(), decrypted.size() });
  auto serverchallenge = crypto::rand(16);

  std::string plaintext;
  plaintext.reserve(hash.size() + serverchallenge.size());

  plaintext.insert(std::end(plaintext), std::begin(hash), std::end(hash));
  plaintext.insert(std::end(plaintext), std::begin(serverchallenge), std::end(serverchallenge));

  std::vector<uint8_t> encrypted;
  cipher.encrypt(plaintext, encrypted);

  sess.serversecret    = std::move(serversecret);
  sess.serverchallenge = std::move(serverchallenge);

  tree.put("root.paired", 1);
  tree.put("root.challengeresponse", util::hex_vec(encrypted, true));
  tree.put("root.<xmlattr>.status_code", 200);
}

void clientpairingsecret(pair_session_t &sess, pt::ptree &tree, const args_t &args) {
  auto &client = sess.client;

  auto pairingsecret = util::from_hex_vec(args.at("clientpairingsecret"), true);

  std::string_view secret { pairingsecret.data(), 16 };
  std::string_view sign { pairingsecret.data() + secret.size(), crypto::digest_size };

  assert((secret.size() + sign.size()) == pairingsecret.size());

  auto x509 = crypto::x509(sess.client.cert);
  auto x509_sign = crypto::signature(x509);

  std::string data;
  data.reserve(sess.serverchallenge.size() + x509_sign.size() + secret.size());

  data.insert(std::end(data), std::begin(sess.serverchallenge), std::end(sess.serverchallenge));
  data.insert(std::end(data), std::begin(x509_sign), std::end(x509_sign));
  data.insert(std::end(data), std::begin(secret), std::end(secret));

  auto hash = crypto::hash(data);

  // if hash not correct, probably MITM
  if(std::memcmp(hash.data(), sess.clienthash.data(), hash.size())) {
    //TODO: log

    map_id_sess.erase(client.uniqueID);
    tree.put("root.paired", 0);
  }

  if(crypto::verify256(crypto::x509(client.cert), secret, sign)) {
    tree.put("root.paired", 1);

    auto it = map_id_sess.find(client.uniqueID);

    auto uniqID = client.uniqueID;
    update_id_client(client, op_e::ADD);
    map_id_sess.erase(it);
  }
  else {
    map_id_sess.erase(client.uniqueID);
    tree.put("root.paired", 0);
  }

  tree.put("root.<xmlattr>.status_code", 200);
}

template<class T>
struct tunnel;

template<>
struct tunnel<SimpleWeb::HTTPS> {
  static auto constexpr to_string = "HTTPS"sv;
};

template<>
struct tunnel<SimpleWeb::HTTP> {
  static auto constexpr to_string = "NONE"sv;
};

template<class T>
void print_req(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  std::cout << "TUNNEL :: "sv << tunnel<T>::to_string << std::endl;

  std::cout << "METHOD :: "sv << request->method << std::endl;
  std::cout << "DESTINATION :: "sv << request->path << std::endl;

  for(auto &[name, val] : request->header) {
    std::cout << name << " -- " << val << std::endl;
  }

  std::cout << std::endl;

  for(auto &[name, val] : request->parse_query_string()) {
    std::cout << name << " -- " << val << std::endl;
  }

  std::cout << std::endl;
}

template<class T>
void not_found(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  print_req<T>(request);

  pt::ptree tree;
  tree.put("root.<xmlattr>.status_code", 404);

  std::ostringstream data;

  pt::write_xml(data, tree);
  response->write(data.str());

  *response << "HTTP/1.1 404 NOT FOUND\r\n" << data.str();
}

template<class T>
void pair(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  print_req<T>(request);

  auto args = request->parse_query_string();
  auto uniqID { std::move(args.at("uniqueid"s)) };
  auto sess_it = map_id_sess.find(uniqID);

  pt::ptree tree;

  args_t::const_iterator it;
  if(it = args.find("phrase"); it != std::end(args)) {
    if(it->second == "getservercert"sv) {
      pair_session_t sess;
      
      sess.client.uniqueID = std::move(uniqID);
      sess.client.cert = util::from_hex_vec(args.at("clientcert"s), true);

      std::cout << sess.client.cert;
      auto ptr = map_id_sess.emplace(sess.client.uniqueID, std::move(sess)).first;

      ptr->second.async_insert_pin.salt = std::move(args.at("salt"s));
      ptr->second.async_insert_pin.response = std::move(response);
      return;
    }
    else if(it->second == "pairchallenge"sv) {
      tree.put("root.paired", 1);
      tree.put("root.<xmlattr>.status_code", 200);
    }
  }
  else if(it = args.find("clientchallenge"); it != std::end(args)) {
    clientchallenge(sess_it->second, tree, args);
  }
  else if(it = args.find("serverchallengeresp"); it != std::end(args)) {
    serverchallengeresp(sess_it->second, tree, args);
  }
  else if(it = args.find("clientpairingsecret"); it != std::end(args)) {
    clientpairingsecret(sess_it->second, tree, args);
  }
  else {
    tree.put("root.<xmlattr>.status_code", 404);
  }

  std::ostringstream data;

  pt::write_xml(data, tree);
  response->write(data.str());
}

template<class T>
void pin(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  print_req<T>(request);

  pt::ptree tree;

  auto &sess = std::begin(map_id_sess)->second;
  getservercert(sess, tree, request->path_match[1]);

  // response to the request for pin
  std::ostringstream data;
  pt::write_xml(data, tree);

  auto &async_response = sess.async_insert_pin.response;
  if(async_response.left()) {
    async_response.left()->write(data.str());
  }
  else {
    async_response.right()->write(data.str());
  }

  async_response = std::decay_t<decltype(async_response.left())>();
  // response to the current request
  response->write(""s);
}

template<class T>
void serverinfo(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  print_req<T>(request);

  auto args = request->parse_query_string();
  auto clientID = args.find("uniqueid"s);

  int pair_status = 0;

  if(clientID != std::end(args)) {
    if (auto it = map_id_client.find(clientID->second); it != std::end(map_id_client)) {
      pair_status = 1;
    }
  }

  pt::ptree tree;

  tree.put("root.<xmlattr>.status_code", 200);
  tree.put("root.hostname", "loki-pc");

  tree.put("root.appversion", VERSION);
  tree.put("root.GfeVersion", GFE_VERSION);
  tree.put("root.uniqueid", config::nvhttp.unique_id);
  tree.put("root.mac", "42:45:F0:65:D6:F4");
  tree.put("root.LocalIP", local_ip);

  if(config::nvhttp.external_ip.empty()) {
    tree.put("root.ExternalIP", local_ip);
  }
  else {
    tree.put("root.ExternalIP", config::nvhttp.external_ip);
  }
  
  tree.put("root.PairStatus", pair_status);
  tree.put("root.currentgame", 0);
  tree.put("root.state", "_SERVER_BUSY"); 

  std::ostringstream data;

  pt::write_xml(data, tree);
  response->write(data.str());
}

template<class T>
void applist(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  print_req<T>(request);

  auto args = request->parse_query_string();
  auto clientID = args.at("uniqueid"s);

  pt::ptree tree;

  auto g = util::fail_guard([&]() {
    std::ostringstream data;

    pt::write_xml(data, tree);
    response->write(data.str());
  });

  auto client = map_id_client.find(clientID);
  if(client == std::end(map_id_client)) {
    tree.put("root.<xmlattr>.status_code", 501);

    return;
  }

  auto &apps = tree.add_child("root", pt::ptree {});

  pt::ptree desktop;

  apps.put("<xmlattr>.status_code", 200);
  desktop.put("IsHdrSupported"s, 0);
  desktop.put("AppTitle"s, "Desktop");
  desktop.put("ID"s, 1);

  int x = 2;
  for(auto &[name, proc] : proc::proc.get_apps()) {
    pt::ptree app;

    app.put("IsHdrSupported"s, 0);
    app.put("AppTitle"s, name);
    app.put("ID"s, x++);

    apps.push_back(std::make_pair("App", std::move(app)));
  }

  apps.push_back(std::make_pair("App", desktop));
}

template<class T>
void launch(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  print_req<T>(request);

  auto args = request->parse_query_string();

  auto appid = util::from_view(args.at("appid")) -2;

  stream::launch_session_t launch_session;
  if(appid >= 0) {
    auto pos = std::begin(proc::proc.get_apps());
    std::advance(pos, appid);

    launch_session.app_name = pos->first;
  }

  auto clientID = args.at("uniqueid"s);
  launch_session.gcm_key = *util::from_hex<crypto::aes_t>(args.at("rikey"s), true);
  uint32_t prepend_iv = util::endian::big<uint32_t>(util::from_view(args.at("rikeyid"s)));
  auto prepend_iv_p = (uint8_t*)&prepend_iv;

  auto next = std::copy(prepend_iv_p, prepend_iv_p + sizeof(prepend_iv), std::begin(launch_session.iv));
  std::fill(next, std::end(launch_session.iv), 0);

  pt::ptree tree;

  auto g = util::fail_guard([&]() {
    std::ostringstream data;

    pt::write_xml(data, tree);
    response->write(data.str());
  });

  stream::launch_event.raise(std::move(launch_session));

/*
  bool sops = args.at("sops"s) == "1";
  std::optional<int> gcmap { std::nullopt };
  if(auto it = args.find("gcmap"s); it != std::end(args)) {
    gcmap = std::stoi(it->second);
  }
*/

  tree.put("root.<xmlattr>.status_code", 200);
  tree.put("root.gamesession", 1);
}

template<class T>
void appasset(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  std::ifstream in(SUNSHINE_ASSETS_DIR "/box.png");
  response->write(SimpleWeb::StatusCode::success_ok, in);
}

void start() {
  local_ip = platf::get_local_ip();
  if(local_ip.empty()) {
    std::cout << "Error: Could not determine the local ip-address"sv << std::endl;

    exit(8);
  }

  load_devices();

  conf_intern.pkey = read_file(config::nvhttp.pkey.c_str());
  conf_intern.servercert = read_file(config::nvhttp.cert.c_str());

  https_server_t https_server { config::nvhttp.cert, config::nvhttp.pkey };
  http_server_t http_server;

  https_server.default_resource = not_found<SimpleWeb::HTTPS>;
  https_server.resource["^/serverinfo$"]["GET"] = serverinfo<SimpleWeb::HTTPS>;
  https_server.resource["^/pair$"]["GET"] = pair<SimpleWeb::HTTPS>;
  https_server.resource["^/applist$"]["GET"] = applist<SimpleWeb::HTTPS>;
  https_server.resource["^/appasset$"]["GET"] = appasset<SimpleWeb::HTTPS>;
  https_server.resource["^/launch$"]["GET"] = launch<SimpleWeb::HTTPS>;
  https_server.resource["^/pin/([0-9]+)$"]["GET"] = pin<SimpleWeb::HTTPS>;

  https_server.config.reuse_address = true;
  https_server.config.address = "0.0.0.0"s;
  https_server.config.port = PORT_HTTPS;

  http_server.default_resource = not_found<SimpleWeb::HTTP>;
  http_server.resource["^/serverinfo$"]["GET"] = serverinfo<SimpleWeb::HTTP>;
  http_server.resource["^/pair$"]["GET"] = pair<SimpleWeb::HTTP>;
  http_server.resource["^/applist$"]["GET"] = applist<SimpleWeb::HTTP>;
  http_server.resource["^/appasset$"]["GET"] = appasset<SimpleWeb::HTTP>;
  http_server.resource["^/launch$"]["GET"] = launch<SimpleWeb::HTTP>;
  http_server.resource["^/pin/([0-9]+)$"]["GET"] = pin<SimpleWeb::HTTP>;

  http_server.config.reuse_address = true;
  http_server.config.address = "0.0.0.0"s;
  http_server.config.port = PORT_HTTP;

  std::thread ssl { &https_server_t::start, &https_server };
  std::thread tcp { &http_server_t::start, &http_server };

  ssl.join();
  tcp.join();
}

std::string read_file(const char *path) {
  std::ifstream in(path);

  std::string input;
  std::string base64_cert;

  while(!in.eof()) {
    std::getline(in, input);
    base64_cert += input + '\n';
  }

  return base64_cert;
}
}
