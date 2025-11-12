/**
 * @file src/platform/linux/misc.cpp
 * @brief Miscellaneous definitions for Linux.
 */

// Required for in6_pktinfo with glibc headers
#ifndef _GNU_SOURCE
  #define _GNU_SOURCE 1
#endif

// standard includes
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

// platform includes
#include <arpa/inet.h>
#include <dlfcn.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <pwd.h>
#include <sys/socket.h>

#ifdef __FreeBSD__
  #include <net/if_dl.h>  // For sockaddr_dl, LLADDR, and AF_LINK
#endif

// lib includes
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/process/v1.hpp>
#include <fcntl.h>
#include <unistd.h>

// local includes
#include "graphics.h"
#include "misc.h"
#include "src/config.h"
#include "src/entry_handler.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "vaapi.h"

#ifdef __GNUC__
  #define SUNSHINE_GNUC_EXTENSION __extension__
#else
  #define SUNSHINE_GNUC_EXTENSION
#endif

#ifndef SOL_IP
  #define SOL_IP IPPROTO_IP
#endif
#ifndef SOL_IPV6
  #define SOL_IPV6 IPPROTO_IPV6
#endif
#ifndef SOL_UDP
  #define SOL_UDP IPPROTO_UDP
#endif

using namespace std::literals;
namespace fs = std::filesystem;
namespace bp = boost::process::v1;

window_system_e window_system;

