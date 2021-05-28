#include "process.h"

#include <filesystem>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <boost/asio/ssl/context.hpp>

#include <Simple-Web-Server/server_http.hpp>
#include <Simple-Web-Server/server_https.hpp>
#include <boost/asio/ssl/context_base.hpp>

#include "config.h"
#include "utility.h"
#include "rtsp.h"
#include "crypto.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "network.h"
#include "uuid.h"
#include "main.h"
#include "httpcommon.h"

namespace http
{
  using namespace std::literals;
  namespace fs = std::filesystem;
  namespace pt = boost::property_tree;

  int create_creds(const std::string &pkey, const std::string &cert);
  int generate_user_creds(const std::string &file);
  int reload_user_creds(const std::string &file);
  std::string read_file(const char *path);
  int write_file(const char *path, const std::string_view &contents);
  std::string unique_id;
  net::net_e origin_pin_allowed;
  
  void init(std::shared_ptr<safe::signal_t> shutdown_event)
  {
    bool clean_slate = config::sunshine.flags[config::flag::FRESH_STATE];
    origin_pin_allowed = net::from_enum_string(config::nvhttp.origin_pin_allowed);
    if (clean_slate)
    {
      unique_id = util::uuid_t::generate().string();
      auto dir = std::filesystem::temp_directory_path() / "Sushine"sv;
      config::nvhttp.cert = (dir / ("cert-"s + unique_id)).string();
      config::nvhttp.pkey = (dir / ("pkey-"s + unique_id)).string();
    }

    if (!fs::exists(config::nvhttp.pkey) || !fs::exists(config::nvhttp.cert))
    {
      if (create_creds(config::nvhttp.pkey, config::nvhttp.cert))
      {
        shutdown_event->raise(true);
        return;
      }
    }
    if(!fs::exists(config::sunshine.credentials_file)){
      if(generate_user_creds(config::sunshine.credentials_file)){
        shutdown_event->raise(true);
        return;
      }
    }
    if(reload_user_creds(config::sunshine.credentials_file)){
      shutdown_event->raise(true);
      return;
    }
  }

  int generate_user_creds(const std::string &file)
  {
    pt::ptree outputTree;
    try {
      std::string username = "sunshine";
      std::string plainPassword = crypto::rand_string(16);
      std::string salt = crypto::rand_string(16);
      outputTree.put("username","sunshine");
      outputTree.put("salt",salt);
      outputTree.put("password",crypto::hash_hexstr(plainPassword + salt));
      BOOST_LOG(info) << "New credentials has been created";
      BOOST_LOG(info) << "Username: " << username;
      BOOST_LOG(info) << "Password: " << plainPassword;
      pt::write_json(file,outputTree);
    } catch (std::exception &e){
      BOOST_LOG(fatal) << e.what();
      return 1;
    }
    return 0;
  }

  int reload_user_creds(const std::string &file)
  {
    pt::ptree inputTree;
    try {
      pt::read_json(file, inputTree);
      config::sunshine.username = inputTree.get<std::string>("username");
      config::sunshine.password = inputTree.get<std::string>("password");
      config::sunshine.salt = inputTree.get<std::string>("salt");
    } catch(std::exception &e){
      BOOST_LOG(fatal) << e.what();
      return 1;
    }
    return 0;
  }
  
  int create_creds(const std::string &pkey, const std::string &cert)
  {
    fs::path pkey_path = pkey;
    fs::path cert_path = cert;

    auto creds = crypto::gen_creds("Sunshine Gamestream Host"sv, 2048);

    auto pkey_dir = pkey_path;
    auto cert_dir = cert_path;
    pkey_dir.remove_filename();
    cert_dir.remove_filename();

    std::error_code err_code{};
    fs::create_directories(pkey_dir, err_code);
    if (err_code)
    {
      BOOST_LOG(fatal) << "Couldn't create directory ["sv << pkey_dir << "] :"sv << err_code.message();
      return -1;
    }

    fs::create_directories(cert_dir, err_code);
    if (err_code)
    {
      BOOST_LOG(fatal) << "Couldn't create directory ["sv << cert_dir << "] :"sv << err_code.message();
      return -1;
    }

    if (write_file(pkey.c_str(), creds.pkey))
    {
      BOOST_LOG(fatal) << "Couldn't open ["sv << config::nvhttp.pkey << ']';
      return -1;
    }

    if (write_file(cert.c_str(), creds.x509))
    {
      BOOST_LOG(fatal) << "Couldn't open ["sv << config::nvhttp.cert << ']';
      return -1;
    }

    fs::permissions(pkey_path,
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, err_code);

    if (err_code)
    {
      BOOST_LOG(fatal) << "Couldn't change permissions of ["sv << config::nvhttp.pkey << "] :"sv << err_code.message();
      return -1;
    }

    fs::permissions(cert_path,
                    fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read | fs::perms::owner_write,
                    fs::perm_options::replace, err_code);

    if (err_code)
    {
      BOOST_LOG(fatal) << "Couldn't change permissions of ["sv << config::nvhttp.cert << "] :"sv << err_code.message();
      return -1;
    }

    return 0;
  }
  int write_file(const char *path, const std::string_view &contents)
  {
    std::ofstream out(path);

    if (!out.is_open())
    {
      return -1;
    }

    out << contents;

    return 0;
  }

  std::string read_file(const char *path)
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
}