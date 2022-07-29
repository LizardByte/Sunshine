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
#include "version.h"

namespace http {
using namespace std::literals;
namespace fs = std::filesystem;
namespace pt = boost::property_tree;

std::string unique_id;
net::net_e origin_pin_allowed;

int init() {
  bool clean_slate      = config::sunshine.flags[config::flag::FRESH_STATE];
  origin_pin_allowed    = net::from_enum_string(config::nvhttp.origin_pin_allowed);

  if(clean_slate) {
    unique_id           = util::uuid_t::generate().string();
    auto dir            = std::filesystem::temp_directory_path() / "Sushine"sv;
    config::nvhttp.cert = (dir / ("cert-"s + unique_id)).string();
    config::nvhttp.pkey = (dir / ("pkey-"s + unique_id)).string();
  }

  if(!fs::exists(config::nvhttp.pkey) || !fs::exists(config::nvhttp.cert)) {
    if(create_creds(config::nvhttp.pkey, config::nvhttp.cert)) {
      return -1;
    }
  }

  if(!credentials_exists()) {
    save_credentials("", false);
  }
  return 0;
}

bool save_credentials(std::string password, bool isHashed) {
  pt::ptree outputTree;
  const std::string file = config::sunshine.credentials_file;

  if(password.empty()) {
    password = crypto::rand_alphabet(8);
    BOOST_LOG(warning) << "API password has been randomly generated: " << password;
  }

  std::string hash_hex_full = password;
  if(!isHashed) {
    auto passwordHash = crypto::hash(password);
    hash_hex_full     = util::hex_vec(passwordHash.begin(), passwordHash.end(), true);  
  }

  outputTree.put("hash", hash_hex_full);
  outputTree.put("version", PROJECT_VER);

  std::ostringstream data;
  try {
    pt::write_json(data, outputTree, false);
    std::string plaintext = data.str();

    crypto::aes_t key = util::from_hex<crypto::aes_t>(hash_hex_full.substr(0, 16), true);

    crypto::cipher::gcm_t gcm(key, true);
    crypto::aes_t iv;
    RAND_bytes((uint8_t *)iv.data(), iv.size());

    auto cipher_len = crypto::cipher::round_to_pkcs7_padded(plaintext.size()) + crypto::cipher::tag_size;
    uint8_t cipher[cipher_len];
    gcm.encrypt(plaintext, &cipher[0], &iv);

    std::string cipher_output { (char *)cipher, cipher_len };
    std::string iv_output { (char *)iv.data(), iv.size() };

    write_file(file.c_str(), iv_output + cipher_output);
  }
  catch(std::exception &e) {
    BOOST_LOG(error) << "Failed to save credentials: "sv << e.what();
    return false;
  }

  BOOST_LOG(info) << "New credentials have been created"sv;
  return true;
}
int renew_credentials(const std::string &old_password, std::string new_password) {
  if(credentials_exists() == false) return -3;
  if(load_credentials(old_password) == false) return -4;
  return save_credentials(new_password, true);
}

bool credentials_exists() {
  return fs::exists(config::sunshine.credentials_file);
}

bool load_credentials(const std::string &passwordHash) {

  pt::ptree inputTree;
  try {
    std::string credsFile = read_file(config::sunshine.credentials_file.c_str());
    if(credsFile.size() <= 16) {
      BOOST_LOG(error) << "Failed to load API credentials. Corrupt file: invalid length.";
      return false;
    }
    std::string iv_str = credsFile.substr(0, 16);
    std::string cipher = credsFile.substr(16);
    crypto::aes_t iv;
    std::copy(iv_str.begin(), iv_str.end(), iv.begin());

    crypto::aes_t key = util::from_hex<crypto::aes_t>(passwordHash.substr(0, 16), true);

    crypto::cipher::gcm_t gcm(key, true);

    std::vector<uint8_t> plaintext_vec;
    gcm.decrypt(cipher, plaintext_vec, &iv);

    std::string plaintext { (char *)plaintext_vec.data(), plaintext_vec.size() };
    std::string json = plaintext.substr(0, plaintext.find_last_of('\n') + 1);

    std::stringstream data;
    data << json;
    pt::read_json(data, inputTree);
    auto hash = inputTree.get<std::string>("hash");
    return passwordHash.compare(hash) == 0;
  }
  catch(std::exception &e) {
    BOOST_LOG(error) << "Failed to load API credentials. Incorrect password or corrupt file.";
    return false;
  }
}

int create_creds(const std::string &pkey, const std::string &cert) {
  fs::path pkey_path = pkey;
  fs::path cert_path = cert;

  auto creds = crypto::gen_creds("Sunshine Gamestream Host"sv, 2048);

  auto pkey_dir = pkey_path;
  auto cert_dir = cert_path;
  pkey_dir.remove_filename();
  cert_dir.remove_filename();

  std::error_code err_code {};
  fs::create_directories(pkey_dir, err_code);
  if(err_code) {
    BOOST_LOG(error) << "Couldn't create directory ["sv << pkey_dir << "] :"sv << err_code.message();
    return -1;
  }

  fs::create_directories(cert_dir, err_code);
  if(err_code) {
    BOOST_LOG(error) << "Couldn't create directory ["sv << cert_dir << "] :"sv << err_code.message();
    return -1;
  }

  if(write_file(pkey.c_str(), creds.pkey)) {
    BOOST_LOG(error) << "Couldn't open ["sv << pkey << ']';
    return -1;
  }

  if(write_file(cert.c_str(), creds.x509)) {
    BOOST_LOG(error) << "Couldn't open ["sv << cert << ']';
    return -1;
  }

  fs::permissions(pkey_path,
    fs::perms::owner_read | fs::perms::owner_write,
    fs::perm_options::replace, err_code);

  if(err_code) {
    BOOST_LOG(error) << "Couldn't change permissions of ["sv << pkey << "] :"sv << err_code.message();
    return -1;
  }

  fs::permissions(cert_path,
    fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read | fs::perms::owner_write,
    fs::perm_options::replace, err_code);

  if(err_code) {
    BOOST_LOG(error) << "Couldn't change permissions of ["sv << cert << "] :"sv << err_code.message();
    return -1;
  }

  return 0;
}
} // namespace http