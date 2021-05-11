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

  int create_creds(const std::string &pkey, const std::string &cert);
  std::string read_file(const char *path);
  int write_file(const char *path, const std::string_view &contents);
  std::string unique_id;
  
  void init(std::shared_ptr<safe::signal_t> shutdown_event)
  {
    bool clean_slate = config::sunshine.flags[config::flag::FRESH_STATE];
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