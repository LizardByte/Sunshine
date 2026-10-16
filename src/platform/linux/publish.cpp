/**
 * @file src/platform/linux/publish.cpp
 * @brief Definitions for publishing services on Linux.
 * @note Adapted from https://www.avahi.org/doxygen/html/client-publish-service_8c-example.html
 */
// standard includes
#include <array>
#include <cstddef>
#include <cstring>
#include <ifaddrs.h>
#ifdef __linux__
  // Netlink is a Linux-kernel API: these headers exist only on Linux.
  #include <linux/netlink.h>
  #include <linux/rtnetlink.h>
#endif
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <utility>

// local includes
#include "misc.h"
#include "publish.h"
#include "src/config.h"
#include "src/logging.h"
#include "src/network.h"
#include "src/nvhttp.h"
#include "src/platform/common.h"
#include "src/utility.h"

using namespace std::literals;

namespace avahi {

  /**
   * @brief Error codes used by Avahi.
   * @see https://www.rfc-editor.org/rfc/rfc2136.html#section-2.2
   * @see https://www.iana.org/assignments/dns-parameters/
   */
  enum err_e {
    OK = 0,  ///< OK
    ERR_FAILURE = -1,  ///< Generic error code
    ERR_BAD_STATE = -2,  ///< Object was in a bad state
    ERR_INVALID_HOST_NAME = -3,  ///< Invalid host name
    ERR_INVALID_DOMAIN_NAME = -4,  ///< Invalid domain name
    ERR_NO_NETWORK = -5,  ///< No suitable network protocol available
    ERR_INVALID_TTL = -6,  ///< Invalid DNS TTL
    ERR_IS_PATTERN = -7,  ///< RR key is pattern
    ERR_COLLISION = -8,  ///< Name collision
    ERR_INVALID_RECORD = -9,  ///< Invalid RR

    ERR_INVALID_SERVICE_NAME = -10,  ///< Invalid service name
    ERR_INVALID_SERVICE_TYPE = -11,  ///< Invalid service type
    ERR_INVALID_PORT = -12,  ///< Invalid port number
    ERR_INVALID_KEY = -13,  ///< Invalid key
    ERR_INVALID_ADDRESS = -14,  ///< Invalid address
    ERR_TIMEOUT = -15,  ///< Timeout reached
    ERR_TOO_MANY_CLIENTS = -16,  ///< Too many clients
    ERR_TOO_MANY_OBJECTS = -17,  ///< Too many objects
    ERR_TOO_MANY_ENTRIES = -18,  ///< Too many entries
    ERR_OS = -19,  ///< OS error

    ERR_ACCESS_DENIED = -20,  ///< Access denied
    ERR_INVALID_OPERATION = -21,  ///< Invalid operation
    ERR_DBUS_ERROR = -22,  ///< An unexpected D-Bus error occurred
    ERR_DISCONNECTED = -23,  ///< Daemon connection failed
    ERR_NO_MEMORY = -24,  ///< Memory exhausted
    ERR_INVALID_OBJECT = -25,  ///< The object passed to this function was invalid
    ERR_NO_DAEMON = -26,  ///< Daemon not running
    ERR_INVALID_INTERFACE = -27,  ///< Invalid interface
    ERR_INVALID_PROTOCOL = -28,  ///< Invalid protocol
    ERR_INVALID_FLAGS = -29,  ///< Invalid flags

    ERR_NOT_FOUND = -30,  ///< Not found
    ERR_INVALID_CONFIG = -31,  ///< Configuration error
    ERR_VERSION_MISMATCH = -32,  ///< Version mismatch
    ERR_INVALID_SERVICE_SUBTYPE = -33,  ///< Invalid service subtype
    ERR_INVALID_PACKET = -34,  ///< Invalid packet
    ERR_INVALID_DNS_ERROR = -35,  ///< Invalid DNS return code
    ERR_DNS_FORMERR = -36,  ///< DNS Error: Form error
    ERR_DNS_SERVFAIL = -37,  ///< DNS Error: Server Failure
    ERR_DNS_NXDOMAIN = -38,  ///< DNS Error: No such domain
    ERR_DNS_NOTIMP = -39,  ///< DNS Error: Not implemented