namespace dyn {
  void *handle(const std::vector<const char *> &libs) {
    void *handle;

    for (auto lib : libs) {
      handle = dlopen(lib, RTLD_LAZY | RTLD_LOCAL);
      if (handle) {
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
    for (auto &func : funcs) {
      TUPLE_2D_REF(fn, name, func);

      *fn = SUNSHINE_GNUC_EXTENSION(apiproc) dlsym(handle, name);

      if (!*fn && strict) {
        BOOST_LOG(error) << "Couldn't find function: "sv << name;

        err = -1;
      }
    }

    return err;
  }
}  // namespace dyn

namespace platf {
  using ifaddr_t = util::safe_ptr<ifaddrs, freeifaddrs>;

  ifaddr_t get_ifaddrs() {
    ifaddrs *p {nullptr};

    getifaddrs(&p);

    return ifaddr_t {p};
  }

  /**
   * @brief Performs migration if necessary, then returns the appdata directory.
   * @details This is used for the log directory, so it cannot invoke Boost logging!
   * @return The path of the appdata directory that should be used.
   */
  fs::path appdata() {
    static std::once_flag migration_flag;
    static fs::path config_path;

    // Ensure migration is only attempted once
    std::call_once(migration_flag, []() {
      bool found = false;
      bool migrate_config = true;
      const char *dir;
      const char *homedir;
      const char *migrate_envvar;

      // Get the home directory
      if ((homedir = getenv("HOME")) == nullptr || strlen(homedir) == 0) {
        // If HOME is empty or not set, use the current user's home directory
        homedir = getpwuid(geteuid())->pw_dir;
      }

      // May be set if running under a systemd service with the ConfigurationDirectory= option set.
      if ((dir = getenv("CONFIGURATION_DIRECTORY")) != nullptr && strlen(dir) > 0) {
        found = true;
        config_path = fs::path(dir) / "sunshine"sv;
      }
      // Otherwise, follow the XDG base directory specification:
      // https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
      if (!found && (dir = getenv("XDG_CONFIG_HOME")) != nullptr && strlen(dir) > 0) {
        found = true;
        config_path = fs::path(dir) / "sunshine"sv;
      }
      // As a last resort, use the home directory
      if (!found) {
        migrate_config = false;
        config_path = fs::path(homedir) / ".config/sunshine"sv;
      }

      // migrate from the old config location if necessary
      migrate_envvar = getenv("SUNSHINE_MIGRATE_CONFIG");
      if (migrate_config && found && migrate_envvar && strcmp(migrate_envvar, "1") == 0) {
        std::error_code ec;
        fs::path old_config_path = fs::path(homedir) / ".config/sunshine"sv;
        if (old_config_path != config_path && fs::exists(old_config_path, ec)) {
          if (!fs::exists(config_path, ec)) {
            std::cout << "Migrating config from "sv << old_config_path << " to "sv << config_path << std::endl;
            if (!ec) {
              // Create the new directory tree if it doesn't already exist
              fs::create_directories(config_path, ec);
            }
            if (!ec) {
              // Copy the old directory into the new location
              // NB: We use a copy instead of a move so that cross-volume migrations work
              fs::copy(old_config_path, config_path, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
            }
            if (!ec) {
              // If the copy was successful, delete the original directory
              fs::remove_all(old_config_path, ec);
              if (ec) {
                std::cerr << "Failed to clean up old config directory: " << ec.message() << std::endl;

                // This is not fatal. Next time we start, we'll warn the user to delete the old one.
                ec.clear();
              }
            }
            if (ec) {
              std::cerr << "Migration failed: " << ec.message() << std::endl;
              config_path = old_config_path;
            }
          } else {
            // We cannot use Boost logging because it hasn't been initialized yet!
            std::cerr << "Config exists in both "sv << old_config_path << " and "sv << config_path << ". Using "sv << config_path << " for config" << std::endl;
            std::cerr << "It is recommended to remove "sv << old_config_path << std::endl;
          }
        }
      }
    });

    return config_path;
  }

  std::string from_sockaddr(const sockaddr *const ip_addr) {
    char data[INET6_ADDRSTRLEN] = {};

    auto family = ip_addr->sa_family;
    if (family == AF_INET6) {
      inet_ntop(AF_INET6, &((sockaddr_in6 *) ip_addr)->sin6_addr, data, INET6_ADDRSTRLEN);
    } else if (family == AF_INET) {
      inet_ntop(AF_INET, &((sockaddr_in *) ip_addr)->sin_addr, data, INET_ADDRSTRLEN);
    }

    return std::string {data};
  }

  std::pair<std::uint16_t, std::string> from_sockaddr_ex(const sockaddr *const ip_addr) {
    char data[INET6_ADDRSTRLEN] = {};

    auto family = ip_addr->sa_family;
    std::uint16_t port = 0;
    if (family == AF_INET6) {
      inet_ntop(AF_INET6, &((sockaddr_in6 *) ip_addr)->sin6_addr, data, INET6_ADDRSTRLEN);
      port = ((sockaddr_in6 *) ip_addr)->sin6_port;
    } else if (family == AF_INET) {
      inet_ntop(AF_INET, &((sockaddr_in *) ip_addr)->sin_addr, data, INET_ADDRSTRLEN);
      port = ((sockaddr_in *) ip_addr)->sin_port;
    }

    return {port, std::string {data}};
  }

  std::string get_mac_address(const std::string_view &address) {
    auto ifaddrs = get_ifaddrs();

#ifdef __FreeBSD__
    // On FreeBSD, we need to find the interface name first, then look for its AF_LINK entry
    std::string interface_name;
    for (auto pos = ifaddrs.get(); pos != nullptr; pos = pos->ifa_next) {
      if (pos->ifa_addr && address == from_sockaddr(pos->ifa_addr)) {
        interface_name = pos->ifa_name;
        break;
      }
    }

    if (!interface_name.empty()) {
      // Find the AF_LINK entry for this interface to get MAC address
      for (auto pos = ifaddrs.get(); pos != nullptr; pos = pos->ifa_next) {
        if (pos->ifa_addr && pos->ifa_addr->sa_family == AF_LINK &&
            interface_name == pos->ifa_name) {
          auto sdl = (struct sockaddr_dl *) pos->ifa_addr;
          auto mac = (unsigned char *) LLADDR(sdl);

          // Format MAC address as XX:XX:XX:XX:XX:XX
          std::ostringstream mac_stream;
          mac_stream << std::hex << std::setfill('0');
          for (int i = 0; i < sdl->sdl_alen; i++) {
            if (i > 0) {
              mac_stream << ':';
            }
            mac_stream << std::setw(2) << (int) mac[i];
          }
          return mac_stream.str();
        }
      }
    }
#else
    // On Linux, read MAC address from sysfs
    for (auto pos = ifaddrs.get(); pos != nullptr; pos = pos->ifa_next) {
      if (pos->ifa_addr && address == from_sockaddr(pos->ifa_addr)) {
        std::ifstream mac_file("/sys/class/net/"s + pos->ifa_name + "/address");
        if (mac_file.good()) {
          std::string mac_address;
          std::getline(mac_file, mac_address);
          return mac_address;
        }
      }
    }
#endif

    BOOST_LOG(warning) << "Unable to find MAC address for "sv << address;
    return "00:00:00:00:00:00"s;
  }

  bp::child run_command(bool elevated, bool interactive, const std::string &cmd, boost::filesystem::path &working_dir, const bp::environment &env, FILE *file, std::error_code &ec, bp::group *group) {
    // clang-format off
    if (!group) {
      if (!file) {
        return bp::child(cmd, env, bp::start_dir(working_dir), bp::std_in < bp::null, bp::std_out > bp::null, bp::std_err > bp::null, bp::limit_handles, ec);
      }
      else {
        return bp::child(cmd, env, bp::start_dir(working_dir), bp::std_in < bp::null, bp::std_out > file, bp::std_err > file, bp::limit_handles, ec);
      }
    }
    else {
      if (!file) {
        return bp::child(cmd, env, bp::start_dir(working_dir), bp::std_in < bp::null, bp::std_out > bp::null, bp::std_err > bp::null, bp::limit_handles, ec, *group);
      }
      else {
        return bp::child(cmd, env, bp::start_dir(working_dir), bp::std_in < bp::null, bp::std_out > file, bp::std_err > file, bp::limit_handles, ec, *group);
      }
    }
    // clang-format on
  }

  /**
   * @brief Open a url in the default web browser.
   * @param url The url to open.
   */
  void open_url(const std::string &url) {
    // set working dir to user home directory
    auto working_dir = boost::filesystem::path(std::getenv("HOME"));
    std::string cmd = R"(xdg-open ")" + url + R"(")";

    boost::process::v1::environment _env = boost::this_process::environment();
    std::error_code ec;
    auto child = run_command(false, false, cmd, working_dir, _env, nullptr, ec, nullptr);
    if (ec) {
      BOOST_LOG(warning) << "Couldn't open url ["sv << url << "]: System: "sv << ec.message();
    } else {
      BOOST_LOG(info) << "Opened url ["sv << url << "]"sv;
      child.detach();
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

  void restart_on_exit() {
    char executable[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", executable, PATH_MAX - 1);
    if (len == -1) {
      BOOST_LOG(fatal) << "readlink() failed: "sv << errno;
      return;
    }
    executable[len] = '\0';

    // ASIO doesn't use O_CLOEXEC, so we have to close all fds ourselves
    int openmax = (int) sysconf(_SC_OPEN_MAX);
    for (int fd = STDERR_FILENO + 1; fd < openmax; fd++) {
      close(fd);
    }

    // Re-exec ourselves with the same arguments
    if (execv(executable, lifetime::get_argv()) < 0) {
      BOOST_LOG(fatal) << "execv() failed: "sv << errno;
      return;
    }
  }

  void restart() {
    // Gracefully clean up and restart ourselves instead of exiting
    atexit(restart_on_exit);
    lifetime::exit_sunshine(0, true);
  }

  int set_env(const std::string &name, const std::string &value) {
    return setenv(name.c_str(), value.c_str(), 1);
  }

  int unset_env(const std::string &name) {
    return unsetenv(name.c_str());
  }

  bool request_process_group_exit(std::uintptr_t native_handle) {
    if (kill(-((pid_t) native_handle), SIGTERM) == 0 || errno == ESRCH) {
      BOOST_LOG(debug) << "Successfully sent SIGTERM to process group: "sv << native_handle;
      return true;
    } else {
      BOOST_LOG(warning) << "Unable to send SIGTERM to process group ["sv << native_handle << "]: "sv << errno;
      return false;
    }
  }

  bool process_group_running(std::uintptr_t native_handle) {
    return waitpid(-((pid_t) native_handle), nullptr, WNOHANG) >= 0;
  }

  struct sockaddr_in to_sockaddr(boost::asio::ip::address_v4 address, uint16_t port) {
    struct sockaddr_in saddr_v4 = {};

    saddr_v4.sin_family = AF_INET;
    saddr_v4.sin_port = htons(port);

    auto addr_bytes = address.to_bytes();
    memcpy(&saddr_v4.sin_addr, addr_bytes.data(), sizeof(saddr_v4.sin_addr));

    return saddr_v4;
  }

  struct sockaddr_in6 to_sockaddr(boost::asio::ip::address_v6 address, uint16_t port) {
    struct sockaddr_in6 saddr_v6 = {};

    saddr_v6.sin6_family = AF_INET6;
    saddr_v6.sin6_port = htons(port);
    saddr_v6.sin6_scope_id = address.scope_id();

    auto addr_bytes = address.to_bytes();
    memcpy(&saddr_v6.sin6_addr, addr_bytes.data(), sizeof(saddr_v6.sin6_addr));

    return saddr_v6;
  }

  bool send_batch(batched_send_info_t &send_info) {
    auto sockfd = (int) send_info.native_socket;
    struct msghdr msg = {};

    // Convert the target address into a sockaddr
    struct sockaddr_in taddr_v4 = {};
    struct sockaddr_in6 taddr_v6 = {};
    if (send_info.target_address.is_v6()) {
      taddr_v6 = to_sockaddr(send_info.target_address.to_v6(), send_info.target_port);

      msg.msg_name = (struct sockaddr *) &taddr_v6;
      msg.msg_namelen = sizeof(taddr_v6);
    } else {
      taddr_v4 = to_sockaddr(send_info.target_address.to_v4(), send_info.target_port);

      msg.msg_name = (struct sockaddr *) &taddr_v4;
      msg.msg_namelen = sizeof(taddr_v4);
    }

    union {
#ifdef IP_PKTINFO
      char buf[CMSG_SPACE(sizeof(uint16_t)) + std::max(CMSG_SPACE(sizeof(struct in_pktinfo)), CMSG_SPACE(sizeof(struct in6_pktinfo)))];
#elif defined(IP_SENDSRCADDR)
      // FreeBSD uses IP_SENDSRCADDR with struct in_addr instead of IP_PKTINFO with struct in_pktinfo
      char buf[CMSG_SPACE(sizeof(uint16_t)) + std::max(CMSG_SPACE(sizeof(struct in_addr)), CMSG_SPACE(sizeof(struct in6_pktinfo)))];
#endif
      struct cmsghdr alignment;
    } cmbuf = {};  // Must be zeroed for CMSG_NXTHDR()

    socklen_t cmbuflen = 0;

    msg.msg_control = cmbuf.buf;
    msg.msg_controllen = sizeof(cmbuf.buf);

    // The PKTINFO option will always be first, then we will conditionally
    // append the UDP_SEGMENT option next if applicable.
    auto pktinfo_cm = CMSG_FIRSTHDR(&msg);
    if (send_info.source_address.is_v6()) {
      struct in6_pktinfo pktInfo;

      struct sockaddr_in6 saddr_v6 = to_sockaddr(send_info.source_address.to_v6(), 0);
      pktInfo.ipi6_addr = saddr_v6.sin6_addr;
      pktInfo.ipi6_ifindex = 0;

      cmbuflen += CMSG_SPACE(sizeof(pktInfo));

      pktinfo_cm->cmsg_level = IPPROTO_IPV6;
      pktinfo_cm->cmsg_type = IPV6_PKTINFO;
      pktinfo_cm->cmsg_len = CMSG_LEN(sizeof(pktInfo));
      memcpy(CMSG_DATA(pktinfo_cm), &pktInfo, sizeof(pktInfo));
    } else {
#ifdef IP_PKTINFO
      struct in_pktinfo pktInfo;

      struct sockaddr_in saddr_v4 = to_sockaddr(send_info.source_address.to_v4(), 0);
      pktInfo.ipi_spec_dst = saddr_v4.sin_addr;
      pktInfo.ipi_ifindex = 0;

      cmbuflen += CMSG_SPACE(sizeof(pktInfo));

      pktinfo_cm->cmsg_level = IPPROTO_IP;
      pktinfo_cm->cmsg_type = IP_PKTINFO;
      pktinfo_cm->cmsg_len = CMSG_LEN(sizeof(pktInfo));
      memcpy(CMSG_DATA(pktinfo_cm), &pktInfo, sizeof(pktInfo));
#elif defined(IP_SENDSRCADDR)
      // FreeBSD uses IP_SENDSRCADDR with struct in_addr instead of IP_PKTINFO
      struct sockaddr_in saddr_v4 = to_sockaddr(send_info.source_address.to_v4(), 0);
      struct in_addr src_addr = saddr_v4.sin_addr;

      cmbuflen += CMSG_SPACE(sizeof(src_addr));

      pktinfo_cm->cmsg_level = IPPROTO_IP;
      pktinfo_cm->cmsg_type = IP_SENDSRCADDR;
      pktinfo_cm->cmsg_len = CMSG_LEN(sizeof(src_addr));
      memcpy(CMSG_DATA(pktinfo_cm), &src_addr, sizeof(src_addr));
#endif
    }

    auto const max_iovs_per_msg = send_info.payload_buffers.size() + (send_info.headers ? 1 : 0);

#ifdef UDP_SEGMENT
    {
      // UDP GSO on Linux currently only supports sending 64K or 64 segments at a time
      size_t seg_index = 0;
      const size_t seg_max = 65536 / 1500;
      struct iovec iovs[(send_info.headers ? std::min(seg_max, send_info.block_count) : 1) * max_iovs_per_msg];
      auto msg_size = send_info.header_size + send_info.payload_size;
      while (seg_index < send_info.block_count) {
        int iovlen = 0;
        auto segs_in_batch = std::min(send_info.block_count - seg_index, seg_max);
        if (send_info.headers) {
          // Interleave iovs for headers and payloads
          for (auto i = 0; i < segs_in_batch; i++) {
            iovs[iovlen].iov_base = (void *) &send_info.headers[(send_info.block_offset + seg_index + i) * send_info.header_size];
            iovs[iovlen].iov_len = send_info.header_size;
            iovlen++;
            auto payload_desc = send_info.buffer_for_payload_offset((send_info.block_offset + seg_index + i) * send_info.payload_size);
            iovs[iovlen].iov_base = (void *) payload_desc.buffer;
            iovs[iovlen].iov_len = send_info.payload_size;
            iovlen++;
          }
        } else {
          // Translate buffer descriptors into iovs
          auto payload_offset = (send_info.block_offset + seg_index) * send_info.payload_size;
          auto payload_length = payload_offset + (segs_in_batch * send_info.payload_size);
          while (payload_offset < payload_length) {
            auto payload_desc = send_info.buffer_for_payload_offset(payload_offset);
            iovs[iovlen].iov_base = (void *) payload_desc.buffer;
            iovs[iovlen].iov_len = std::min(payload_desc.size, payload_length - payload_offset);
            payload_offset += iovs[iovlen].iov_len;
            iovlen++;
          }
        }

        msg.msg_iov = iovs;
        msg.msg_iovlen = iovlen;

        // We should not use GSO if the data is <= one full block size
        if (segs_in_batch > 1) {
          msg.msg_controllen = cmbuflen + CMSG_SPACE(sizeof(uint16_t));

          // Enable GSO to perform segmentation of our buffer for us
          auto cm = CMSG_NXTHDR(&msg, pktinfo_cm);
          cm->cmsg_level = SOL_UDP;
          cm->cmsg_type = UDP_SEGMENT;
          cm->cmsg_len = CMSG_LEN(sizeof(uint16_t));
          *((uint16_t *) CMSG_DATA(cm)) = msg_size;
        } else {
          msg.msg_controllen = cmbuflen;
        }

        // This will fail if GSO is not available, so we will fall back to non-GSO if
        // it's the first sendmsg() call. On subsequent calls, we will treat errors as
        // actual failures and return to the caller.
        auto bytes_sent = sendmsg(sockfd, &msg, 0);
        if (bytes_sent < 0) {
          // If there's no send buffer space, wait for some to be available
          if (errno == EAGAIN) {
            struct pollfd pfd;

            pfd.fd = sockfd;
            pfd.events = POLLOUT;

            if (poll(&pfd, 1, -1) != 1) {
              BOOST_LOG(warning) << "poll() failed: "sv << errno;
              break;
            }

            // Try to send again
            continue;
          }

          BOOST_LOG(verbose) << "sendmsg() failed: "sv << errno;
          break;
        }

        seg_index += bytes_sent / msg_size;
      }

      // If we sent something, return the status and don't fall back to the non-GSO path.
      if (seg_index != 0) {
        return seg_index >= send_info.block_count;
      }
    }
#endif

    {
      // If GSO is not supported, use sendmmsg() instead.
      std::vector<struct mmsghdr> msgs(send_info.block_count);
      std::vector<struct iovec> iovs(send_info.block_count * (send_info.headers ? 2 : 1));
      int iov_idx = 0;
      for (size_t i = 0; i < send_info.block_count; i++) {
        msgs[i].msg_len = 0;
        msgs[i].msg_hdr.msg_iov = &iovs[iov_idx];
        msgs[i].msg_hdr.msg_iovlen = send_info.headers ? 2 : 1;

        if (send_info.headers) {
          iovs[iov_idx].iov_base = (void *) &send_info.headers[(send_info.block_offset + i) * send_info.header_size];
          iovs[iov_idx].iov_len = send_info.header_size;
          iov_idx++;
        }
        auto payload_desc = send_info.buffer_for_payload_offset((send_info.block_offset + i) * send_info.payload_size);
        iovs[iov_idx].iov_base = (void *) payload_desc.buffer;
        iovs[iov_idx].iov_len = send_info.payload_size;
        iov_idx++;

        msgs[i].msg_hdr.msg_name = msg.msg_name;
        msgs[i].msg_hdr.msg_namelen = msg.msg_namelen;
        msgs[i].msg_hdr.msg_control = cmbuf.buf;
        msgs[i].msg_hdr.msg_controllen = cmbuflen;
        msgs[i].msg_hdr.msg_flags = 0;
      }

      // Call sendmmsg() until all messages are sent
      size_t blocks_sent = 0;
      while (blocks_sent < send_info.block_count) {
        int msgs_sent = sendmmsg(sockfd, &msgs[blocks_sent], send_info.block_count - blocks_sent, 0);
        if (msgs_sent < 0) {
          // If there's no send buffer space, wait for some to be available
          if (errno == EAGAIN) {
            struct pollfd pfd;

            pfd.fd = sockfd;
            pfd.events = POLLOUT;

            if (poll(&pfd, 1, -1) != 1) {
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

  bool send(send_info_t &send_info) {
    auto sockfd = (int) send_info.native_socket;
    struct msghdr msg = {};

    // Convert the target address into a sockaddr
    struct sockaddr_in taddr_v4 = {};
    struct sockaddr_in6 taddr_v6 = {};
    if (send_info.target_address.is_v6()) {
      taddr_v6 = to_sockaddr(send_info.target_address.to_v6(), send_info.target_port);

      msg.msg_name = (struct sockaddr *) &taddr_v6;
      msg.msg_namelen = sizeof(taddr_v6);
    } else {
      taddr_v4 = to_sockaddr(send_info.target_address.to_v4(), send_info.target_port);

      msg.msg_name = (struct sockaddr *) &taddr_v4;
      msg.msg_namelen = sizeof(taddr_v4);
    }

    union {
#ifdef IP_PKTINFO
      char buf[std::max(CMSG_SPACE(sizeof(struct in_pktinfo)), CMSG_SPACE(sizeof(struct in6_pktinfo)))];
#elif defined(IP_SENDSRCADDR)
      // FreeBSD uses IP_SENDSRCADDR with struct in_addr instead of IP_PKTINFO with struct in_pktinfo
      char buf[std::max(CMSG_SPACE(sizeof(struct in_addr)), CMSG_SPACE(sizeof(struct in6_pktinfo)))];
#endif
      struct cmsghdr alignment;
    } cmbuf;

    socklen_t cmbuflen = 0;

    msg.msg_control = cmbuf.buf;
    msg.msg_controllen = sizeof(cmbuf.buf);

    auto pktinfo_cm = CMSG_FIRSTHDR(&msg);
    if (send_info.source_address.is_v6()) {
      struct in6_pktinfo pktInfo;

      struct sockaddr_in6 saddr_v6 = to_sockaddr(send_info.source_address.to_v6(), 0);
      pktInfo.ipi6_addr = saddr_v6.sin6_addr;
      pktInfo.ipi6_ifindex = 0;

      cmbuflen += CMSG_SPACE(sizeof(pktInfo));

      pktinfo_cm->cmsg_level = IPPROTO_IPV6;
      pktinfo_cm->cmsg_type = IPV6_PKTINFO;
      pktinfo_cm->cmsg_len = CMSG_LEN(sizeof(pktInfo));
      memcpy(CMSG_DATA(pktinfo_cm), &pktInfo, sizeof(pktInfo));
    } else {
#ifdef IP_PKTINFO
      struct in_pktinfo pktInfo;

      struct sockaddr_in saddr_v4 = to_sockaddr(send_info.source_address.to_v4(), 0);
      pktInfo.ipi_spec_dst = saddr_v4.sin_addr;
      pktInfo.ipi_ifindex = 0;

      cmbuflen += CMSG_SPACE(sizeof(pktInfo));

      pktinfo_cm->cmsg_level = IPPROTO_IP;
      pktinfo_cm->cmsg_type = IP_PKTINFO;
      pktinfo_cm->cmsg_len = CMSG_LEN(sizeof(pktInfo));
      memcpy(CMSG_DATA(pktinfo_cm), &pktInfo, sizeof(pktInfo));
#elif defined(IP_SENDSRCADDR)
      // FreeBSD uses IP_SENDSRCADDR with struct in_addr instead of IP_PKTINFO
      struct sockaddr_in saddr_v4 = to_sockaddr(send_info.source_address.to_v4(), 0);
      struct in_addr src_addr = saddr_v4.sin_addr;

      cmbuflen += CMSG_SPACE(sizeof(src_addr));

      pktinfo_cm->cmsg_level = IPPROTO_IP;
      pktinfo_cm->cmsg_type = IP_SENDSRCADDR;
      pktinfo_cm->cmsg_len = CMSG_LEN(sizeof(src_addr));
      memcpy(CMSG_DATA(pktinfo_cm), &src_addr, sizeof(src_addr));
#endif
    }

    struct iovec iovs[2];
    int iovlen = 0;
    if (send_info.header) {
      iovs[iovlen].iov_base = (void *) send_info.header;
      iovs[iovlen].iov_len = send_info.header_size;
      iovlen++;
    }
    iovs[iovlen].iov_base = (void *) send_info.payload;
    iovs[iovlen].iov_len = send_info.payload_size;
    iovlen++;

    msg.msg_iov = iovs;
    msg.msg_iovlen = iovlen;

    msg.msg_controllen = cmbuflen;

    auto bytes_sent = sendmsg(sockfd, &msg, 0);

    // If there's no send buffer space, wait for some to be available
    while (bytes_sent < 0 && errno == EAGAIN) {
      struct pollfd pfd;

      pfd.fd = sockfd;
      pfd.events = POLLOUT;

      if (poll(&pfd, 1, -1) != 1) {
        BOOST_LOG(warning) << "poll() failed: "sv << errno;
        break;
      }

      // Try to send again
      bytes_sent = sendmsg(sockfd, &msg, 0);
    }

    if (bytes_sent < 0) {
      BOOST_LOG(warning) << "sendmsg() failed: "sv << errno;
      return false;
    }

    return true;
  }

  // We can't track QoS state separately for each destination on this OS,
  // so we keep a ref count to only disable QoS options when all clients
  // are disconnected.
  static std::atomic<int> qos_ref_count = 0;

  class qos_t: public deinit_t {
  public:
    qos_t(int sockfd, std::vector<std::tuple<int, int, int>> options):
        sockfd(sockfd),
        options(options) {
      qos_ref_count++;
    }

    virtual ~qos_t() {
      if (--qos_ref_count == 0) {
        for (const auto &tuple : options) {
          auto reset_val = std::get<2>(tuple);
          if (setsockopt(sockfd, std::get<0>(tuple), std::get<1>(tuple), &reset_val, sizeof(reset_val)) < 0) {
            BOOST_LOG(warning) << "Failed to reset option: "sv << errno;
          }
        }
      }
    }

  private:
    int sockfd;
    std::vector<std::tuple<int, int, int>> options;
  };

  /**
   * @brief Enables QoS on the given socket for traffic to the specified destination.
   * @param native_socket The native socket handle.
   * @param address The destination address for traffic sent on this socket.
   * @param port The destination port for traffic sent on this socket.
   * @param data_type The type of traffic sent on this socket.
   * @param dscp_tagging Specifies whether to enable DSCP tagging on outgoing traffic.
   */
  std::unique_ptr<deinit_t> enable_socket_qos(uintptr_t native_socket, boost::asio::ip::address &address, uint16_t port, qos_data_type_e data_type, bool dscp_tagging) {
    int sockfd = (int) native_socket;
    std::vector<std::tuple<int, int, int>> reset_options;

    if (dscp_tagging) {
      int level;
      int option;

      // With dual-stack sockets, Linux uses IPV6_TCLASS for IPv6 traffic
      // and IP_TOS for IPv4 traffic.
      if (address.is_v6() && !address.to_v6().is_v4_mapped()) {
        level = SOL_IPV6;
        option = IPV6_TCLASS;
      } else {
        level = SOL_IP;
        option = IP_TOS;
      }

      // The specific DSCP values here are chosen to be consistent with Windows,
      // except that we use CS6 instead of CS7 for audio traffic.
      int dscp = 0;
      switch (data_type) {
        case qos_data_type_e::video:
          dscp = 40;
          break;
        case qos_data_type_e::audio:
          dscp = 48;
          break;
        default:
          BOOST_LOG(error) << "Unknown traffic type: "sv << (int) data_type;
          break;
      }

      if (dscp) {
        // Shift to put the DSCP value in the correct position in the TOS field
        dscp <<= 2;

        if (setsockopt(sockfd, level, option, &dscp, sizeof(dscp)) == 0) {
          // Reset TOS to -1 when QoS is disabled
          reset_options.emplace_back(std::make_tuple(level, option, -1));
        } else {
          BOOST_LOG(error) << "Failed to set TOS/TCLASS: "sv << errno;
        }
      }
    }

    // We can use SO_PRIORITY to set outgoing traffic priority without DSCP tagging.
    //
    // NB: We set this after IP_TOS/IPV6_TCLASS since setting TOS value seems to
    // reset SO_PRIORITY back to 0.
    //
    // 6 is the highest priority that can be used without SYS_CAP_ADMIN.
#ifndef SO_PRIORITY
    // FreeBSD doesn't support SO_PRIORITY, so we skip this
    BOOST_LOG(debug) << "SO_PRIORITY not supported on this platform, skipping traffic priority setting";
#else
    int priority = data_type == qos_data_type_e::audio ? 6 : 5;
    if (setsockopt(sockfd, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority)) == 0) {
      // Reset SO_PRIORITY to 0 when QoS is disabled
      reset_options.emplace_back(std::make_tuple(SOL_SOCKET, SO_PRIORITY, 0));
    } else {
      BOOST_LOG(error) << "Failed to set SO_PRIORITY: "sv << errno;
    }
#endif

    return std::make_unique<qos_t>(sockfd, reset_options);
  }

  std::string get_host_name() {
    try {
      return boost::asio::ip::host_name();
    } catch (boost::system::system_error &err) {
      BOOST_LOG(error) << "Failed to get hostname: "sv << err.what();
      return "Sunshine"s;
    }
  }

  namespace source {
    enum source_e : std::size_t {
#ifdef SUNSHINE_BUILD_CUDA
      NVFBC,  ///< NvFBC
#endif
#ifdef SUNSHINE_BUILD_WAYLAND
      WAYLAND,  ///< Wayland
#endif
#ifdef SUNSHINE_BUILD_DRM
      KMS,  ///< KMS
#endif
#ifdef SUNSHINE_BUILD_X11
      X11,  ///< X11
#endif
      MAX_FLAGS  ///< The maximum number of flags
    };
  }  // namespace source

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
  std::vector<std::string> kms_display_names(mem_type_e hwdevice_type);
  std::shared_ptr<display_t> kms_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config);

  bool verify_kms() {
    return !kms_display_names(mem_type_e::unknown).empty();
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
    if (sources[source::NVFBC] && hwdevice_type == mem_type_e::cuda) {
      return nvfbc_display_names();
    }
#endif
#ifdef SUNSHINE_BUILD_WAYLAND
    if (sources[source::WAYLAND]) {
      return wl_display_names();
    }
#endif
#ifdef SUNSHINE_BUILD_DRM
    if (sources[source::KMS]) {
      return kms_display_names(hwdevice_type);
    }
#endif
#ifdef SUNSHINE_BUILD_X11
    if (sources[source::X11]) {
      return x11_display_names();
    }
#endif
    return {};
  }

  /**
   * @brief Returns if GPUs/drivers have changed since the last call to this function.
   * @return `true` if a change has occurred or if it is unknown whether a change occurred.
   */
  bool needs_encoder_reenumeration() {
    // We don't track GPU state, so we will always reenumerate. Fortunately, it is fast on Linux.
    return true;
  }

  std::shared_ptr<display_t> display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
#ifdef SUNSHINE_BUILD_CUDA
    if (sources[source::NVFBC] && hwdevice_type == mem_type_e::cuda) {
      BOOST_LOG(info) << "Screencasting with NvFBC"sv;
      return nvfbc_display(hwdevice_type, display_name, config);
    }
#endif
#ifdef SUNSHINE_BUILD_WAYLAND
    if (sources[source::WAYLAND]) {
      BOOST_LOG(info) << "Screencasting with Wayland's protocol"sv;
      return wl_display(hwdevice_type, display_name, config);
    }
#endif
#ifdef SUNSHINE_BUILD_DRM
    if (sources[source::KMS]) {
      BOOST_LOG(info) << "Screencasting with KMS"sv;
      return kms_display(hwdevice_type, display_name, config);
    }
#endif
#ifdef SUNSHINE_BUILD_X11
    if (sources[source::X11]) {
      BOOST_LOG(info) << "Screencasting with X11"sv;
      return x11_display(hwdevice_type, display_name, config);
    }
#endif

    return nullptr;
  }

  std::unique_ptr<deinit_t> init() {
    // enable low latency mode for AMD
    // https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/30039
    set_env("AMD_DEBUG", "lowlatencyenc");

    // These are allowed to fail.
    gbm::init();

    window_system = window_system_e::NONE;
#ifdef SUNSHINE_BUILD_WAYLAND
    if (std::getenv("WAYLAND_DISPLAY")) {
      window_system = window_system_e::WAYLAND;
    }
#endif
#if defined(SUNSHINE_BUILD_X11) || defined(SUNSHINE_BUILD_CUDA)
    if (std::getenv("DISPLAY") && window_system != window_system_e::WAYLAND) {
      if (std::getenv("WAYLAND_DISPLAY")) {
        BOOST_LOG(warning) << "Wayland detected, yet sunshine will use X11 for screencasting, screencasting will only work on XWayland applications"sv;
      }

      window_system = window_system_e::X11;
    }
#endif

#ifdef SUNSHINE_BUILD_CUDA
    if ((config::video.capture.empty() && sources.none()) || config::video.capture == "nvfbc") {
      if (verify_nvfbc()) {
        sources[source::NVFBC] = true;
      }
    }
#endif
#ifdef SUNSHINE_BUILD_WAYLAND
    if ((config::video.capture.empty() && sources.none()) || config::video.capture == "wlr") {
      if (verify_wl()) {
        sources[source::WAYLAND] = true;
      }
    }
#endif
#ifdef SUNSHINE_BUILD_DRM
    if ((config::video.capture.empty() && sources.none()) || config::video.capture == "kms") {
      if (verify_kms()) {
        sources[source::KMS] = true;
      }
    }
#endif
#ifdef SUNSHINE_BUILD_X11
    // We enumerate this capture backend regardless of other suitable sources,
    // since it may be needed as a NvFBC fallback for software encoding on X11.
    if (config::video.capture.empty() || config::video.capture == "x11") {
      if (verify_x11()) {
        sources[source::X11] = true;
      }
    }
#endif

    if (sources.none()) {
      BOOST_LOG(error) << "Unable to initialize capture method"sv;
      return nullptr;
    }

    if (!gladLoaderLoadEGL(EGL_NO_DISPLAY) || !eglGetPlatformDisplay) {
      BOOST_LOG(warning) << "Couldn't load EGL library"sv;
    }

    return std::make_unique<deinit_t>();
  }

  class linux_high_precision_timer: public high_precision_timer {
  public:
    void sleep_for(const std::chrono::nanoseconds &duration) override {
      std::this_thread::sleep_for(duration);
    }

    operator bool() override {
      return true;
    }
  };

  std::unique_ptr<high_precision_timer> create_high_precision_timer() {
    return std::make_unique<linux_high_precision_timer>();
  }
}  // namespace platf
