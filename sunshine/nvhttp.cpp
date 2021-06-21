//
// Created by loki on 6/3/19.
//

#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include "process.h"

#include <filesystem>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <boost/asio/ssl/context.hpp>

#include <Simple-Web-Server/server_http.hpp>
#include <Simple-Web-Server/server_https.hpp>
#include <boost/asio/ssl/context_base.hpp>


#include "config.h"
#include "crypto.h"
#include "httpcommon.h"
#include "main.h"
#include "network.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "rtsp.h"
#include "utility.h"
#include "uuid.h"

namespace nvhttp {
using namespace std::literals;
constexpr auto PORT_HTTP  = 47989;
constexpr auto PORT_HTTPS = 47984;

constexpr auto VERSION     = "7.1.400.0";
constexpr auto GFE_VERSION = "3.12.0.1";

namespace fs = std::filesystem;
namespace pt = boost::property_tree;

using https_server_t = SimpleWeb::Server<SimpleWeb::HTTPS>;
using http_server_t  = SimpleWeb::Server<SimpleWeb::HTTP>;

struct conf_intern_t {
  std::string servercert;
  std::string pkey;
} conf_intern;

struct client_t {
  std::string uniqueID;
  std::vector<std::string> certs;
};

struct pair_session_t {
  struct {
    std::string uniqueID;
    std::string cert;
  } client;

  std::unique_ptr<crypto::aes_t> cipher_key;
  std::vector<uint8_t> clienthash;

  std::string serversecret;
  std::string serverchallenge;