    ERR_DNS_REFUSED = -40,  ///< DNS Error: Operation refused
    ERR_DNS_YXDOMAIN = -41,  ///< DNS Error: A name exists despite a prerequisite requiring its absence
    ERR_DNS_YXRRSET = -42,  ///< DNS Error: A resource record set exists despite a prerequisite requiring its absence
    ERR_DNS_NXRRSET = -43,  ///< DNS Error: A required resource record set is missing or has different records
    ERR_DNS_NOTAUTH = -44,  ///< DNS Error: The server lacks zone authority or TSIG authorization failed
    ERR_DNS_NOTZONE = -45,  ///< DNS Error: An update or prerequisite name lies outside the target DNS zone
    ERR_INVALID_RDATA = -46,  ///< Invalid RDATA
    ERR_INVALID_DNS_CLASS = -47,  ///< Invalid DNS class
    ERR_INVALID_DNS_TYPE = -48,  ///< Invalid DNS type
    ERR_NOT_SUPPORTED = -49,  ///< Not supported

    ERR_NOT_PERMITTED = -50,  ///< Operation not permitted
    ERR_INVALID_ARGUMENT = -51,  ///< Invalid argument
    ERR_IS_EMPTY = -52,  ///< Is empty
    ERR_NO_CHANGE = -53,  ///< The requested operation is invalid because it is redundant

    ERR_MAX = -54  ///< Sentinel below the valid Avahi error-code range; not an error code
  };

  /**
   * @brief Avahi daemon lifecycle states.
   */
  enum ServerState {
    SERVER_INVALID,  ///< Invalid state (initial)
    SERVER_REGISTERING,  ///< Host RRs are being registered
    SERVER_RUNNING,  ///< All host RRs have been established
    SERVER_COLLISION,  ///< There is a collision with a host RR. All host RRs have been withdrawn, the user should set a new host name via avahi_server_set_host_name()
    SERVER_FAILURE  ///< Some fatal failure happened, the server is unable to proceed
  };

  /**
   * @brief Enumerates supported client state options.
   */
  enum ClientState {
    CLIENT_S_REGISTERING = SERVER_REGISTERING,  ///< Server state: REGISTERING
    CLIENT_S_RUNNING = SERVER_RUNNING,  ///< Server state: RUNNING
    CLIENT_S_COLLISION = SERVER_COLLISION,  ///< Server state: COLLISION
    CLIENT_FAILURE = 100,  ///< Some kind of error happened on the client side
    CLIENT_CONNECTING = 101  ///< We're still connecting. This state is only entered when AVAHI_CLIENT_NO_FAIL has been passed to avahi_client_new() and the daemon is not yet available.
  };

  /**
   * @brief Enumerates supported entry group state options.
   */
  enum EntryGroupState {
    ENTRY_GROUP_UNCOMMITED,  ///< The group has not yet been committed, the user must still call avahi_entry_group_commit()
    ENTRY_GROUP_REGISTERING,  ///< The entries of the group are currently being registered
    ENTRY_GROUP_ESTABLISHED,  ///< The entries have successfully been established
    ENTRY_GROUP_COLLISION,  ///< A name collision for one of the entries in the group has been detected, the entries have been withdrawn
    ENTRY_GROUP_FAILURE  ///< Some kind of failure happened, the entries have been withdrawn
  };

  /**
   * @brief Enumerates supported client flags options.
   */
  enum ClientFlags {
    CLIENT_IGNORE_USER_CONFIG = 1,  ///< Don't read user configuration
    CLIENT_NO_FAIL = 2  ///< Don't fail if the daemon is not available when avahi_client_new() is called, instead enter CLIENT_CONNECTING state and wait for the daemon to appear
  };

  struct Poll;
  struct SimplePoll;

  /**
   * @brief Avahi client state-change callback signature.
   */
  using ClientCallback = void (*)(Client *, ClientState, void *userdata);
  /**
   * @brief Avahi entry-group state-change callback signature.
   */
  using EntryGroupCallback = void (*)(EntryGroup *g, EntryGroupState state, void *userdata);

  /**
   * @brief Function pointer used to create an Avahi client.
   */
  using client_new_fn = Client *(*)(const Poll *poll_api, ClientFlags flags, ClientCallback callback, void *userdata, int *error);

