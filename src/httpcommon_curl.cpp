#include <curl/curl.h>
#include <string>

#include "logging.h"

namespace http {
  using namespace std::literals;

  bool
  download_file(const std::string &url, const std::string &file) {
    CURL *curl = curl_easy_init();
    if (!curl) {
      BOOST_LOG(error) << "Couldn't create CURL instance";
      return false;
    }
    FILE *fp = fopen(file.c_str(), "wb");
    if (!fp) {
      BOOST_LOG(error) << "Couldn't open ["sv << file << ']';
      curl_easy_cleanup(curl);
      return false;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
      BOOST_LOG(error) << "Couldn't download ["sv << url << ", code:" << result << ']';
    }
    curl_easy_cleanup(curl);
    fclose(fp);
    return result == CURLE_OK;
  }

  std::string
  url_escape(const std::string &url) {
    CURL *curl = curl_easy_init();
    char *string = curl_easy_escape(curl, url.c_str(), (int) url.length());
    std::string result(string);
    curl_free(string);
    curl_easy_cleanup(curl);
    return result;
  }

  std::string
  url_get_host(const std::string &url) {
    CURLU *curlu = curl_url();
    curl_url_set(curlu, CURLUPART_URL, url.c_str(), url.length());
    char *host;
    if (curl_url_get(curlu, CURLUPART_HOST, &host, 0) != CURLUE_OK) {
      curl_url_cleanup(curlu);
      return "";
    }
    std::string result(host);
    curl_free(host);
    curl_url_cleanup(curlu);
    return result;
  }
}  // namespace http