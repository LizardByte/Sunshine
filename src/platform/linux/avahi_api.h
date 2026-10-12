/**
 * @file src/platform/linux/avahi_api.h
 * @brief Avahi protocol types and immutable dynamic-library ownership.
 * @note Adapted from https://www.avahi.org/doxygen/html/client-publish-service_8c-example.html
 */
#pragma once

// standard includes
#include <cstdint>

// local includes
#include "dynamic_library.h"

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
   * @brief Callback signature used for native Avahi client state notifications.
   */
  using ClientCallback = void (*)(Client *, ClientState, void *userdata);
  /**
   * @brief Callback signature used for native Avahi entry-group state notifications.
   */
  using EntryGroupCallback = void (*)(EntryGroup *g, EntryGroupState state, void *userdata);

  /**
   * @brief Function signature used to release Avahi-owned allocations.
   */
  using free_fn = void (*)(void *);

  /**
   * @brief Function pointer used to create an Avahi client.
   */
  using client_new_fn = Client *(*)(const Poll *poll_api, ClientFlags flags, ClientCallback callback, void *userdata, int *error);
  /**
   * @brief Function pointer used to destroy an Avahi client.
   */
  using client_free_fn = void (*)(Client *);
  /**
   * @brief Function pointer used to request an alternative Avahi service name.
   */
  using alternative_service_name_fn = char *(*)(char *);

  /**
   * @brief Function pointer used to get an Avahi client from an entry group.
   */
  using entry_group_get_client_fn = Client *(*)(EntryGroup *);

  /**
   * @brief Function pointer used to create an Avahi entry group.
   */
  using entry_group_new_fn = EntryGroup *(*)(Client *, EntryGroupCallback, void *userdata);
  /**
   * @brief Function pointer used to add a service to an Avahi entry group.
   */
  using entry_group_add_service_fn = int (*)(
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
  using entry_group_is_empty_fn = int (*)(EntryGroup *);
  /**
   * @brief Function pointer used to reset an Avahi entry group.
   */
  using entry_group_reset_fn = int (*)(EntryGroup *);
  /**
   * @brief Function pointer used to publish an Avahi entry group.
   */
  using entry_group_commit_fn = int (*)(EntryGroup *);

  /**
   * @brief Function pointer used to duplicate Avahi strings.
   */
  using strdup_fn = char *(*)(const char *);
  /**
   * @brief Function pointer used to format Avahi error codes.
   */
  using strerror_fn = char *(*)(int);
  /**
   * @brief Function pointer used to read the last Avahi client error.
   */
  using client_errno_fn = int (*)(Client *);

  /**
   * @brief Function pointer used to get the Avahi simple-poll API.
   */
  using simple_poll_get_fn = Poll *(*)(SimplePoll *);
  /**
   * @brief Function pointer used to run the Avahi simple-poll loop.
   */
  using simple_poll_loop_fn = int (*)(SimplePoll *);
  /**
   * @brief Function pointer used to stop the Avahi simple-poll loop.
   */
  using simple_poll_quit_fn = void (*)(SimplePoll *);
  /**
   * @brief Function pointer used to allocate an Avahi simple-poll instance.
   */
  using simple_poll_new_fn = SimplePoll *(*)();
  /**
   * @brief Function pointer used to free an Avahi simple-poll instance.
   */
  using simple_poll_free_fn = void (*)(SimplePoll *);

  /**
   * @brief Validated Avahi functions and the libraries that own their code and data.
   */
  class api_t {
  public:
    /**
     * @brief Load the common and client libraries without publishing partial state.
     *
     * @return Immutable API owner, or nullptr when a library or symbol is unavailable.
     */
    static std::shared_ptr<const api_t> load() {
      return load(dyn::system_loader());
    }

    /**
     * @brief Load both Avahi libraries through injected loading operations.
     *
     * @param loader Loading operations used for this publisher initialization attempt.
     * @return Immutable API owner, or nullptr when a library or symbol is unavailable.
     */
    static std::shared_ptr<const api_t> load(const dyn::loader_t &loader) {
      auto api = std::shared_ptr<api_t>(new api_t());
      api->common_library = loader.open({"libavahi-common.so.3", "libavahi-common.so"});
      if (!api->common_library) {
        return {};
      }

      if (const std::vector<std::tuple<dyn::apiproc *, const char *>> common_functions {
            {(dyn::apiproc *) &api->alternative_service_name, "avahi_alternative_service_name"},
            {(dyn::apiproc *) &api->free, "avahi_free"},
            {(dyn::apiproc *) &api->strdup, "avahi_strdup"},
            {(dyn::apiproc *) &api->strerror, "avahi_strerror"},
            {(dyn::apiproc *) &api->simple_poll_get, "avahi_simple_poll_get"},
            {(dyn::apiproc *) &api->simple_poll_loop, "avahi_simple_poll_loop"},
            {(dyn::apiproc *) &api->simple_poll_quit, "avahi_simple_poll_quit"},
            {(dyn::apiproc *) &api->simple_poll_new, "avahi_simple_poll_new"},
            {(dyn::apiproc *) &api->simple_poll_free, "avahi_simple_poll_free"},
          };
          loader.resolve(api->common_library.get(), common_functions)) {
        return {};
      }

      api->client_library = loader.open({"libavahi-client.so.3", "libavahi-client.so"});
      if (!api->client_library) {
        return {};
      }

      if (const std::vector<std::tuple<dyn::apiproc *, const char *>> client_functions {
            {(dyn::apiproc *) &api->client_new, "avahi_client_new"},
            {(dyn::apiproc *) &api->client_free, "avahi_client_free"},
            {(dyn::apiproc *) &api->entry_group_get_client, "avahi_entry_group_get_client"},
            {(dyn::apiproc *) &api->entry_group_new, "avahi_entry_group_new"},
            {(dyn::apiproc *) &api->entry_group_add_service, "avahi_entry_group_add_service"},
            {(dyn::apiproc *) &api->entry_group_is_empty, "avahi_entry_group_is_empty"},
            {(dyn::apiproc *) &api->entry_group_reset, "avahi_entry_group_reset"},
            {(dyn::apiproc *) &api->entry_group_commit, "avahi_entry_group_commit"},
            {(dyn::apiproc *) &api->client_errno, "avahi_client_errno"},
          };
          loader.resolve(api->client_library.get(), client_functions)) {
        return {};
      }
      return api;
    }

    free_fn free {};  ///< Release an allocation from the common library.
    client_new_fn client_new {};  ///< Create a client with an instance callback context.
    client_free_fn client_free {};  ///< Release a client and its entry groups.
    alternative_service_name_fn alternative_service_name {};  ///< Resolve a conflicting service name.
    entry_group_get_client_fn entry_group_get_client {};  ///< Read the client owning an entry group.
    entry_group_new_fn entry_group_new {};  ///< Create an entry group.
    entry_group_add_service_fn entry_group_add_service {};  ///< Add a service to an entry group.
    entry_group_is_empty_fn entry_group_is_empty {};  ///< Check whether a group has service entries.
    entry_group_reset_fn entry_group_reset {};  ///< Clear the services from a group.
    entry_group_commit_fn entry_group_commit {};  ///< Publish the services in a group.
    strdup_fn strdup {};  ///< Allocate a copy of a service name.
    strerror_fn strerror {};  ///< Format an Avahi error.
    client_errno_fn client_errno {};  ///< Read the last client error.
    simple_poll_get_fn simple_poll_get {};  ///< Read the event-loop API.
    simple_poll_loop_fn simple_poll_loop {};  ///< Run the event loop.
    simple_poll_quit_fn simple_poll_quit {};  ///< Request event-loop shutdown.
    simple_poll_new_fn simple_poll_new {};  ///< Allocate an event loop.
    simple_poll_free_fn simple_poll_free {};  ///< Release an event loop.

  private:
    /**
     * @brief Create an empty API that is validated before being returned to a caller.
     */
    api_t() = default;

    std::shared_ptr<dyn::library_handle_t> common_library;  ///< Keep common functions alive until clients are destroyed.
    std::shared_ptr<dyn::library_handle_t> client_library;  ///< Keep client functions alive for all publisher resources.
  };

}  // namespace avahi