  /**
   * @brief Function pointer used to create an Avahi entry group.
   */
  using entry_group_new_fn = EntryGroup *(*)(Client *, EntryGroupCallback, void *userdata);

  /**
   * @brief Function pointer used to duplicate Avahi strings.
   */
  using strdup_fn = char *(*)(const char *);

  /**
   * @brief Function pointer used to run the Avahi simple-poll loop.
   */
  using simple_poll_loop_fn = int (*)(SimplePoll *);
  /**
   * @brief Function pointer used to allocate an Avahi simple-poll instance.
   */
  using simple_poll_new_fn = SimplePoll *(*)();

  client_new_fn client_new;  ///< Client new.
  entry_group_new_fn entry_group_new;  ///< Entry group new.
  strdup_fn strdup;  ///< Strdup.
  simple_poll_loop_fn simple_poll_loop;  ///< Simple poll loop.
  simple_poll_new_fn simple_poll_new;  ///< Simple poll new.

  free_fn free;  ///< Free.
  client_free_fn client_free;  ///< Client free.
  simple_poll_free_fn simple_poll_free;  ///< Simple poll free.
  alternative_service_name_fn alternative_service_name;  ///< Alternative service name.
  entry_group_get_client_fn entry_group_get_client;  ///< Entry group get client.
  entry_group_add_service_fn entry_group_add_service;  ///< Entry group add service.
  entry_group_add_address_fn entry_group_add_address;  ///< Entry group add address.
  entry_group_is_empty_fn entry_group_is_empty;  ///< Entry group is empty.
  entry_group_reset_fn entry_group_reset;  ///< Entry group reset.
  entry_group_commit_fn entry_group_commit;  ///< Entry group commit.
  strerror_fn strerror;  ///< Strerror.
  client_errno_fn client_errno;  ///< Client errno.
  simple_poll_quit_fn simple_poll_quit;  ///< Simple poll quit.
  simple_poll_get_fn simple_poll_get;  ///< Simple poll get.

  /**
   * @brief Event types for Avahi poll watches.
   * @details Layout-compatible with `AvahiWatchEvent` (see
   * avahi-common/watch.h): the values are the `poll(2)` event flags.
   * Scoped enum with an explicit `int` underlying type, so the layout
   * stays identical to the C enum.
   */
  enum class WatchEvent : int {
    WATCH_IN = POLLIN,  ///< Input event
    WATCH_OUT = POLLOUT,  ///< Output event
    WATCH_ERR = POLLERR,  ///< Error event
    WATCH_HUP = POLLHUP  ///< Hangup event
  };

  struct Watch;
  struct Timeout;

  /**
   * @brief Avahi poll-watch callback signature.
   */
  using WatchCallback = void (*)(Watch *w, int fd, WatchEvent event, void *userdata);
  /**
   * @brief Avahi poll-timeout callback signature.
   */
  using TimeoutCallback = void (*)(Timeout *t, void *userdata);

  /**
   * @brief Abstracted event polling API.
   * @details Layout-compatible with `AvahiPoll` (see avahi-common/watch.h):
   * a user-data pointer followed by the watch and timeout operations. Only
   * the watch operations are used here.
   */
  struct Poll {
    void *userdata;  ///< User data for the poll provider.
    Watch *(*watch_new)(const Poll *api, int fd, WatchEvent event, WatchCallback callback, void *userdata);  ///< Create a watch.
    void (*watch_update)(Watch *w, WatchEvent event);  ///< Update a watch's events.
    WatchEvent (*watch_get_events)(Watch *w);  ///< Events that happened on a watch.
    void (*watch_free)(Watch *w);  ///< Free a watch.
    Timeout *(*timeout_new)(const Poll *api, const timeval *tv, TimeoutCallback callback, void *userdata);  ///< Create a timeout.
    void (*timeout_update)(Timeout *t, const timeval *tv);  ///< Update a timeout.
    void (*timeout_free)(Timeout *t);  ///< Free a timeout.
  };

