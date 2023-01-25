#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/udp.h>
#include <pwd.h>
#include <unistd.h>

#include <fstream>

#include "graphics.h"
#include "misc.h"
#include "vaapi.h"

#include "src/main.h"
#include "src/platform/common.h"

#include <boost/asio/ip/address.hpp>
#include <boost/process.hpp>

#ifdef __GNUC__
#define SUNSHINE_GNUC_EXTENSION __extension__
#else
#define SUNSHINE_GNUC_EXTENSION
#endif

using namespace std::literals;
namespace fs = std::filesystem;
namespace bp = boost::process;

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

bp::child run_unprivileged(const std::string &cmd, boost::filesystem::path &working_dir, bp::environment &env, FILE *file, std::error_code &ec, bp::group *group) {
  BOOST_LOG(warning) << "run_unprivileged() is not yet implemented for this platform. The new process will run with Sunshine's permissions."sv;
  if(!group) {
    if(!file) {
      return bp::child(cmd, env, bp::start_dir(working_dir), bp::std_out > bp::null, bp::std_err > bp::null, ec);
    }
    else {
      return bp::child(cmd, env, bp::start_dir(working_dir), bp::std_out > file, bp::std_err > file, ec);
    }
  }
  else {
    if(!file) {
      return bp::child(cmd, env, bp::start_dir(working_dir), bp::std_out > bp::null, bp::std_err > bp::null, ec, *group);
    }
    else {
      return bp::child(cmd, env, bp::start_dir(working_dir), bp::std_out > file, bp::std_err > file, ec, *group);
    }
  }
}

void adjust_thread_priority(thread_priority_e priority) {
  // Unimplemented
}

void streaming_will_start() {
  // Nothing to do
}

void streaming_will_stop() {
  // Nothing to do
}

bool restart_supported() {
  // Restart not supported yet
  return false;
}

bool restart() {
  // Restart not supported yet
  return false;
}

