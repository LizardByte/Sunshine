/**
 * @file src/platform/linux/publish.cpp
 * @brief Definitions for publishing services on Linux.
 * @note Adapted from https://www.avahi.org/doxygen/html/client-publish-service_8c-example.html
 */
// standard includes
#include <thread>

// local includes
#include "avahi_api.h"
#include "misc.h"
#include "src/logging.h"
#include "src/network.h"
#include "src/nvhttp.h"
#include "src/platform/common.h"
#include "src/utility.h"

using namespace std::literals;

namespace avahi {
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
