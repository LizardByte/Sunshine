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
#include "publisher.h"
#include "src/logging.h"
#include "src/network.h"
#include "src/nvhttp.h"
#include "src/platform/common.h"
#include "src/utility.h"

using namespace std::literals;

namespace platf::publish {
  /**
   * @brief Release an Avahi allocation while retaining its loaded library.
   * @tparam T Type of Avahi allocation.
   * @tparam Free Function-pointer member used to release the allocation.
   */
  template<class T, auto Free>
  struct deleter_t {
    std::shared_ptr<const avahi::api_t> api;  ///< Owns the loaded deletion function.

    /**
     * @brief Release the allocation through its Avahi API.
     * @param object Allocation to release.
     */
    void operator()(T *object) const {
      (api.get()->*Free)(object);
    }
  };

  /**
   * @brief Own the published service, its callback context, and its polling thread.
   */
  class deinit_t: public ::platf::deinit_t {
  public:
    /**
     * @brief Construct a publisher retaining a validated Avahi API.
     * @param api Functions retained until all Avahi resources are released.
     * @param port HTTP service port to advertise.
     */
    explicit deinit_t(std::shared_ptr<const avahi::api_t> api, std::uint16_t port):
        api {std::move(api)},
        service_port {port},
        poll {nullptr, {this->api}},
        client {nullptr, {this->api}},
        name {nullptr, {this->api}} {
    }

    /**
     * @brief Stop callbacks before releasing the client and polling resources.
     */
    ~deinit_t() override {
      if (poll) {
        api->simple_poll_quit(poll.get());
      }
      if (poll_thread.joinable()) {
        poll_thread.join();
      }
      client.reset();
      group = nullptr;
    }

    /**
     * @brief Register the service and start its polling loop.
     * @param instance_name Service name already normalized for mDNS publication.
     * @return True when the client and polling thread were initialized.
     */
    bool start(const std::string &instance_name) {
      poll.reset(api->simple_poll_new());
      if (!poll) {
        BOOST_LOG(error) << "Failed to create simple poll object."sv;
        return false;
      }
      name.reset(api->strdup(instance_name.c_str()));
      if (!name) {
        BOOST_LOG(error) << "Failed to allocate Avahi service name."sv;
        return false;
      }
      int avahi_error {};
      // The callback can run synchronously, before client_new returns.
      client.reset(api->client_new(api->simple_poll_get(poll.get()), avahi::ClientFlags(0), client_callback, this, &avahi_error));
      if (!client) {
        BOOST_LOG(error) << "Failed to create client: "sv << api->strerror(avahi_error);
        return false;
      }
      poll_thread = std::jthread {[this]() {
        api->simple_poll_loop(poll.get());
        // The client owns its entry group; release both once callbacks stop.
        client.reset();
        group = nullptr;
      }};
      return true;
    }

  private:
    /**
     * @brief Dispatch an entry-group event to its publisher.
     * @param group Group emitting the event.
     * @param state Current entry-group state.
     * @param userdata Publisher registered with Avahi.
     */
    static void entry_group_callback(avahi::EntryGroup *group, avahi::EntryGroupState state, void *userdata) {
      static_cast<deinit_t *>(userdata)->on_entry_group(group, state);
    }

    /**
     * @brief Dispatch a client event to its publisher.
     * @param client Client emitting the event.
     * @param state Current client state.
     * @param userdata Publisher registered with Avahi.
     */
    static void client_callback(avahi::Client *client, avahi::ClientState state, void *userdata) {
      static_cast<deinit_t *>(userdata)->on_client(client, state);
    }

    /**
     * @brief Replace a colliding service name or stop publication when allocation fails.
     * @return Whether an alternative name was allocated.
     */
    bool rename_service() {
      name.reset(api->alternative_service_name(name.get()));
      if (name) {
        return true;
      }
      BOOST_LOG(error) << "Failed to allocate alternative Avahi service name."sv;
      api->simple_poll_quit(poll.get());
      return false;
    }

