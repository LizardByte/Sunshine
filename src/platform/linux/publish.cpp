/**
 * @file src/platform/linux/publish.cpp
 * @brief Definitions for publishing services on Linux.
 * @note Adapted from https://www.avahi.org/doxygen/html/client-publish-service_8c-example.html
 */
// standard includes
#include <thread>

// local includes
#include "misc.h"
#include "src/logging.h"
#include "src/network.h"
#include "src/nvhttp.h"
#include "src/platform/common.h"
#include "src/utility.h"

using namespace std::literals;

namespace avahi {

  /**
   * @brief Error codes used by avahi.
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
    ERR_DNS_YXDOMAIN = -41,  ///< TODO
    ERR_DNS_YXRRSET = -42,  ///< TODO
    ERR_DNS_NXRRSET = -43,  ///< TODO
    ERR_DNS_NOTAUTH = -44,  ///< DNS Error: Not authorized
    ERR_DNS_NOTZONE = -45,  ///< TODO
    ERR_INVALID_RDATA = -46,  ///< Invalid RDATA
    ERR_INVALID_DNS_CLASS = -47,  ///< Invalid DNS class
    ERR_INVALID_DNS_TYPE = -48,  ///< Invalid DNS type
    ERR_NOT_SUPPORTED = -49,  ///< Not supported

    ERR_NOT_PERMITTED = -50,  ///< Operation not permitted
    ERR_INVALID_ARGUMENT = -51,  ///< Invalid argument
    ERR_IS_EMPTY = -52,  ///< Is empty
    ERR_NO_CHANGE = -53,  ///< The requested operation is invalid because it is redundant

    ERR_MAX = -54  ///< TODO
  };

  constexpr auto IF_UNSPEC = -1;  ///< Protocol or platform constant for if unspec.

  /**
   * @brief Enumerates supported proto options.
   */
  enum proto {
    PROTO_INET = 0,  ///< IPv4
    PROTO_INET6 = 1,  ///< IPv6
    PROTO_UNSPEC = -1  ///< Unspecified/all protocol(s)
  };

  /**
   * @brief Enumerates supported server state options.
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

  /**
   * @brief Flags for publishing functions.
   */
  enum PublishFlags {
    PUBLISH_UNIQUE = 1,  ///< For raw records: The RRset is intended to be unique
    PUBLISH_NO_PROBE = 2,  ///< For raw records: Though the RRset is intended to be unique no probes shall be sent
    PUBLISH_NO_ANNOUNCE = 4,  ///< For raw records: Do not announce this RR to other hosts
    PUBLISH_ALLOW_MULTIPLE = 8,  ///< For raw records: Allow multiple local records of this type, even if they are intended to be unique
    PUBLISH_NO_REVERSE = 16,  ///< For address records: don't create a reverse (PTR) entry
    PUBLISH_NO_COOKIE = 32,  ///< For service records: do not implicitly add the local service cookie to TXT data
    PUBLISH_UPDATE = 64,  ///< Update existing records instead of adding new ones
    PUBLISH_USE_WIDE_AREA = 128,  ///< Register the record using wide area DNS (i.e. unicast DNS update)
    PUBLISH_USE_MULTICAST = 256  ///< Register the record using multicast DNS
  };

  /**
   * @brief Network interface index type used by Avahi.
   */
  using IfIndex = int;
  /**
   * @brief Avahi protocol/address-family selector type.
   */
  using Protocol = int;

  struct EntryGroup;
  struct Poll;
  struct SimplePoll;
  struct Client;

  /**
   * @brief Avahi client state-change callback signature.
   */
  typedef void (*ClientCallback)(Client *, ClientState, void *userdata);
  /**
   * @brief Avahi entry-group state-change callback signature.
   */
  typedef void (*EntryGroupCallback)(EntryGroup *g, EntryGroupState state, void *userdata);

  /**
   * @brief Function pointer used to free Avahi-allocated memory.
   */
  typedef void (*free_fn)(void *);

  /**
   * @brief Function pointer used to create an Avahi client.
   */
  typedef Client *(*client_new_fn)(const Poll *poll_api, ClientFlags flags, ClientCallback callback, void *userdata, int *error);
  /**
   * @brief Function pointer used to destroy an Avahi client.
   */
  typedef void (*client_free_fn)(Client *);
  /**
   * @brief Function pointer used to request an alternative Avahi service name.
   */
  typedef char *(*alternative_service_name_fn)(char *);

  /**
   * @brief Function pointer used to get an Avahi client from an entry group.
   */
  typedef Client *(*entry_group_get_client_fn)(EntryGroup *);

