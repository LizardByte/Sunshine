#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <pwd.h>
#include <unistd.h>

#include <fstream>

#include "graphics.h"
#include "misc.h"
#include "vaapi.h"

#include "sunshine/main.h"
#include "sunshine/platform/common.h"

#ifdef __GNUC__
#define SUNSHINE_GNUC_EXTENSION __extension__
#else
#define SUNSHINE_GNUC_EXTENSION
#endif

using namespace std::literals;
namespace fs = std::filesystem;

window_system_e window_system;

namespace dyn {
void *handle(const std::vector<const char *> &libs) {
  void *handle;

  for(auto lib : libs) {
    handle = dlopen(lib, RTLD_LAZY | RTLD_LOCAL);
    if(handle) {
      return handle;
    }
  }

  std::stringstream ss;
  ss << "Couldn't find any of the following libraries: ["sv << libs.front();
  std::for_each(std::begin(libs) + 1, std::end(libs), [&](auto lib) {
    ss << ", "sv << lib;
  });

  ss << ']';

  BOOST_LOG(error) << ss.str();

  return nullptr;
}

int load(void *handle, const std::vector<std::tuple<apiproc *, const char *>> &funcs, bool strict) {
  int err = 0;
  for(auto &func : funcs) {
    TUPLE_2D_REF(fn, name, func);

    *fn = SUNSHINE_GNUC_EXTENSION(apiproc) dlsym(handle, name);

    if(!*fn && strict) {
      BOOST_LOG(error) << "Couldn't find function: "sv << name;

      err = -1;
    }
  }

  return err;
}
} // namespace dyn
namespace platf {
using ifaddr_t = util::safe_ptr<ifaddrs, freeifaddrs>;

ifaddr_t get_ifaddrs() {
  ifaddrs *p { nullptr };

  getifaddrs(&p);

  return ifaddr_t { p };
}

fs::path appdata() {
  const char *homedir;
  if((homedir = getenv("HOME")) == nullptr) {
    homedir = getpwuid(geteuid())->pw_dir;
  }

  return fs::path { homedir } / ".config/sunshine"sv;
}

std::string from_sockaddr(const sockaddr *const ip_addr) {
  char data[INET6_ADDRSTRLEN];

  auto family = ip_addr->sa_family;
  if(family == AF_INET6) {
    inet_ntop(AF_INET6, &((sockaddr_in6 *)ip_addr)->sin6_addr, data,
      INET6_ADDRSTRLEN);
  }

  if(family == AF_INET) {
    inet_ntop(AF_INET, &((sockaddr_in *)ip_addr)->sin_addr, data,
      INET_ADDRSTRLEN);
  }

  return std::string { data };
}

std::pair<std::uint16_t, std::string> from_sockaddr_ex(const sockaddr *const ip_addr) {
  char data[INET6_ADDRSTRLEN];

  auto family = ip_addr->sa_family;
  std::uint16_t port;
  if(family == AF_INET6) {
    inet_ntop(AF_INET6, &((sockaddr_in6 *)ip_addr)->sin6_addr, data,
      INET6_ADDRSTRLEN);
    port = ((sockaddr_in6 *)ip_addr)->sin6_port;
  }

  if(family == AF_INET) {
    inet_ntop(AF_INET, &((sockaddr_in *)ip_addr)->sin_addr, data,
      INET_ADDRSTRLEN);
    port = ((sockaddr_in *)ip_addr)->sin_port;
  }

  return { port, std::string { data } };
}

std::string get_mac_address(const std::string_view &address) {
  auto ifaddrs = get_ifaddrs();
  for(auto pos = ifaddrs.get(); pos != nullptr; pos = pos->ifa_next) {
    if(pos->ifa_addr && address == from_sockaddr(pos->ifa_addr)) {
      std::ifstream mac_file("/sys/class/net/"s + pos->ifa_name + "/address");
      if(mac_file.good()) {
        std::string mac_address;
        std::getline(mac_file, mac_address);
        return mac_address;
      }
    }
  }

  BOOST_LOG(warning) << "Unable to find MAC address for "sv << address;
  return "00:00:00:00:00:00"s;
}

enum class source_e {
#ifdef SUNSHINE_BUILD_WAYLAND
  WAYLAND,
#endif
#ifdef SUNSHINE_BUILD_DRM
  KMS,
#endif
#ifdef SUNSHINE_BUILD_X11
  X11,
#endif
};
static source_e source;

#ifdef SUNSHINE_BUILD_WAYLAND
std::vector<std::string> wl_display_names();
std::shared_ptr<display_t> wl_display(mem_type_e hwdevice_type, const std::string &display_name, int framerate);

bool verify_wl() {
  return window_system == window_system_e::WAYLAND && !wl_display_names().empty();
}
#endif

#ifdef SUNSHINE_BUILD_DRM
std::vector<std::string> kms_display_names();
std::shared_ptr<display_t> kms_display(mem_type_e hwdevice_type, const std::string &display_name, int framerate);

bool verify_kms() {
  return !kms_display_names().empty();
}
#endif

#ifdef SUNSHINE_BUILD_X11
std::vector<std::string> x11_display_names();
std::shared_ptr<display_t> x11_display(mem_type_e hwdevice_type, const std::string &display_name, int framerate);

bool verify_x11() {
  return window_system == window_system_e::X11 && !x11_display_names().empty();
}
#endif

std::vector<std::string> display_names() {
  switch(source) {
#ifdef SUNSHINE_BUILD_WAYLAND
  case source_e::WAYLAND:
    return wl_display_names();
#endif
#ifdef SUNSHINE_BUILD_DRM
  case source_e::KMS:
    return kms_display_names();
#endif
#ifdef SUNSHINE_BUILD_X11
  case source_e::X11:
    return x11_display_names();
#endif
  }

  return {};
}

std::shared_ptr<display_t> display(mem_type_e hwdevice_type, const std::string &display_name, int framerate) {
  switch(source) {
#ifdef SUNSHINE_BUILD_WAYLAND
  case source_e::WAYLAND:
    return wl_display(hwdevice_type, display_name, framerate);
#endif
#ifdef SUNSHINE_BUILD_DRM
  case source_e::KMS:
    return kms_display(hwdevice_type, display_name, framerate);
#endif
#ifdef SUNSHINE_BUILD_X11
  case source_e::X11:
    return x11_display(hwdevice_type, display_name, framerate);
#endif
  }

  return nullptr;
}

std::unique_ptr<deinit_t> init() {
  // These are allowed to fail.
  gbm::init();
  va::init();

  window_system = window_system_e::NONE;
#ifdef SUNSHINE_BUILD_WAYLAND
  if(std::getenv("WAYLAND_DISPLAY")) {
    window_system = window_system_e::WAYLAND;
  }
#endif
#ifdef SUNSHINE_BUILD_X11
  if(std::getenv("DISPLAY") && window_system != window_system_e::WAYLAND) {
    if(std::getenv("WAYLAND_DISPLAY")) {
      BOOST_LOG(warning) << "Wayland detected, yet sunshine will use X11 for screencasting, screencasting will only work on XWayland applications"sv;
    }

    window_system = window_system_e::X11;
  }
#endif
#ifdef SUNSHINE_BUILD_WAYLAND
  if(verify_wl()) {
    BOOST_LOG(info) << "Using Wayland for screencasting"sv;
    source = source_e::WAYLAND;
    goto found_source;
  }
#endif
#ifdef SUNSHINE_BUILD_DRM
  if(verify_kms()) {
    if(window_system == window_system_e::WAYLAND) {
      // On Wayland, using KMS, the cursor is unreliable.
      // Hide it by default
      display_cursor = false;
    }

    BOOST_LOG(info) << "Using KMS for screencasting"sv;
    source = source_e::KMS;
    goto found_source;
  }
#endif
#ifdef SUNSHINE_BUILD_X11
  if(verify_x11()) {
    BOOST_LOG(info) << "Using X11 for screencasting"sv;
    source = source_e::X11;
    goto found_source;
  }
#endif
  // Did not find a source
  return nullptr;

// Normally, I would simply use if-else statements to achieve this result,
// but due to the macro's, (*spits on ground*), it would be too messy
found_source:
  if(!gladLoaderLoadEGL(EGL_NO_DISPLAY) || !eglGetPlatformDisplay) {
    BOOST_LOG(warning) << "Couldn't load EGL library"sv;
  }

  return std::make_unique<deinit_t>();
}
} // namespace platf