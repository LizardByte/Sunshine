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

namespace http {
using namespace std::literals;
namespace fs = std::filesystem;
namespace pt = boost::property_tree;

std::string unique_id;
net::net_e origin_pin_allowed;
net::net_e origin_web_api_allowed;

int init() {
  bool clean_slate      = config::sunshine.flags[config::flag::FRESH_STATE];
  origin_pin_allowed    = net::from_enum_string(config::nvhttp.origin_pin_allowed);
  origin_web_api_allowed = net::from_enum_string(config::nvhttp.origin_web_api_allowed);

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
  return 0;
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
    BOOST_LOG(error) << "Couldn't open ["sv << config::nvhttp.pkey << ']';
    return -1;
  }

  if(write_file(cert.c_str(), creds.x509)) {
    BOOST_LOG(error) << "Couldn't open ["sv << config::nvhttp.cert << ']';
    return -1;
  }

  fs::permissions(pkey_path,
    fs::perms::owner_read | fs::perms::owner_write,
    fs::perm_options::replace, err_code);

  if(err_code) {
    BOOST_LOG(error) << "Couldn't change permissions of ["sv << config::nvhttp.pkey << "] :"sv << err_code.message();
    return -1;
  }

  fs::permissions(cert_path,
    fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read | fs::perms::owner_write,
    fs::perm_options::replace, err_code);

  if(err_code) {
    BOOST_LOG(error) << "Couldn't change permissions of ["sv << config::nvhttp.cert << "] :"sv << err_code.message();
    return -1;
  }

  return 0;
}
} // namespace http