    /**
     * @brief React to Avahi entry-group state changes.
     *
     * @param g Entry group that emitted the state change.
     * @param state Avahi entry-group state reported by the callback.
     */
    void on_entry_group(avahi::EntryGroup *g, avahi::EntryGroupState state) {
      group = g;

      switch (state) {
        case avahi::ENTRY_GROUP_ESTABLISHED:
          BOOST_LOG(info) << "Avahi service " << name.get() << " successfully established.";
          break;
        case avahi::ENTRY_GROUP_COLLISION:
          if (!rename_service()) {
            break;
          }

          BOOST_LOG(info) << "Avahi service name collision, renaming service to " << name.get();

          create_services(api->entry_group_get_client(g));
          break;
        case avahi::ENTRY_GROUP_FAILURE:
          BOOST_LOG(error) << "Avahi entry group failure: " << api->strerror(api->client_errno(api->entry_group_get_client(g)));
          api->simple_poll_quit(poll.get());
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

      auto fg = util::fail_guard([this]() {
        api->simple_poll_quit(poll.get());
      });

      if (!group) {
        if (!(group = api->entry_group_new(c, entry_group_callback, this))) {
          BOOST_LOG(error) << "api->entry_group_new() failed: "sv << api->strerror(api->client_errno(c));
          return;
        }
      }

      // Entry-group construction can synchronously report a collision whose new name cannot be allocated.
      if (!name) {
        return;
      }

      if (api->entry_group_is_empty(group)) {
        BOOST_LOG(info) << "Adding avahi service "sv << name.get();

        ret = api->entry_group_add_service(
          group,
          avahi::IF_UNSPEC,
          avahi::PROTO_UNSPEC,
          avahi::PublishFlags(0),
          name.get(),
          platf::SERVICE_TYPE,
          nullptr,
          nullptr,
          service_port,
          nullptr
        );

        if (ret < 0) {
          if (ret == avahi::ERR_COLLISION) {
            // A service name collision with a local service happened. Let's pick a new name
            if (!rename_service()) {
              return;
            }
            BOOST_LOG(info) << "Service name collision, renaming service to "sv << name.get();

            api->entry_group_reset(group);

            create_services(c);

            fg.disable();
            return;
          }

          BOOST_LOG(error) << "Failed to add "sv << platf::SERVICE_TYPE << " service: "sv << api->strerror(ret);
          return;
        }

        ret = api->entry_group_commit(group);
        if (ret < 0) {
          BOOST_LOG(error) << "Failed to commit entry group: "sv << api->strerror(ret);
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
    void on_client(avahi::Client *c, avahi::ClientState state) {
      switch (state) {
        case avahi::CLIENT_S_RUNNING:
          create_services(c);
          break;
        case avahi::CLIENT_FAILURE:
          BOOST_LOG(error) << "Client failure: "sv << api->strerror(api->client_errno(c));
          api->simple_poll_quit(poll.get());
          break;
        case avahi::CLIENT_S_COLLISION:
        case avahi::CLIENT_S_REGISTERING:
          if (group) {
            api->entry_group_reset(group);
          }
          break;
        case avahi::CLIENT_CONNECTING:;
      }
    }

    std::shared_ptr<const avahi::api_t> api;  ///< API that outlives every Avahi resource.
    std::uint16_t service_port;  ///< HTTP port advertised by this publisher.
    std::unique_ptr<avahi::SimplePoll, deleter_t<avahi::SimplePoll, &avahi::api_t::simple_poll_free>> poll;  ///< Service polling loop.
    std::unique_ptr<avahi::Client, deleter_t<avahi::Client, &avahi::api_t::client_free>> client;  ///< Client owning the entry group.
    std::unique_ptr<char, deleter_t<char, &avahi::api_t::free>> name;  ///< Current name, replaced on collisions.
    avahi::EntryGroup *group {};  ///< Group owned by the client.
    std::jthread poll_thread;  ///< Thread stopped before destroying callback context.
  };

  std::unique_ptr<::platf::deinit_t> start(std::shared_ptr<const avahi::api_t> api, std::string instance_name, std::uint16_t port) {
    if (!api) {
      return nullptr;
    }
    auto publisher = std::make_unique<deinit_t>(std::move(api), port);
    if (!publisher->start(instance_name)) {
      return nullptr;
    }
    return publisher;
  }

  [[nodiscard]] std::unique_ptr<::platf::deinit_t> start(const config::settings_t &settings) {
    platf::set_thread_name("publish::avahi");
    return start(avahi::api_t::load(), net::mdns_instance_name(platf::get_host_name()), net::map_port(settings, nvhttp::PORT_HTTP));
  }
}  // namespace platf::publish