  struct {
    util::Either<
      std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response>,
      std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>>
      response;
    std::string salt;
  } async_insert_pin;
};

// uniqueID, session
std::unordered_map<std::string, pair_session_t> map_id_sess;
std::unordered_map<std::string, client_t> map_id_client;

using args_t       = SimpleWeb::CaseInsensitiveMultimap;
using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Response>;
using req_https_t  = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTPS>::Request>;
using resp_http_t  = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response>;
using req_http_t   = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request>;

enum class op_e {
  ADD,
  REMOVE
};

void save_state() {
  pt::ptree root;

  if(fs::exists(config::nvhttp.file_state)) {
    try {
      pt::read_json(config::nvhttp.file_state, root);
    }
    catch(std::exception &e) {
      BOOST_LOG(error) << "Couldn't read "sv << config::nvhttp.file_state << ": "sv << e.what();
      return;
    }
  }

  root.erase("root"s);

  root.put("root.uniqueid", http::unique_id);
  auto &nodes = root.add_child("root.devices", pt::ptree {});
  for(auto &[_, client] : map_id_client) {
    pt::ptree node;

    node.put("uniqueid"s, client.uniqueID);

    pt::ptree cert_nodes;
    for(auto &cert : client.certs) {
      pt::ptree cert_node;
      cert_node.put_value(cert);
      cert_nodes.push_back(std::make_pair(""s, cert_node));
    }
    node.add_child("certs"s, cert_nodes);

    nodes.push_back(std::make_pair(""s, node));
  }

  try {
    pt::write_json(config::nvhttp.file_state, root);
  }
  catch(std::exception &e) {
    BOOST_LOG(error) << "Couldn't write "sv << config::nvhttp.file_state << ": "sv << e.what();
    return;
  }
}

void load_state() {
  if(!fs::exists(config::nvhttp.file_state)) {
    BOOST_LOG(info) << "DOENST EXIST"sv;
    http::unique_id = util::uuid_t::generate().string();
    return;
  }

  pt::ptree root;
  try {
    pt::read_json(config::nvhttp.file_state, root);
  }
  catch(std::exception &e) {
    BOOST_LOG(error) << "Couldn't read "sv << config::nvhttp.file_state << ": "sv << e.what();

    return;
  }

  auto unique_id_p = root.get_optional<std::string>("root.uniqueid");
  if(!unique_id_p) {
    // This file doesn't contain moonlight credentials
    http::unique_id = util::uuid_t::generate().string();
    return;
  }
  http::unique_id = std::move(*unique_id_p);

  auto device_nodes = root.get_child("root.devices");

  for(auto &[_, device_node] : device_nodes) {
    auto uniqID  = device_node.get<std::string>("uniqueid");
    auto &client = map_id_client.emplace(uniqID, client_t {}).first->second;

    client.uniqueID = uniqID;

    for(auto &[_, el] : device_node.get_child("certs")) {
      client.certs.emplace_back(el.get_value<std::string>());
    }
  }
}

void update_id_client(const std::string &uniqueID, std::string &&cert, op_e op) {
  switch(op) {
  case op_e::ADD: {
    auto &client = map_id_client[uniqueID];
    client.certs.emplace_back(std::move(cert));
    client.uniqueID = uniqueID;
  } break;
  case op_e::REMOVE:
    map_id_client.erase(uniqueID);
    break;
  }

  if(!config::sunshine.flags[config::flag::FRESH_STATE]) {
    save_state();
  }
}

stream::launch_session_t make_launch_session(bool host_audio, const args_t &args) {
  stream::launch_session_t launch_session;

  launch_session.host_audio = host_audio;
  launch_session.gcm_key    = *util::from_hex<crypto::aes_t>(args.at("rikey"s), true);
  uint32_t prepend_iv       = util::endian::big<uint32_t>(util::from_view(args.at("rikeyid"s)));
  auto prepend_iv_p         = (uint8_t *)&prepend_iv;

  auto next = std::copy(prepend_iv_p, prepend_iv_p + sizeof(prepend_iv), std::begin(launch_session.iv));
  std::fill(next, std::end(launch_session.iv), 0);

  return launch_session;
}

void getservercert(pair_session_t &sess, pt::ptree &tree, const std::string &pin) {
  if(sess.async_insert_pin.salt.size() < 32) {
    tree.put("root.paired", 0);
    tree.put("root.<xmlattr>.status_code", 400);
    return;
  }

  std::string_view salt_view { sess.async_insert_pin.salt.data(), 32 };

  auto salt = util::from_hex<std::array<uint8_t, 16>>(salt_view, true);

  auto key        = crypto::gen_aes_key(*salt, pin);
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
  auto sign         = crypto::sign256(crypto::pkey(conf_intern.pkey), serversecret);

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

  auto x509         = crypto::x509(conf_intern.servercert);
  auto sign         = crypto::signature(x509);
  auto serversecret = crypto::rand(16);

  decrypted.insert(std::end(decrypted), std::begin(sign), std::end(sign));
  decrypted.insert(std::end(decrypted), std::begin(serversecret), std::end(serversecret));

  auto hash            = crypto::hash({ (char *)decrypted.data(), decrypted.size() });
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

void clientpairingsecret(std::shared_ptr<safe::queue_t<crypto::x509_t>> &add_cert, pair_session_t &sess, pt::ptree &tree, const args_t &args) {
  auto &client = sess.client;

  auto pairingsecret = util::from_hex_vec(args.at("clientpairingsecret"), true);

  std::string_view secret { pairingsecret.data(), 16 };
  std::string_view sign { pairingsecret.data() + secret.size(), crypto::digest_size };

  assert((secret.size() + sign.size()) == pairingsecret.size());

  auto x509      = crypto::x509(client.cert);
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
    add_cert->raise(crypto::x509(client.cert));

    auto it = map_id_sess.find(client.uniqueID);

    update_id_client(client.uniqueID, std::move(client.cert), op_e::ADD);
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
  BOOST_LOG(debug) << "TUNNEL :: "sv << tunnel<T>::to_string;

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

template<class T>
void not_found(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  print_req<T>(request);

  pt::ptree tree;
  tree.put("root.<xmlattr>.status_code", 404);

  std::ostringstream data;

  pt::write_xml(data, tree);
  response->write(data.str());

  *response << "HTTP/1.1 404 NOT FOUND\r\n"
            << data.str();
}

template<class T>
void pair(std::shared_ptr<safe::queue_t<crypto::x509_t>> &add_cert, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  print_req<T>(request);

  pt::ptree tree;

  auto args = request->parse_query_string();
  if(args.find("uniqueid"s) == std::end(args)) {
    tree.put("root.<xmlattr>.status_code", 400);

    return;
  }

  auto uniqID { std::move(args.at("uniqueid"s)) };
  auto sess_it = map_id_sess.find(uniqID);

  args_t::const_iterator it;
  if(it = args.find("phrase"); it != std::end(args)) {
    if(it->second == "getservercert"sv) {
      pair_session_t sess;

      sess.client.uniqueID = std::move(uniqID);
      sess.client.cert     = util::from_hex_vec(args.at("clientcert"s), true);

      BOOST_LOG(debug) << sess.client.cert;
      auto ptr = map_id_sess.emplace(sess.client.uniqueID, std::move(sess)).first;

      ptr->second.async_insert_pin.salt = std::move(args.at("salt"s));

      if(config::sunshine.flags[config::flag::CONST_PIN]) {
        std::string pin("6174");
        getservercert(ptr->second, tree, pin);
      }
      else if(config::sunshine.flags[config::flag::PIN_STDIN]) {
        std::string pin;

        std::cout << "Please insert pin: "sv;
        std::getline(std::cin, pin);

        getservercert(ptr->second, tree, pin);
      }
      else {
        ptr->second.async_insert_pin.response = std::move(response);

        return;
      }
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
    clientpairingsecret(add_cert, sess_it->second, tree, args);
  }
  else {
    tree.put("root.<xmlattr>.status_code", 404);
  }

  std::ostringstream data;

  pt::write_xml(data, tree);
  response->write(data.str());
}

bool pin(std::string pin) {
  pt::ptree tree;
  if(map_id_sess.empty()) {
    return false;
  }

  auto &sess = std::begin(map_id_sess)->second;
  getservercert(sess, tree, pin);

  // response to the request for pin
  std::ostringstream data;
  pt::write_xml(data, tree);

  auto &async_response = sess.async_insert_pin.response;
  if(async_response.has_left() && async_response.left()) {
    async_response.left()->write(data.str());
  }
  else if(async_response.has_right() && async_response.right()) {
    async_response.right()->write(data.str());
  }
  else {
    return false;
  }

  // reset async_response
  async_response = std::decay_t<decltype(async_response.left())>();
  // response to the current request
  return true;
}

template<class T>
void pin(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  print_req<T>(request);

  auto address = request->remote_endpoint_address();
  auto ip_type = net::from_address(address);
  if(ip_type > http::origin_pin_allowed) {
    BOOST_LOG(info) << '[' << address << "] -- denied"sv;

    response->write(SimpleWeb::StatusCode::client_error_forbidden);

    return;
  }

  bool pinResponse = pin(request->path_match[1]);
  if(pinResponse) {
    response->write(SimpleWeb::StatusCode::success_ok);
  }
  else {
    response->write(SimpleWeb::StatusCode::client_error_im_a_teapot);
  }
}

template<class T>
void serverinfo(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
  print_req<T>(request);

  int pair_status = 0;
  if constexpr(std::is_same_v<SimpleWeb::HTTPS, T>) {
    auto args     = request->parse_query_string();
    auto clientID = args.find("uniqueid"s);


    if(clientID != std::end(args)) {
      if(auto it = map_id_client.find(clientID->second); it != std::end(map_id_client)) {
        pair_status = 1;
      }
    }
  }

  pt::ptree tree;

  tree.put("root.<xmlattr>.status_code", 200);
  tree.put("root.hostname", config::nvhttp.sunshine_name);

  tree.put("root.appversion", VERSION);
  tree.put("root.GfeVersion", GFE_VERSION);
  tree.put("root.uniqueid", http::unique_id);
  tree.put("root.mac", platf::get_mac_address(request->local_endpoint_address()));
  tree.put("root.MaxLumaPixelsHEVC", config::video.hevc_mode > 1 ? "1869449984" : "0");
  tree.put("root.LocalIP", request->local_endpoint_address());

  if(config::video.hevc_mode == 3) {
    tree.put("root.ServerCodecModeSupport", "3843");
  }
  else if(config::video.hevc_mode == 2) {
    tree.put("root.ServerCodecModeSupport", "259");
  }
  else {
    tree.put("root.ServerCodecModeSupport", "3");
  }

  if(!config::nvhttp.external_ip.empty()) {
    tree.put("root.ExternalIP", config::nvhttp.external_ip);
  }

  pt::ptree display_nodes;
  for(auto &resolution : config::nvhttp.resolutions) {
    auto pred = [](auto ch) { return ch == ' ' || ch == '\t' || ch == 'x'; };

    auto middle = std::find_if(std::begin(resolution), std::end(resolution), pred);
    if(middle == std::end(resolution)) {
      BOOST_LOG(warning) << resolution << " is not in the proper format for a resolution: WIDTHxHEIGHT"sv;
      continue;
    }

    auto width  = util::from_chars(&*std::begin(resolution), &*middle);
    auto height = util::from_chars(&*(middle + 1), &*std::end(resolution));
    for(auto fps : config::nvhttp.fps) {
      pt::ptree display_node;
      display_node.put("Width", width);
      display_node.put("Height", height);
      display_node.put("RefreshRate", fps);

      display_nodes.add_child("DisplayMode", display_node);
    }
  }

  if(!config::nvhttp.resolutions.empty()) {
    tree.add_child("root.SupportedDisplayMode", display_nodes);
  }
  auto current_appid = proc::proc.running();
  tree.put("root.PairStatus", pair_status);
  tree.put("root.currentgame", current_appid >= 0 ? current_appid + 1 : 0);
  tree.put("root.state", current_appid >= 0 ? "SUNSHINE_SERVER_BUSY" : "SUNSHINE_SERVER_FREE");

  std::ostringstream data;

  pt::write_xml(data, tree);
  response->write(data.str());
}

void applist(resp_https_t response, req_https_t request) {
  print_req<SimpleWeb::HTTPS>(request);

  pt::ptree tree;

  auto g = util::fail_guard([&]() {
    std::ostringstream data;

    pt::write_xml(data, tree);
    response->write(data.str());
  });

  auto args = request->parse_query_string();
  if(args.find("uniqueid"s) == std::end(args)) {
    tree.put("root.<xmlattr>.status_code", 400);

    return;
  }

  auto clientID = args.at("uniqueid"s);

  auto client = map_id_client.find(clientID);
  if(client == std::end(map_id_client)) {
    tree.put("root.<xmlattr>.status_code", 501);

    return;
  }

  auto &apps = tree.add_child("root", pt::ptree {});

  apps.put("<xmlattr>.status_code", 200);

  int x = 0;
  for(auto &proc : proc::proc.get_apps()) {
    pt::ptree app;

    app.put("IsHdrSupported"s, config::video.hevc_mode == 3 ? 1 : 0);
    app.put("AppTitle"s, proc.name);
    app.put("ID"s, ++x);

    apps.push_back(std::make_pair("App", std::move(app)));
  }
}

void launch(bool &host_audio, resp_https_t response, req_https_t request) {
  print_req<SimpleWeb::HTTPS>(request);

  pt::ptree tree;
  auto g = util::fail_guard([&]() {
    std::ostringstream data;

    pt::write_xml(data, tree);
    response->write(data.str());
  });

  if(stream::session_count() == config::stream.channels) {
    tree.put("root.resume", 0);
    tree.put("root.<xmlattr>.status_code", 503);

    return;
  }

  auto args = request->parse_query_string();
  if(
    args.find("rikey"s) == std::end(args) ||
    args.find("rikeyid"s) == std::end(args) ||
    args.find("localAudioPlayMode"s) == std::end(args) ||
    args.find("appid"s) == std::end(args)) {

    tree.put("root.resume", 0);
    tree.put("root.<xmlattr>.status_code", 400);

    return;
  }

  auto appid = util::from_view(args.at("appid")) - 1;

  auto current_appid = proc::proc.running();
  if(current_appid != -1) {
    tree.put("root.resume", 0);
    tree.put("root.<xmlattr>.status_code", 400);

    return;
  }

  if(appid >= 0) {
    auto err = proc::proc.execute(appid);
    if(err) {
      tree.put("root.<xmlattr>.status_code", err);
      tree.put("root.gamesession", 0);

      return;
    }
  }

  host_audio = util::from_view(args.at("localAudioPlayMode"));
  stream::launch_session_raise(make_launch_session(host_audio, args));

  tree.put("root.<xmlattr>.status_code", 200);
  tree.put("root.gamesession", 1);
}

void resume(bool &host_audio, resp_https_t response, req_https_t request) {
  print_req<SimpleWeb::HTTPS>(request);

  pt::ptree tree;
  auto g = util::fail_guard([&]() {
    std::ostringstream data;

    pt::write_xml(data, tree);
    response->write(data.str());
  });

  // It is possible that due a race condition that this if-statement gives a false negative,
  // that is automatically resolved in rtsp_server_t
  if(stream::session_count() == config::stream.channels) {
    tree.put("root.resume", 0);
    tree.put("root.<xmlattr>.status_code", 503);

    return;
  }

  auto current_appid = proc::proc.running();
  if(current_appid == -1) {
    tree.put("root.resume", 0);
    tree.put("root.<xmlattr>.status_code", 503);

    return;
  }

  auto args = request->parse_query_string();
  if(
    args.find("rikey"s) == std::end(args) ||
    args.find("rikeyid"s) == std::end(args)) {

    tree.put("root.resume", 0);
    tree.put("root.<xmlattr>.status_code", 400);

    return;
  }

  stream::launch_session_raise(make_launch_session(host_audio, args));

  tree.put("root.<xmlattr>.status_code", 200);
  tree.put("root.resume", 1);
}

void cancel(resp_https_t response, req_https_t request) {
  print_req<SimpleWeb::HTTPS>(request);

  pt::ptree tree;
  auto g = util::fail_guard([&]() {
    std::ostringstream data;

    pt::write_xml(data, tree);
    response->write(data.str());
  });

  // It is possible that due a race condition that this if-statement gives a false positive,
  // the client should try again
  if(stream::session_count() != 0) {
    tree.put("root.resume", 0);
    tree.put("root.<xmlattr>.status_code", 503);

    return;
  }

  tree.put("root.cancel", 1);
  tree.put("root.<xmlattr>.status_code", 200);

  if(proc::proc.running() != -1) {
    proc::proc.terminate();
  }
}

void appasset(resp_https_t response, req_https_t request) {
  print_req<SimpleWeb::HTTPS>(request);

  std::ifstream in(SUNSHINE_ASSETS_DIR "/box.png");
  response->write(SimpleWeb::StatusCode::success_ok, in);
}

void start() {
  auto shutdown_event = mail::man->event<bool>(mail::shutdown);

  bool clean_slate = config::sunshine.flags[config::flag::FRESH_STATE];

  if(!clean_slate) {
    load_state();
  }

  conf_intern.pkey       = read_file(config::nvhttp.pkey.c_str());
  conf_intern.servercert = read_file(config::nvhttp.cert.c_str());

  auto ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tls);
  ctx->use_certificate_chain_file(config::nvhttp.cert);
  ctx->use_private_key_file(config::nvhttp.pkey, boost::asio::ssl::context::pem);

  crypto::cert_chain_t cert_chain;
  for(auto &[_, client] : map_id_client) {
    for(auto &cert : client.certs) {
      cert_chain.add(crypto::x509(cert));
    }
  }

  auto add_cert = std::make_shared<safe::queue_t<crypto::x509_t>>(30);

  // Ugly hack for verifying certificates, see crypto::cert_chain_t::verify() for details
  ctx->set_verify_callback([&cert_chain, add_cert](int verified, boost::asio::ssl::verify_context &ctx) {
    auto fg = util::fail_guard([&]() {
      char subject_name[256];

      auto x509 = ctx.native_handle();
      X509_NAME_oneline(X509_get_subject_name(X509_STORE_CTX_get_current_cert(x509)), subject_name, sizeof(subject_name));


      BOOST_LOG(info) << subject_name << " -- "sv << (verified ? "verfied"sv : "denied"sv);
    });

    if(verified) {
      return 1;
    }

    while(add_cert->peek()) {
      char subject_name[256];

      auto cert = add_cert->pop();
      X509_NAME_oneline(X509_get_subject_name(cert.get()), subject_name, sizeof(subject_name));

      BOOST_LOG(debug) << "Added cert ["sv << subject_name << ']';
      cert_chain.add(std::move(cert));
    }

    auto err_str = cert_chain.verify(X509_STORE_CTX_get_current_cert(ctx.native_handle()));
    if(err_str) {
      BOOST_LOG(warning) << "SSL Verification error :: "sv << err_str;
      return 0;
    }

    verified = 1;
    return 1;
  });

  // /resume doesn't get the parameter "localAudioPlayMode"
  // /launch will store it in host_audio
  bool host_audio {};

  https_server_t https_server { ctx, boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert | boost::asio::ssl::verify_client_once };
  http_server_t http_server;

  https_server.default_resource                   = not_found<SimpleWeb::HTTPS>;
  https_server.resource["^/serverinfo$"]["GET"]   = serverinfo<SimpleWeb::HTTPS>;
  https_server.resource["^/pair$"]["GET"]         = [&add_cert](auto resp, auto req) { pair<SimpleWeb::HTTPS>(add_cert, resp, req); };
  https_server.resource["^/applist$"]["GET"]      = applist;
  https_server.resource["^/appasset$"]["GET"]     = appasset;
  https_server.resource["^/launch$"]["GET"]       = [&host_audio](auto resp, auto req) { launch(host_audio, resp, req); };
  https_server.resource["^/pin/([0-9]+)$"]["GET"] = pin<SimpleWeb::HTTPS>;
  https_server.resource["^/resume$"]["GET"]       = [&host_audio](auto resp, auto req) { resume(host_audio, resp, req); };
  https_server.resource["^/cancel$"]["GET"]       = cancel;

  https_server.config.reuse_address = true;
  https_server.config.address       = "0.0.0.0"s;
  https_server.config.port          = PORT_HTTPS;

  http_server.default_resource                   = not_found<SimpleWeb::HTTP>;
  http_server.resource["^/serverinfo$"]["GET"]   = serverinfo<SimpleWeb::HTTP>;
  http_server.resource["^/pair$"]["GET"]         = [&add_cert](auto resp, auto req) { pair<SimpleWeb::HTTP>(add_cert, resp, req); };
  http_server.resource["^/pin/([0-9]+)$"]["GET"] = pin<SimpleWeb::HTTP>;

  http_server.config.reuse_address = true;
  http_server.config.address       = "0.0.0.0"s;
  http_server.config.port          = PORT_HTTP;

  try {
    https_server.bind();
    http_server.bind();
  }
  catch(boost::system::system_error &err) {
    BOOST_LOG(fatal) << "Couldn't bind http server to ports ["sv << PORT_HTTPS << ", "sv << PORT_HTTP << "]: "sv << err.what();

    shutdown_event->raise(true);
    return;
  }

  auto accept_and_run = [&](auto *http_server) {
    try {
      http_server->accept_and_run();
    }
    catch(boost::system::system_error &err) {
      // It's possible the exception gets thrown after calling http_server->stop() from a different thread
      if(shutdown_event->peek()) {
        return;
      }

      BOOST_LOG(fatal) << "Couldn't start http server to ports ["sv << PORT_HTTPS << ", "sv << PORT_HTTP << "]: "sv << err.what();
      shutdown_event->raise(true);
      return;
    }
  };
  std::thread ssl { accept_and_run, &https_server };
  std::thread tcp { accept_and_run, &http_server };

  // Wait for any event
  shutdown_event->view();

  https_server.stop();
  http_server.stop();

  ssl.join();
  tcp.join();
}
} // namespace nvhttp