bool send_batch(batched_send_info_t &send_info) {
  auto sockfd = (int)send_info.native_socket;

  // Convert the target address into a sockaddr
  struct sockaddr_in saddr_v4  = {};
  struct sockaddr_in6 saddr_v6 = {};
  struct sockaddr *addr;
  socklen_t addr_len;
  if(send_info.target_address.is_v6()) {
    auto address_v6 = send_info.target_address.to_v6();

    saddr_v6.sin6_family   = AF_INET6;
    saddr_v6.sin6_port     = htons(send_info.target_port);
    saddr_v6.sin6_scope_id = address_v6.scope_id();

    auto addr_bytes = address_v6.to_bytes();
    memcpy(&saddr_v6.sin6_addr, addr_bytes.data(), sizeof(saddr_v6.sin6_addr));

    addr     = (struct sockaddr *)&saddr_v6;
    addr_len = sizeof(saddr_v6);
  }
  else {
    auto address_v4 = send_info.target_address.to_v4();

    saddr_v4.sin_family = AF_INET;
    saddr_v4.sin_port   = htons(send_info.target_port);

    auto addr_bytes = address_v4.to_bytes();
    memcpy(&saddr_v4.sin_addr, addr_bytes.data(), sizeof(saddr_v4.sin_addr));

    addr     = (struct sockaddr *)&saddr_v4;
    addr_len = sizeof(saddr_v4);
  }

#ifdef UDP_SEGMENT
  {
    struct msghdr msg = {};
    struct iovec iov  = {};
    union {
      char buf[CMSG_SPACE(sizeof(uint16_t))];
      struct cmsghdr alignment;
    } cmbuf;

    // UDP GSO on Linux currently only supports sending 64K or 64 segments at a time
    size_t seg_index     = 0;
    const size_t seg_max = 65536 / 1500;
    while(seg_index < send_info.block_count) {
      iov.iov_base = (void *)&send_info.buffer[seg_index * send_info.block_size];
      iov.iov_len  = send_info.block_size * std::min(send_info.block_count - seg_index, seg_max);

      msg.msg_name    = addr;
      msg.msg_namelen = addr_len;
      msg.msg_iov     = &iov;
      msg.msg_iovlen  = 1;

      // We should not use GSO if the data is <= one full block size
      if(iov.iov_len > send_info.block_size) {
        msg.msg_control    = cmbuf.buf;
        msg.msg_controllen = CMSG_SPACE(sizeof(uint16_t));

        // Enable GSO to perform segmentation of our buffer for us
        auto cm                      = CMSG_FIRSTHDR(&msg);
        cm->cmsg_level               = SOL_UDP;
        cm->cmsg_type                = UDP_SEGMENT;
        cm->cmsg_len                 = CMSG_LEN(sizeof(uint16_t));
        *((uint16_t *)CMSG_DATA(cm)) = send_info.block_size;
      }
      else {
        msg.msg_control    = nullptr;
        msg.msg_controllen = 0;
      }

      // This will fail if GSO is not available, so we will fall back to non-GSO if
      // it's the first sendmsg() call. On subsequent calls, we will treat errors as
      // actual failures and return to the caller.
      auto bytes_sent = sendmsg(sockfd, &msg, 0);
      if(bytes_sent < 0) {
        // If there's no send buffer space, wait for some to be available
        if(errno == EAGAIN) {
          struct pollfd pfd;

          pfd.fd     = sockfd;
          pfd.events = POLLOUT;

          if(poll(&pfd, 1, -1) != 1) {
            BOOST_LOG(warning) << "poll() failed: "sv << errno;
            break;
          }

          // Try to send again
          continue;
        }

        break;
      }

      seg_index += bytes_sent / send_info.block_size;
    }

    // If we sent something, return the status and don't fall back to the non-GSO path.
    if(seg_index != 0) {
      return seg_index >= send_info.block_count;
    }
  }
#endif

  {
    // If GSO is not supported, use sendmmsg() instead.
    struct mmsghdr msgs[send_info.block_count];
    struct iovec iovs[send_info.block_count];
    for(size_t i = 0; i < send_info.block_count; i++) {
      iovs[i]          = {};
      iovs[i].iov_base = (void *)&send_info.buffer[i * send_info.block_size];
      iovs[i].iov_len  = send_info.block_size;

      msgs[i]                     = {};
      msgs[i].msg_hdr.msg_name    = addr;
      msgs[i].msg_hdr.msg_namelen = addr_len;
      msgs[i].msg_hdr.msg_iov     = &iovs[i];
      msgs[i].msg_hdr.msg_iovlen  = 1;
    }

    // Call sendmmsg() until all messages are sent
    size_t blocks_sent = 0;
    while(blocks_sent < send_info.block_count) {
      int msgs_sent = sendmmsg(sockfd, &msgs[blocks_sent], send_info.block_count - blocks_sent, 0);
      if(msgs_sent < 0) {
        // If there's no send buffer space, wait for some to be available
        if(errno == EAGAIN) {
          struct pollfd pfd;

          pfd.fd     = sockfd;
          pfd.events = POLLOUT;

          if(poll(&pfd, 1, -1) != 1) {
            BOOST_LOG(warning) << "poll() failed: "sv << errno;
            break;
          }

          // Try to send again
          continue;
        }

        BOOST_LOG(warning) << "sendmmsg() failed: "sv << errno;
        return false;
      }

      blocks_sent += msgs_sent;
    }

    return true;
  }
}

class qos_t : public deinit_t {
public:
  qos_t(int sockfd, int level, int option) : sockfd(sockfd), level(level), option(option) {}

  virtual ~qos_t() {
    int reset_val = -1;
    if(setsockopt(sockfd, level, option, &reset_val, sizeof(reset_val)) < 0) {
      BOOST_LOG(warning) << "Failed to reset IP TOS: "sv << errno;
    }
  }

private:
  int sockfd;
  int level;
  int option;
};

std::unique_ptr<deinit_t> enable_socket_qos(uintptr_t native_socket, boost::asio::ip::address &address, uint16_t port, qos_data_type_e data_type) {
  int sockfd = (int)native_socket;

  int level;
  int option;
  if(address.is_v6()) {
    level  = SOL_IPV6;
    option = IPV6_TCLASS;
  }
  else {
    level  = SOL_IP;
    option = IP_TOS;
  }

  // The specific DSCP values here are chosen to be consistent with Windows
  int dscp;
  switch(data_type) {
  case qos_data_type_e::video:
    dscp = 40;
    break;
  case qos_data_type_e::audio:
    dscp = 56;
    break;
  default:
    BOOST_LOG(error) << "Unknown traffic type: "sv << (int)data_type;
    return nullptr;
  }

  // Shift to put the DSCP value in the correct position in the TOS field
  dscp <<= 2;

  if(setsockopt(sockfd, level, option, &dscp, sizeof(dscp)) < 0) {
    return nullptr;
  }

  return std::make_unique<qos_t>(sockfd, level, option);
}

namespace source {
enum source_e : std::size_t {
#ifdef SUNSHINE_BUILD_CUDA
  NVFBC,
#endif
#ifdef SUNSHINE_BUILD_WAYLAND
  WAYLAND,
#endif
#ifdef SUNSHINE_BUILD_DRM
  KMS,
#endif
#ifdef SUNSHINE_BUILD_X11
  X11,
#endif
  MAX_FLAGS
};
} // namespace source