  /**
   * @brief Load Avahi common-library memory, naming, error, and simple-poll functions.
   *
   * @return 0 when common Avahi functions are available; nonzero otherwise.
   */
  int init_common() {
    static dyn::library_t *handle {nullptr};
    static bool funcs_loaded = false;

    if (funcs_loaded) {
      return 0;
    }

    if (!handle) {
      handle = dyn::handle({"libavahi-common.so.3", "libavahi-common.so"});
      if (!handle) {
        return -1;
      }
    }

    if (std::vector<std::tuple<dyn::apiproc *, const char *>> funcs {
          {(dyn::apiproc *) &alternative_service_name, "avahi_alternative_service_name"},
          {(dyn::apiproc *) &free, "avahi_free"},
          {(dyn::apiproc *) &strdup, "avahi_strdup"},
          {(dyn::apiproc *) &strerror, "avahi_strerror"},
          {(dyn::apiproc *) &simple_poll_get, "avahi_simple_poll_get"},
          {(dyn::apiproc *) &simple_poll_loop, "avahi_simple_poll_loop"},
          {(dyn::apiproc *) &simple_poll_quit, "avahi_simple_poll_quit"},
          {(dyn::apiproc *) &simple_poll_new, "avahi_simple_poll_new"},
          {(dyn::apiproc *) &simple_poll_free, "avahi_simple_poll_free"},
        };
        dyn::load(handle, funcs)) {
      return -1;
    }

    funcs_loaded = true;
    return 0;
  }

  /**
   * @brief Load Avahi client and service-publication function pointers.
   *
   * @return 0 when all client functions are available; nonzero otherwise.
   */
  int init_client() {
    if (init_common()) {
      return -1;
    }

    static dyn::library_t *handle {nullptr};
    static bool funcs_loaded = false;

    if (funcs_loaded) {
      return 0;
    }

    if (!handle) {
      handle = dyn::handle({"libavahi-client.so.3", "libavahi-client.so"});
      if (!handle) {
        return -1;
      }
    }

    if (std::vector<std::tuple<dyn::apiproc *, const char *>> funcs {
          {(dyn::apiproc *) &client_new, "avahi_client_new"},
          {(dyn::apiproc *) &client_free, "avahi_client_free"},
          {(dyn::apiproc *) &entry_group_get_client, "avahi_entry_group_get_client"},
          {(dyn::apiproc *) &entry_group_new, "avahi_entry_group_new"},
          {(dyn::apiproc *) &entry_group_add_service, "avahi_entry_group_add_service"},
          {(dyn::apiproc *) &entry_group_add_address, "avahi_entry_group_add_address"},
          {(dyn::apiproc *) &entry_group_is_empty, "avahi_entry_group_is_empty"},
          {(dyn::apiproc *) &entry_group_reset, "avahi_entry_group_reset"},
          {(dyn::apiproc *) &entry_group_commit, "avahi_entry_group_commit"},
          {(dyn::apiproc *) &client_errno, "avahi_client_errno"},
        };
        dyn::load(handle, funcs)) {
      return -1;
    }

    funcs_loaded = true;
    return 0;
  }
}  // namespace avahi

namespace platf::publish {

  avahi::EntryGroup *group = nullptr;  ///< Active Avahi entry group that owns the published service.

  poll_t poll;  ///< Avahi poll loop used while the service is published.
  client_t client;  ///< Avahi client used to register the Sunshine service.

  ptr_t<char> name;  ///< Current service name, updated when Avahi reports a collision.

  unsigned host_name_collision_count = 0;  ///< Suffix counter for regenerating the service host name after collisions.

  std::optional<std::vector<ServedAddress>> published_addresses;  ///< Committed snapshot; nullopt while withdrawn.

#ifdef __linux__
  int netlink_fd = -1;  ///< Netlink socket watched for interface/address changes, -1 when not installed.
  avahi::Watch *netlink_watch = nullptr;  ///< Avahi poll watch for the netlink socket, nullptr when not installed.
#endif

  /* Enumerate the local interface addresses available for publication. */
  std::vector<ServedAddress> enumerate_interface_addresses() {
    ifaddrs *interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) {
      BOOST_LOG(error) << "Failed to enumerate network interfaces."sv;
      return {};
    }
    auto cleanup = util::fail_guard([&interfaces]() {
      freeifaddrs(interfaces);
    });

