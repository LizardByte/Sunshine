
// adapted from https://www.avahi.org/doxygen/html/client-publish-service_8c-example.html
#include <thread>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>

#include "sunshine/main.h"
#include "sunshine/nvhttp.h"
#include "sunshine/platform/common.h"
#include "sunshine/utility.h"

using namespace std::literals;
namespace platf::publish {

template<class T>
void free(T *p) {
  avahi_free(p);
}

template<class T>
using ptr_t    = util::safe_ptr<T, free<T>>;
using client_t = util::safe_ptr<AvahiClient, avahi_client_free>;
using poll_t   = util::safe_ptr<AvahiSimplePoll, avahi_simple_poll_free>;

AvahiEntryGroup *group = NULL;

poll_t poll;

ptr_t<char> name;

void create_services(AvahiClient *c);

void entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, AVAHI_GCC_UNUSED void *userdata) {
  group = g;

  switch(state) {
  case AVAHI_ENTRY_GROUP_ESTABLISHED:
    BOOST_LOG(info) << "Avahi service " << name.get() << " successfully established.";
    break;
  case AVAHI_ENTRY_GROUP_COLLISION:
    name.reset(avahi_alternative_service_name(name.get()));

    BOOST_LOG(info) << "Avahi service name collision, renaming service to " << name.get();

    create_services(avahi_entry_group_get_client(g));
    break;
  case AVAHI_ENTRY_GROUP_FAILURE:
    BOOST_LOG(error) << "Avahi entry group failure: " << avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g)));
    avahi_simple_poll_quit(poll.get());
    break;
  case AVAHI_ENTRY_GROUP_UNCOMMITED:
  case AVAHI_ENTRY_GROUP_REGISTERING:;
  }
}

void create_services(AvahiClient *c) {
  int ret;

  auto fg = util::fail_guard([]() {
    avahi_simple_poll_quit(poll.get());
  });

  if(!group) {
    if(!(group = avahi_entry_group_new(c, entry_group_callback, nullptr))) {
      BOOST_LOG(error) << "avahi_entry_group_new() failed: "sv << avahi_strerror(avahi_client_errno(c));
      return;
    }
  }
  if(avahi_entry_group_is_empty(group)) {
    BOOST_LOG(info) << "Adding avahi service "sv << name.get();

    ret = avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, AvahiPublishFlags(0), name.get(), SERVICE_TYPE, nullptr, nullptr, nvhttp::PORT_HTTP, nullptr);
    if(ret < 0) {
      if(ret == AVAHI_ERR_COLLISION) {
        // A service name collision with a local service happened. Let's pick a new name
        name.reset(avahi_alternative_service_name(name.get()));
        BOOST_LOG(info) << "Service name collision, renaming service to "sv << name.get();

        avahi_entry_group_reset(group);

        create_services(c);

        fg.disable();
        return;
      }

      BOOST_LOG(error) << "Failed to add "sv << SERVICE_TYPE << " service: "sv << avahi_strerror(ret);
      return;
    }

    ret = avahi_entry_group_commit(group);
    if(ret < 0) {
      BOOST_LOG(error) << "Failed to commit entry group: "sv << avahi_strerror(ret);
      return;
    }
  }

  fg.disable();
}

void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void *userdata) {
  switch(state) {
  case AVAHI_CLIENT_S_RUNNING:
    create_services(c);
    break;
  case AVAHI_CLIENT_FAILURE:
    BOOST_LOG(error) << "Client failure: "sv << avahi_strerror(avahi_client_errno(c));
    avahi_simple_poll_quit(poll.get());
    break;
  case AVAHI_CLIENT_S_COLLISION:
  case AVAHI_CLIENT_S_REGISTERING:
    if(group)
      avahi_entry_group_reset(group);
    break;
  case AVAHI_CLIENT_CONNECTING:;
  }
}

void start() {
  auto shutdown_event = mail::man->event<bool>(mail::shutdown);

  int avhi_error;

  poll.reset(avahi_simple_poll_new());
  if(!poll) {
    BOOST_LOG(error) << "Failed to create simple poll object."sv;
    return;
  }

  name.reset(avahi_strdup(SERVICE_NAME));

  client_t client {
    avahi_client_new(avahi_simple_poll_get(poll.get()), AvahiClientFlags(0), client_callback, NULL, &avhi_error)
  };

  if(!client) {
    BOOST_LOG(error) << "Failed to create client: "sv << avahi_strerror(avhi_error);
    return;
  }

  std::thread poll_thread { avahi_simple_poll_loop, poll.get() };

  auto fg = util::fail_guard([&]() {
    avahi_simple_poll_quit(poll.get());
    poll_thread.join();
  });

  // Wait for any event
  shutdown_event->view();
}
}; // namespace platf::publish