static std::bitset<source::MAX_FLAGS> sources;

#ifdef SUNSHINE_BUILD_CUDA
std::vector<std::string> nvfbc_display_names();
std::shared_ptr<display_t> nvfbc_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config);

bool verify_nvfbc() {
  return !nvfbc_display_names().empty();
}
#endif

#ifdef SUNSHINE_BUILD_WAYLAND
std::vector<std::string> wl_display_names();
std::shared_ptr<display_t> wl_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config);

bool verify_wl() {
  return window_system == window_system_e::WAYLAND && !wl_display_names().empty();
}
#endif

#ifdef SUNSHINE_BUILD_DRM
std::vector<std::string> kms_display_names();
std::shared_ptr<display_t> kms_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config);

bool verify_kms() {
  return !kms_display_names().empty();
}
#endif

#ifdef SUNSHINE_BUILD_X11
std::vector<std::string> x11_display_names();
std::shared_ptr<display_t> x11_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config);

bool verify_x11() {
  return window_system == window_system_e::X11 && !x11_display_names().empty();
}
#endif

std::vector<std::string> display_names(mem_type_e hwdevice_type) {
#ifdef SUNSHINE_BUILD_CUDA
  // display using NvFBC only supports mem_type_e::cuda
  if(sources[source::NVFBC] && hwdevice_type == mem_type_e::cuda) return nvfbc_display_names();
#endif
#ifdef SUNSHINE_BUILD_WAYLAND
  if(sources[source::WAYLAND]) return wl_display_names();
#endif
#ifdef SUNSHINE_BUILD_DRM
  if(sources[source::KMS]) return kms_display_names();
#endif
#ifdef SUNSHINE_BUILD_X11
  if(sources[source::X11]) return x11_display_names();
#endif
  return {};
}

std::shared_ptr<display_t> display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
#ifdef SUNSHINE_BUILD_CUDA
  if(sources[source::NVFBC] && hwdevice_type == mem_type_e::cuda) {
    BOOST_LOG(info) << "Screencasting with NvFBC"sv;
    return nvfbc_display(hwdevice_type, display_name, config);
  }
#endif
#ifdef SUNSHINE_BUILD_WAYLAND
  if(sources[source::WAYLAND]) {
    BOOST_LOG(info) << "Screencasting with Wayland's protocol"sv;
    return wl_display(hwdevice_type, display_name, config);
  }
#endif
#ifdef SUNSHINE_BUILD_DRM
  if(sources[source::KMS]) {
    BOOST_LOG(info) << "Screencasting with KMS"sv;
    return kms_display(hwdevice_type, display_name, config);
  }
#endif
#ifdef SUNSHINE_BUILD_X11
  if(sources[source::X11]) {
    BOOST_LOG(info) << "Screencasting with X11"sv;
    return x11_display(hwdevice_type, display_name, config);
  }
#endif

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
#if defined(SUNSHINE_BUILD_X11) || defined(SUNSHINE_BUILD_CUDA)
  if(std::getenv("DISPLAY") && window_system != window_system_e::WAYLAND) {
    if(std::getenv("WAYLAND_DISPLAY")) {
      BOOST_LOG(warning) << "Wayland detected, yet sunshine will use X11 for screencasting, screencasting will only work on XWayland applications"sv;
    }

    window_system = window_system_e::X11;
  }
#endif
#ifdef SUNSHINE_BUILD_CUDA
  if(verify_nvfbc()) {
    sources[source::NVFBC] = true;
  }
#endif
#ifdef SUNSHINE_BUILD_WAYLAND
  if(verify_wl()) {
    sources[source::WAYLAND] = true;
  }
#endif
#ifdef SUNSHINE_BUILD_DRM
  if(verify_kms()) {
    if(window_system == window_system_e::WAYLAND) {
      // On Wayland, using KMS, the cursor is unreliable.
      // Hide it by default
      display_cursor = false;
    }

    sources[source::KMS] = true;
  }
#endif
#ifdef SUNSHINE_BUILD_X11
  if(verify_x11()) {
    sources[source::X11] = true;
  }
#endif

  if(sources.none()) {
    return nullptr;
  }

  if(!gladLoaderLoadEGL(EGL_NO_DISPLAY) || !eglGetPlatformDisplay) {
    BOOST_LOG(warning) << "Couldn't load EGL library"sv;
  }

  return std::make_unique<deinit_t>();
}
} // namespace platf