    std::vector<ServedAddress> addresses;
    for (const auto *ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == nullptr || (ifa->ifa_flags & IFF_UP) == 0) {
        continue;
      }

      // The owning interface index scopes the record to the link it is
      // reachable on; if_nametoindex() only fails for names that vanished
      // mid-enumeration, and those records fall back to IF_UNSPEC.
      avahi::IfIndex interface = avahi::IF_UNSPEC;
      if (const auto index = if_nametoindex(ifa->ifa_name); index != 0) {
        interface = static_cast<avahi::IfIndex>(index);
      }

      if (ifa->ifa_addr->sa_family == AF_INET) {
        const auto *inet_address = reinterpret_cast<const sockaddr_in *>(ifa->ifa_addr);
        boost::asio::ip::address_v4::bytes_type bytes {};
        std::memcpy(bytes.data(), &inet_address->sin_addr, bytes.size());
        addresses.emplace_back(interface, boost::asio::ip::address_v4(bytes));
      } else if (ifa->ifa_addr->sa_family == AF_INET6) {
        const auto *inet6_address = reinterpret_cast<const sockaddr_in6 *>(ifa->ifa_addr);
        boost::asio::ip::address_v6::bytes_type bytes {};
        std::memcpy(bytes.data(), &inet6_address->sin6_addr, bytes.size());
        addresses.emplace_back(interface, boost::asio::ip::address_v6(bytes, inet6_address->sin6_scope_id));
      }
    }
    return addresses;
  }

  /* Determine the addresses published for the service host name. */
  std::vector<ServedAddress> served_addresses(net::af_e effective_family) {
    // An explicit, non-wildcard bind address is, by definition, an address
    // Sunshine serves: publish exactly it, with the interface index of the
    // interface that owns it when one does.
    if (config::sunshine.bind_address.empty()) {
      // A wildcard bind serves every local interface address of the effective family.
      return select_served_addresses(enumerate_interface_addresses(), effective_family);
    }

    boost::system::error_code ec;
    auto bind_address = boost::asio::ip::make_address(config::sunshine.bind_address, ec);
    if (ec) {
      BOOST_LOG(warning) << "Configured bind address is not a valid IP address, falling back to interface enumeration: "sv
                         << config::sunshine.bind_address;
      // An unparsable bind serves every local interface address of the effective family.
      return select_served_addresses(enumerate_interface_addresses(), effective_family);
    }

    bind_address = net::normalize_address(bind_address);
    if (bind_address.is_unspecified()) {
      // A wildcard bind serves every local interface address of the effective family.
      return select_served_addresses(enumerate_interface_addresses(), effective_family);
    }

    if (effective_family == net::IPV4 && !bind_address.is_v4()) {
      return {};
    }

    const auto enumerated = enumerate_interface_addresses();
    const auto owner = std::find_if(
      enumerated.begin(),
      enumerated.end(),
      [&bind_address](const ServedAddress &entry) {
        return entry.address == bind_address;
      }
    );
    const auto interface = owner != enumerated.end() ? owner->interface : avahi::IF_UNSPEC;
    return {ServedAddress {interface, bind_address}};
  }

  /* Add address records for the service host name to the entry group. */
  int add_service_addresses(
    avahi::EntryGroup *entry_group,
    const std::string &service_host,
    const std::vector<ServedAddress> &addresses
  ) {
    using enum avahi::proto;

    for (const auto &address : addresses) {
      const auto avahi_address = to_avahi_address(address.address);
      const int ret = avahi::entry_group_add_address(
        entry_group,
        address.interface,
        std::to_underlying(PROTO_UNSPEC),
        avahi::PUBLISH_NO_REVERSE,
        service_host.c_str(),
        &avahi_address
      );
      if (ret < 0) {
        BOOST_LOG(error) << "Failed to add avahi address for "sv << service_host << ": "sv << avahi::strerror(ret);
        return ret;
      }
    }
    return 0;
  }

  /**
   * @brief React to Avahi entry-group state changes.
   *
   * @param g Entry group that emitted the state change.
   * @param state Avahi entry-group state reported by the callback.
   */
  void entry_group_callback(avahi::EntryGroup *g, avahi::EntryGroupState state, void *) {  // NOSONAR(cpp:S5008): Avahi callbacks require a void pointer for user data.
    group = g;

    switch (state) {
      case avahi::ENTRY_GROUP_ESTABLISHED:
        BOOST_LOG(info) << "Avahi service " << name.get() << " successfully established.";
        break;
      case avahi::ENTRY_GROUP_COLLISION:
        name.reset(avahi::alternative_service_name(name.get()));

        BOOST_LOG(info) << "Avahi service name collision, renaming service to " << name.get();

        // The collision may have been on the service host name rather than
        // the service name: regenerate the host name as well so the retry
        // does not collide on the same host name again.
        ++host_name_collision_count;

        create_services(avahi::entry_group_get_client(g));
        break;
      case avahi::ENTRY_GROUP_FAILURE:
        BOOST_LOG(error) << "Avahi entry group failure: " << avahi::strerror(avahi::client_errno(avahi::entry_group_get_client(g)));
        avahi::simple_poll_quit(poll.get());
        break;
      case avahi::ENTRY_GROUP_UNCOMMITED:
      case avahi::ENTRY_GROUP_REGISTERING:;
    }
  }

  /* Publish Sunshine's mDNS service through Avahi. */
  void create_services(avahi::Client *c, bool quit_on_failure) {
    using enum avahi::proto;

    int ret;

    auto fg = util::fail_guard([quit_on_failure]() {
      if (quit_on_failure) {
        avahi::simple_poll_quit(poll.get());
      }
    });

    if (!group) {
      if (!(group = avahi::entry_group_new(c, entry_group_callback, nullptr))) {
        BOOST_LOG(error) << "avahi::entry_group_new() failed: "sv << avahi::strerror(avahi::client_errno(c));
        return;
      }
    }

    if (avahi::entry_group_is_empty(group)) {
      // A fresh registration attempt starts from a withdrawn snapshot: the
      // group is empty, so no previously committed snapshot is still live.
      published_addresses.reset();
      BOOST_LOG(info) << "Adding avahi service "sv << name.get();

      // Discovery transport stays on both protocols: the transport only
      // decides which multicast groups carry the announcement, it does not
      // filter the advertised host addresses (see #5860). The filter is the
      // service-specific SRV target below: the daemon publishes address
      // records for that host name only from the explicit records added
      // here, so an IPv4-only configuration advertises no AAAA record even
      // though the daemon's shared host name still carries both A and AAAA
      // (publish-aaaa-on-ipv4). The effective family accounts for
      // bind_address, matching the listener setup. On non-Linux platforms
      // there is no address-change watch, so the service instead uses the
      // daemon-managed host name (a null SRV target) and the daemon
      // maintains the address records.
      const auto effective_family = net::get_effective_address_family(
        net::af_from_enum_string(config::sunshine.address_family)
      );
      const auto addresses = served_addresses(effective_family);

      // The service-specific host name and its fixed address records are
      // Linux-only: only Linux installs the address-change watch that
      // refreshes them (see install_address_change_watch()).
      const char *service_target = nullptr;
#ifdef __linux__
      const auto service_host = service_host_name(platf::get_host_name(), host_name_collision_count);
      if (addresses.empty()) {
        BOOST_LOG(warning) << "No served addresses determined for "sv << service_host
                           << "; falling back to the daemon host name, which may advertise unserved addresses (see #5860).";
      } else {
        service_target = service_host.c_str();
        ret = add_service_addresses(group, service_host, addresses);
        if (ret < 0) {
          published_addresses.reset();
          avahi::entry_group_reset(group);
          return;
        }
      }

#endif

      ret = avahi::entry_group_add_service(
        group,
        avahi::IF_UNSPEC,
        std::to_underlying(PROTO_UNSPEC),
        avahi::PublishFlags(0),
        name.get(),
        platf::SERVICE_TYPE,
        nullptr,
        service_target,
        net::map_port(nvhttp::PORT_HTTP),
        nullptr
      );

      if (ret < 0) {
        if (ret == avahi::ERR_COLLISION) {
          // A service name collision with a local service happened. Let's pick a new name
          name.reset(avahi::alternative_service_name(name.get()));
          BOOST_LOG(info) << "Service name collision, renaming service to "sv << name.get();

          // The collision may have been on the service host name instead:
          // regenerate it as well so the retry does not collide again.
          ++host_name_collision_count;

          published_addresses.reset();
          avahi::entry_group_reset(group);

          create_services(c, quit_on_failure);

          fg.disable();
          return;
        }

        BOOST_LOG(error) << "Failed to add "sv << platf::SERVICE_TYPE << " service: "sv << avahi::strerror(ret);
        published_addresses.reset();
        avahi::entry_group_reset(group);
        return;
      }

      ret = avahi::entry_group_commit(group);
      if (ret < 0) {
        BOOST_LOG(error) << "Failed to commit entry group: "sv << avahi::strerror(ret);
        return;
      }

      published_addresses = addresses;
    }

    fg.disable();
  }

  /* Rebuild the service records after local interface addresses change. */
  void refresh_service_addresses() {
    if (!client || !group) {
      return;
    }

    const auto effective_family = net::get_effective_address_family(
      net::af_from_enum_string(config::sunshine.address_family)
    );
    const auto current = served_addresses(effective_family);
    if (published_addresses && current == *published_addresses) {
      return;
    }

    BOOST_LOG(info) << "Local interface addresses changed, republishing Avahi service."sv;
    published_addresses.reset();
    avahi::entry_group_reset(group);
    // A refresh failure must not kill discovery: the group is already reset
    // (records withdrawn), and the next change notification retries, while
    // quitting the poll loop here would leave the service withdrawn with no
    // recovery short of restarting Sunshine.
    create_services(client.get(), false);
  }

