#include <fstream>
#include <string>

#include <boost/url.hpp>
#include <windows.h>
#include <wininet.h>

namespace http {

  using namespace boost::urls;

  bool
  download_file(const std::string &url, const std::string &file) {
    HINTERNET hInternet, hConnect;
    DWORD bytesRead;

    // Initialize WinINet
    hInternet = InternetOpenA("Sunshine", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (hInternet == nullptr) {
      // Handle error
      return false;
    }

    // Connect to the website
    hConnect = InternetOpenUrlA(hInternet, url.c_str(), nullptr, 0, INTERNET_FLAG_RELOAD, 0);
    if (hConnect == nullptr) {
      // Handle error
      InternetCloseHandle(hInternet);
      return false;
    }

    // Create a buffer to receive the data
    char buffer[4096];

    // Open the destination file
    std::ofstream outFile(file, std::ios::binary);

    // Read the data from the website
    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
      outFile.write(buffer, bytesRead);
    }

    // Cleanup
    outFile.close();
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    return true;
  }

  std::string
  url_escape(const std::string &str) {
    return encode(str, unreserved_chars);
  }

  std::string
  url_get_host(const std::string &url) {
    auto parsed = parse_uri(url);
    if (!parsed) {
      return "";
    }
    return parsed->host();
  }

}  // namespace http
