
// adapted from https://www.avahi.org/doxygen/html/client-publish-service_8c-example.html
#include <thread>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/timeval.h>

#include "publish.h"
#include "nvhttp.h"
#include "main.h"

namespace publish {
AvahiEntryGroup *group = NULL;
AvahiSimplePoll *simple_poll = NULL;
char *name = NULL;
void create_services(AvahiClient *c);

void entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, AVAHI_GCC_UNUSED void *userdata) {
  assert(g == group || group == NULL);
  group = g;
  switch (state) {
  case AVAHI_ENTRY_GROUP_ESTABLISHED:
    BOOST_LOG(info) << "Avahi service " << name << " successfully established.";
    break;
  case AVAHI_ENTRY_GROUP_COLLISION:
    char *n;
    n = avahi_alternative_service_name(name);
    avahi_free(name);
    name = n;
    BOOST_LOG(info) << "Avahi service name collision, renaming service to " << name;
    create_services(avahi_entry_group_get_client(g));
    break;
  case AVAHI_ENTRY_GROUP_FAILURE:
    BOOST_LOG(error) << "Avahi entry group failure: " << avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g)));
    avahi_simple_poll_quit(simple_poll);
    break;
  case AVAHI_ENTRY_GROUP_UNCOMMITED:
  case AVAHI_ENTRY_GROUP_REGISTERING:
    ;
  }
}

void create_services(AvahiClient *c) {
  char *n;
  int ret;
  assert(c);
  if (!group) {
    if (!(group = avahi_entry_group_new(c, entry_group_callback, NULL))) {
      BOOST_LOG(error) << "avahi_entry_group_new() failed: " << avahi_strerror(avahi_client_errno(c));
      goto fail;
    }
  }
  if (avahi_entry_group_is_empty(group)) {
    BOOST_LOG(info) << "Adding avahi service " << name;

    if ((ret = avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, AvahiPublishFlags(0), name, SERVICE_TYPE, NULL, NULL, nvhttp::PORT_HTTP, NULL)) < 0) {
      if (ret == AVAHI_ERR_COLLISION)
        goto collision;
      BOOST_LOG(error) << "Failed to add " << SERVICE_TYPE << " service: " << avahi_strerror(ret);
      goto fail;
    }
    if ((ret = avahi_entry_group_commit(group)) < 0) {
      BOOST_LOG(error) << "Failed to commit entry group: " << avahi_strerror(ret);
      goto fail;
    }
  }
  return;
collision:
  // A service name collision with a local service happened. Let's pick a new name
  n = avahi_alternative_service_name(name);
  avahi_free(name);
  name = n;
  BOOST_LOG(info) <<  "Service name collision, renaming service to " << name;
  avahi_entry_group_reset(group);
  create_services(c);
  return;
fail:
  avahi_simple_poll_quit(simple_poll);
}

void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void *userdata) {
  assert(c);
  switch (state) {
  case AVAHI_CLIENT_S_RUNNING:
    create_services(c);
    break;
  case AVAHI_CLIENT_FAILURE:
    BOOST_LOG(error) << "Client failure: " << avahi_strerror(avahi_client_errno(c));
    avahi_simple_poll_quit(simple_poll);
    break;
  case AVAHI_CLIENT_S_COLLISION:
  case AVAHI_CLIENT_S_REGISTERING:
    if (group)
      avahi_entry_group_reset(group);
    break;
  case AVAHI_CLIENT_CONNECTING:
    ;
  }
}

void start(std::shared_ptr<safe::signal_t> shutdown_event) {
  AvahiClient *client = NULL;
  int error;
  if (!(simple_poll = avahi_simple_poll_new())) {
    fprintf(stderr, "Failed to create simple poll object.\n");
    return;
  }
  name = avahi_strdup(SERVICE_NAME);
  client = avahi_client_new(avahi_simple_poll_get(simple_poll), AvahiClientFlags(0), client_callback, NULL, &error);
  if (!client) {
    fprintf(stderr, "Failed to create client: %s\n", avahi_strerror(error));
    avahi_simple_poll_free(simple_poll);
    return;
  }

  std::thread poll_thread { avahi_simple_poll_loop, simple_poll };

  // Wait for any event
  shutdown_event->view();

  avahi_simple_poll_quit(simple_poll);

  poll_thread.join();
  
  avahi_client_free(client);
  avahi_simple_poll_free(simple_poll);
  avahi_free(name);
}
}; // namespace publish