#ifdef __linux__
  /**
   * @brief Refresh service publication after an interface/address change.
   *
   * @param fd Netlink route socket to drain before refreshing the addresses.
   */
  void address_change_callback(avahi::Watch * /*watch*/, int fd, avahi::WatchEvent /*event*/, void * /*userdata*/) {
    // Drain the netlink socket: a burst of address/link messages must not
    // leave the fd readable, or the poll loop would spin on it.
    std::array<std::byte, 4096> buffer {};
    while (recv(fd, buffer.data(), buffer.size(), MSG_DONTWAIT) > 0) {
      // Discard: the snapshot is re-read from the interfaces, not from the message.
    }

    refresh_service_addresses();
  }
#endif

  /* Subscribe to interface/address changes on the Avahi poll thread. */
  bool install_address_change_watch() {
#ifdef __linux__
    if (netlink_watch) {
      return true;
    }

    // No existing network-change notification exists in Sunshine to reuse:
    // the only related code is UPnP's WAN-IP renewal, which is unrelated to
    // mDNS. A netlink route socket is the kernel's address/link change
    // feed; registering it as an Avahi poll watch dispatches the callback
    // on the existing poll thread, so no new thread and no cross-thread
    // Avahi calls are introduced. Failure degrades to the old behavior
    // (no refresh) instead of failing startup.
    const int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
    if (fd < 0) {
      BOOST_LOG(warning) << "Failed to open netlink socket for address-change notifications, mDNS records will not refresh on network changes."sv;
      return false;
    }

    sockaddr_nl address {};
    address.nl_family = AF_NETLINK;
    address.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;
    if (bind(fd, (sockaddr *) &address, sizeof(address)) < 0) {
      BOOST_LOG(warning) << "Failed to bind netlink socket for address-change notifications, mDNS records will not refresh on network changes."sv;
      close(fd);
      return false;
    }

    const auto *poll_api = avahi::simple_poll_get(poll.get());
    if (!poll_api) {
      BOOST_LOG(warning) << "Failed to get Avahi poll API for address-change notifications, mDNS records will not refresh on network changes."sv;
      close(fd);
      return false;
    }

    auto *watch = poll_api->watch_new(poll_api, fd, avahi::WatchEvent::WATCH_IN, address_change_callback, nullptr);
    if (!watch) {
      BOOST_LOG(warning) << "Failed to install Avahi watch for address-change notifications, mDNS records will not refresh on network changes."sv;
      close(fd);
      return false;
    }

    netlink_fd = fd;
    netlink_watch = watch;
    return true;
#else
    // Netlink is a Linux-kernel API, so no address-change watch exists on
    // other platforms. That is safe because create_services() publishes
    // the service with a null SRV target there, selecting the Avahi
    // daemon-managed host name: the daemon owns the interface address
    // records and updates and withdraws them when addresses change, so
    // discovery stays correct without a refresh.
    return false;
#endif
  }

  /**
   * @brief React to Avahi client state changes and register services when ready.
   *
   * @param c Avahi client that emitted the state change.
   * @param state Avahi client state reported by the callback.
   */
  void client_callback(avahi::Client *c, avahi::ClientState state, void *) {  // NOSONAR(cpp:S5008): Avahi callbacks require a void pointer for user data.
    switch (state) {
      case avahi::CLIENT_S_RUNNING:
        create_services(c);
        break;
      case avahi::CLIENT_FAILURE:
        BOOST_LOG(error) << "Client failure: "sv << avahi::strerror(avahi::client_errno(c));
        avahi::simple_poll_quit(poll.get());
        break;
      case avahi::CLIENT_S_COLLISION:
      case avahi::CLIENT_S_REGISTERING:
        if (group) {
          // The daemon withdraws the entries on collision/re-registration,
          // so the committed snapshot is no longer live.
          published_addresses.reset();
          avahi::entry_group_reset(group);
        }
        break;
      case avahi::CLIENT_CONNECTING:;
    }
  }

  /**
   * @brief RAII helper that runs shutdown cleanup when destroyed.
   */
  class deinit_t: public ::platf::deinit_t {
  public:
    std::jthread poll_thread;  ///< Poll thread.

    /**
     * @brief Explicitly store the Avahi polling thread for shutdown on destruction.
     *
     * @param poll_thread Poll thread.
     */
    explicit deinit_t(std::jthread poll_thread):
        poll_thread {std::move(poll_thread)} {
    }

    /**
     * @brief Destroy the Avahi publisher deinitializer.
     */
    ~deinit_t() override {
      if (avahi::simple_poll_quit && poll) {
        avahi::simple_poll_quit(poll.get());
      }

      if (poll_thread.joinable()) {
        poll_thread.join();
      }

      // The poll thread has exited, so no watch callback can be in flight:
      // release the address-change watch and its socket now.
#ifdef __linux__
      if (netlink_watch) {
        if (const auto *poll_api = avahi::simple_poll_get(poll.get()); poll_api) {
          poll_api->watch_free(netlink_watch);
        }
        netlink_watch = nullptr;
      }
      if (netlink_fd >= 0) {
        close(netlink_fd);
        netlink_fd = -1;
      }
#endif

      poll.reset();
    }
  };

  [[nodiscard]] std::unique_ptr<::platf::deinit_t> start() {
    if (avahi::init_client()) {
      return nullptr;
    }

    platf::set_thread_name("publish::avahi");

    int avhi_error;

    poll.reset(avahi::simple_poll_new());
    if (!poll) {
      BOOST_LOG(error) << "Failed to create simple poll object."sv;
      return nullptr;
    }

    auto instance_name = net::mdns_instance_name(platf::get_host_name());
    name.reset(avahi::strdup(instance_name.c_str()));

    client.reset(
      avahi::client_new(avahi::simple_poll_get(poll.get()), avahi::ClientFlags(0), client_callback, nullptr, &avhi_error)
    );

    if (!client) {
      BOOST_LOG(error) << "Failed to create client: "sv << avahi::strerror(avhi_error);
      return nullptr;
    }

    // Best-effort: without the watch, discovery still works. On Linux the
    // records go stale after a network change (the pre-watch behavior);
    // on other platforms the daemon-managed host name delegates address
    // maintenance to the Avahi daemon, so no refresh is needed.
    install_address_change_watch();

    return std::make_unique<deinit_t>(std::jthread {[]() {
      avahi::simple_poll_loop(poll.get());

      // simple_poll_loop() returns only once publishing has stopped for good: either at
      // shutdown, or because a failure path called simple_poll_quit(). Nothing services the
      // client's D-Bus connection past this point, so release it here rather than leave it
      // open and subscribed until the process exits. The entry group is owned by the client,
      // so clear it too, otherwise a later start() would reuse a dangling pointer.
      client.reset();
      group = nullptr;
    }});
  }
}  // namespace platf::publish