  /**
   * @brief Function pointer used to create an Avahi entry group.
   */
  typedef EntryGroup *(*entry_group_new_fn)(Client *, EntryGroupCallback, void *userdata);
  /**
   * @brief Function pointer used to add a service to an Avahi entry group.
   */
  typedef int (*entry_group_add_service_fn)(
    EntryGroup *group,
    IfIndex interface,
    Protocol protocol,
    PublishFlags flags,
    const char *name,
    const char *type,
    const char *domain,
    const char *host,
    uint16_t port,
    ...
  );

  /**
   * @brief Function pointer used to test whether an Avahi entry group is empty.
   */
  typedef int (*entry_group_is_empty_fn)(EntryGroup *);
  /**
   * @brief Function pointer used to reset an Avahi entry group.
   */
  typedef int (*entry_group_reset_fn)(EntryGroup *);
  /**
   * @brief Function pointer used to publish an Avahi entry group.
   */
  typedef int (*entry_group_commit_fn)(EntryGroup *);

  /**
   * @brief Function pointer used to duplicate Avahi strings.
   */
  typedef char *(*strdup_fn)(const char *);
  /**
   * @brief Function pointer used to format Avahi error codes.
   */
  typedef char *(*strerror_fn)(int);
  /**
   * @brief Function pointer used to read the last Avahi client error.
   */
  typedef int (*client_errno_fn)(Client *);

  /**
   * @brief Function pointer used to get the Avahi simple-poll API.
   */
  typedef Poll *(*simple_poll_get_fn)(SimplePoll *);
  /**
   * @brief Function pointer used to run the Avahi simple-poll loop.
   */
  typedef int (*simple_poll_loop_fn)(SimplePoll *);
  /**
   * @brief Function pointer used to stop the Avahi simple-poll loop.
   */
  typedef void (*simple_poll_quit_fn)(SimplePoll *);
  /**
   * @brief Function pointer used to allocate an Avahi simple-poll instance.
   */
  typedef SimplePoll *(*simple_poll_new_fn)();
  /**
   * @brief Function pointer used to free an Avahi simple-poll instance.
   */
  typedef void (*simple_poll_free_fn)(SimplePoll *);

  free_fn free;  ///< Free.
  client_new_fn client_new;  ///< Client new.
  client_free_fn client_free;  ///< Client free.
  alternative_service_name_fn alternative_service_name;  ///< Alternative service name.
  entry_group_get_client_fn entry_group_get_client;  ///< Entry group get client.
  entry_group_new_fn entry_group_new;  ///< Entry group new.
  entry_group_add_service_fn entry_group_add_service;  ///< Entry group add service.
  entry_group_is_empty_fn entry_group_is_empty;  ///< Entry group is empty.
  entry_group_reset_fn entry_group_reset;  ///< Entry group reset.
  entry_group_commit_fn entry_group_commit;  ///< Entry group commit.
  strdup_fn strdup;  ///< Strdup.
  strerror_fn strerror;  ///< Strerror.
  client_errno_fn client_errno;  ///< Client errno.
  simple_poll_get_fn simple_poll_get;  ///< Simple poll get.
  simple_poll_loop_fn simple_poll_loop;  ///< Simple poll loop.
  simple_poll_quit_fn simple_poll_quit;  ///< Simple poll quit.
  simple_poll_new_fn simple_poll_new;  ///< Simple poll new.
  simple_poll_free_fn simple_poll_free;  ///< Simple poll free.

  /**
   * @brief Load common Avahi client and entry-group function pointers.
   *
   * @return 0 when common Avahi functions are available; nonzero otherwise.
   */
  int init_common() {
    static void *handle {nullptr};
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

    std::vector<std::tuple<dyn::apiproc *, const char *>> funcs {
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

    if (dyn::load(handle, funcs)) {
      return -1;
    }

    funcs_loaded = true;
    return 0;
  }

  /**
   * @brief Load Avahi threaded-poll and client function pointers.
   *
   * @return 0 when all client functions are available; nonzero otherwise.
   */
  int init_client() {
    if (init_common()) {
      return -1;
    }

    static void *handle {nullptr};
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

    std::vector<std::tuple<dyn::apiproc *, const char *>> funcs {
      {(dyn::apiproc *) &client_new, "avahi_client_new"},
      {(dyn::apiproc *) &client_free, "avahi_client_free"},
      {(dyn::apiproc *) &entry_group_get_client, "avahi_entry_group_get_client"},
      {(dyn::apiproc *) &entry_group_new, "avahi_entry_group_new"},
      {(dyn::apiproc *) &entry_group_add_service, "avahi_entry_group_add_service"},
      {(dyn::apiproc *) &entry_group_is_empty, "avahi_entry_group_is_empty"},
      {(dyn::apiproc *) &entry_group_reset, "avahi_entry_group_reset"},
      {(dyn::apiproc *) &entry_group_commit, "avahi_entry_group_commit"},
      {(dyn::apiproc *) &client_errno, "avahi_client_errno"},
    };

    if (dyn::load(handle, funcs)) {
      return -1;
    }

    funcs_loaded = true;
    return 0;
  }
}  // namespace avahi

namespace platf::publish {

  /**
   * @brief Release memory allocated by Avahi.
   *
   * @param p Avahi-allocated pointer to release.
   */
  template<class T>
  void free(T *p) {
    avahi::free(p);
  }

  /**
   * @brief Owning pointer for Avahi allocations released with `avahi_free`.
   */
  template<class T>
  using ptr_t = util::safe_ptr<T, free<T>>;
  /**
   * @brief Owning pointer for an Avahi client.
   */
  using client_t = util::dyn_safe_ptr<avahi::Client, &avahi::client_free>;
  /**
   * @brief Owning pointer for an Avahi simple poll loop.
   */
  using poll_t = util::dyn_safe_ptr<avahi::SimplePoll, &avahi::simple_poll_free>;

  avahi::EntryGroup *group = nullptr;  ///< Active Avahi entry group that owns the published service.

  poll_t poll;  ///< Avahi poll loop used while the service is published.
  client_t client;  ///< Avahi client used to register the Sunshine service.

  ptr_t<char> name;  ///< Current service name, updated when Avahi reports a collision.

  /**
   * @brief Create or update the Avahi service entry group.
   *
   * @param c Avahi client used to allocate and commit the service entry group.
   */
  void create_services(avahi::Client *c);

  /**
   * @brief React to Avahi entry-group state changes.
   *
   * @param g Entry group that emitted the state change.
   * @param state Avahi entry-group state reported by the callback.
   */
  void entry_group_callback(avahi::EntryGroup *g, avahi::EntryGroupState state, void *) {
    group = g;

    switch (state) {
      case avahi::ENTRY_GROUP_ESTABLISHED:
        BOOST_LOG(info) << "Avahi service " << name.get() << " successfully established.";
        break;
      case avahi::ENTRY_GROUP_COLLISION:
        name.reset(avahi::alternative_service_name(name.get()));

        BOOST_LOG(info) << "Avahi service name collision, renaming service to " << name.get();

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

  /**
   * @brief Publish Sunshine's mDNS service through Avahi.
   */
  void create_services(avahi::Client *c) {
    int ret;

    auto fg = util::fail_guard([]() {
      avahi::simple_poll_quit(poll.get());
    });

    if (!group) {
      if (!(group = avahi::entry_group_new(c, entry_group_callback, nullptr))) {
        BOOST_LOG(error) << "avahi::entry_group_new() failed: "sv << avahi::strerror(avahi::client_errno(c));
        return;
      }
    }

    if (avahi::entry_group_is_empty(group)) {
      BOOST_LOG(info) << "Adding avahi service "sv << name.get();

      ret = avahi::entry_group_add_service(
        group,
        avahi::IF_UNSPEC,
        avahi::PROTO_UNSPEC,
        avahi::PublishFlags(0),
        name.get(),
        platf::SERVICE_TYPE,
        nullptr,
        nullptr,
        net::map_port(nvhttp::PORT_HTTP),
        nullptr
      );

      if (ret < 0) {
        if (ret == avahi::ERR_COLLISION) {
          // A service name collision with a local service happened. Let's pick a new name
          name.reset(avahi::alternative_service_name(name.get()));
          BOOST_LOG(info) << "Service name collision, renaming service to "sv << name.get();

          avahi::entry_group_reset(group);

          create_services(c);

          fg.disable();
          return;
        }

        BOOST_LOG(error) << "Failed to add "sv << platf::SERVICE_TYPE << " service: "sv << avahi::strerror(ret);
        return;
      }

      ret = avahi::entry_group_commit(group);
      if (ret < 0) {
        BOOST_LOG(error) << "Failed to commit entry group: "sv << avahi::strerror(ret);
        return;
      }
    }

    fg.disable();
  }

  /**
   * @brief React to Avahi client state changes and register services when ready.
   *
   * @param c Avahi client that emitted the state change.
   * @param state Avahi client state reported by the callback.
   */
  void client_callback(avahi::Client *c, avahi::ClientState state, void *) {
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
     * @brief Store the Avahi polling thread for shutdown on destruction.
     *
     * @param poll_thread Poll thread.
     */
    deinit_t(std::jthread poll_thread):
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

    return std::make_unique<deinit_t>(std::jthread {avahi::simple_poll_loop, poll.get()});
  }
}  // namespace platf::publish
