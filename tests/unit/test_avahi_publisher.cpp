/**
 * @file tests/unit/test_avahi_publisher.cpp
 * @brief Test publisher callback ownership and partial-initialization cleanup.
 */
#ifdef __linux__

  // standard includes
  #include <atomic>
  #include <condition_variable>
  #include <cstdlib>
  #include <cstring>
  #include <map>
  #include <mutex>
  #include <string_view>
  #include <vector>

  // lib includes
  #include <gtest/gtest.h>

  // local includes
  #include "src/platform/common.h"
  #include "src/platform/linux/avahi_api.h"
  #include "src/platform/linux/publisher.h"

namespace {
  /**
   * @brief Counters and failure injection owned by one fake Avahi library fixture.
   */
  struct fake_avahi_t {
    bool fail_poll {};  ///< Simulate poll allocation failure.
    bool fail_name {};  ///< Simulate service-name allocation failure.
    bool fail_client {};  ///< Simulate client creation failure.
    bool fail_group {};  ///< Simulate entry-group allocation failure.
    bool fail_alternative {};  ///< Simulate alternative-name allocation failure.
    int add_collisions {};  ///< Number of local service collisions before success.
    bool group_empty {true};  ///< Whether an existing group requires publication.
    std::vector<avahi::ClientState> initial_client_states {avahi::CLIENT_S_RUNNING};  ///< States emitted during client construction.
    avahi::EntryGroupState initial_group_state {avahi::ENTRY_GROUP_REGISTERING};  ///< State emitted during group construction.
    std::atomic<int> resets {};  ///< Entry-group reset requests.
    int add_result {};  ///< Result of adding the service.
    int commit_result {};  ///< Result of committing the service.
    std::atomic<int> polls {};  ///< Live poll allocations.
    std::atomic<int> clients {};  ///< Live client allocations.
    std::atomic<int> names {};  ///< Live service-name allocations.
    std::atomic<int> commits {};  ///< Number of successful commit attempts.
    std::atomic<int> quit_calls {};  ///< Poll shutdown requests.
    std::uint16_t port {};  ///< Port recorded during service registration.
    std::string published_name;  ///< Name recorded during service registration.
    void *callback_context {};  ///< Context passed to the client callback.

    /**
     * @brief Bridge the context-free allocation ABI only during fixture initialization.
     * @return Thread-local pointer to the currently scoped fake library fixture.
     */
    static fake_avahi_t *&allocation_context() {
      static thread_local fake_avahi_t *context {};
      return context;
    }

    /**
     * @brief Scope the context required by Avahi's context-free allocation functions.
     */
    fake_avahi_t() {
      allocation_context() = this;
    }

    /**
     * @brief Verify all publisher resources were released before its fixture.
     */
    ~fake_avahi_t() {
      EXPECT_EQ(polls, 0);
      EXPECT_EQ(clients, 0);
      EXPECT_EQ(names, 0);
      if (allocation_context() == this) {
        allocation_context() = nullptr;
      }
    }
  };

  /**
   * @brief Fake event loop carrying its own callback-independent fixture reference.
   */
  struct fake_poll_t {
    fake_avahi_t &fixture;  ///< Fixture that outlives the poll allocation.
    std::mutex mutex;  ///< Protects the quit flag.
    std::condition_variable condition;  ///< Wakes the poll thread on shutdown.
    bool quit {};  ///< Whether the poll loop should finish.
  };

  /**
   * @brief Fake client carrying the poll and entry-group owner.
   */
  struct fake_client_t {
    fake_poll_t &poll;  ///< Poll used by this client.
    bool group {};  ///< Whether the client owns an entry group.
  };

  /**
   * @brief Allocate a poll using the scoped fixture required by Avahi's zero-argument ABI.
   * @return Opaque fake poll, or nullptr for an injected allocation failure.
   */
  avahi::SimplePoll *new_poll() {
    auto &fixture = *fake_avahi_t::allocation_context();
    if (fixture.fail_poll) {
      return nullptr;
    }
    ++fixture.polls;
    return reinterpret_cast<avahi::SimplePoll *>(new fake_poll_t {fixture});
  }

  /**
   * @brief Release the fake poll after the owner has joined its thread.
   * @param poll Poll to release.
   */
  void free_poll(avahi::SimplePoll *poll) {
    auto *fake = reinterpret_cast<fake_poll_t *>(poll);
    --fake->fixture.polls;
    delete fake;
  }

  /**
   * @brief Return the opaque poll API passed into client construction.
   * @param poll Fake poll.
   * @return Poll API referencing the same allocation.
   */
  avahi::Poll *get_poll(avahi::SimplePoll *poll) {
    return reinterpret_cast<avahi::Poll *>(poll);
  }

  /**
   * @brief Wait until the publisher stops this poll.
   * @param poll Fake poll.
   * @return Zero once polling has stopped.
   */
  int loop_poll(avahi::SimplePoll *poll) {
    auto &fake = *reinterpret_cast<fake_poll_t *>(poll);
    std::unique_lock lock {fake.mutex};
    fake.condition.wait(lock, [&fake]() {
      return fake.quit;
    });
    return 0;
  }

  /**
   * @brief Request shutdown and wake the polling thread.
   * @param poll Fake poll.
   */
  void quit_poll(avahi::SimplePoll *poll) {
    auto &fake = *reinterpret_cast<fake_poll_t *>(poll);
    {
      std::lock_guard lock {fake.mutex};
      fake.quit = true;
      ++fake.fixture.quit_calls;
    }
    fake.condition.notify_all();
  }

  /**
   * @brief Allocate a fake client and invoke its callback before returning it.
   * @param poll Poll API for the client.
   * @param flags Flags requested by the publisher.
   * @param callback Client callback installed by the publisher.
   * @param userdata Explicit publisher context.
   * @param error Receives an injected failure code.
   * @return Opaque fake client or nullptr for a creation failure.
   */
  avahi::Client *new_client(const avahi::Poll *poll, [[maybe_unused]] avahi::ClientFlags flags, avahi::ClientCallback callback, void *userdata, int *error) {
    auto &fake_poll = *reinterpret_cast<fake_poll_t *>(const_cast<avahi::Poll *>(poll));
    auto &fixture = fake_poll.fixture;
    if (fixture.fail_client) {
      *error = -1;
      return nullptr;
    }
    auto *client = new fake_client_t {fake_poll};
    ++fixture.clients;
    fixture.callback_context = userdata;
    EXPECT_NE(userdata, nullptr);
    for (const auto state : fixture.initial_client_states) {
      callback(reinterpret_cast<avahi::Client *>(client), state, userdata);
    }
    return reinterpret_cast<avahi::Client *>(client);
  }

  /**
   * @brief Release the client together with the entry group it owns.
   * @param client Fake client.
   */
  void free_client(avahi::Client *client) {
    auto *fake = reinterpret_cast<fake_client_t *>(client);
    --fake->poll.fixture.clients;
    delete fake;
  }

  /**
   * @brief Allocate the service-name string using the currently scoped fixture.
   * @param name Service name to duplicate.
   * @return Allocated name or nullptr for an injected failure.
   */
  char *duplicate_name(const char *name) {
    auto &fixture = *fake_avahi_t::allocation_context();
    if (fixture.fail_name) {
      return nullptr;
    }
    auto *result = static_cast<char *>(std::malloc(sizeof(fake_avahi_t *) + std::strlen(name) + 1));
    std::memcpy(result, &fake_avahi_t::allocation_context(), sizeof(fake_avahi_t *));
    auto *text = result + sizeof(fake_avahi_t *);
    std::strcpy(text, name);
    ++fixture.names;
    return text;
  }

  /**
   * @brief Release a service name using the fixture recorded in its allocation.
   * @param name Allocated service name.
   */
  void free_name(void *name) {
    auto *allocation = static_cast<char *>(name) - sizeof(fake_avahi_t *);
    fake_avahi_t *fixture {};
    std::memcpy(&fixture, allocation, sizeof(fixture));
    --fixture->names;
    std::free(allocation);
  }

  /**
   * @brief Return another deterministic service name for collision handling.
   * @param name Current name.
   * @return Newly allocated alternative name.
   */
  char *alternative_name([[maybe_unused]] char *name) {
    if (fake_avahi_t::allocation_context()->fail_alternative) {
      return nullptr;
    }
    return duplicate_name("alternative");
  }

  /**
   * @brief Return the client that owns the fake entry group.
   * @param group Fake group represented by its owning client.
   * @return Owning client.
   */
  avahi::Client *group_client(avahi::EntryGroup *group) {
    return reinterpret_cast<avahi::Client *>(group);
  }

  /**
   * @brief Allocate the fake group and invoke its registration callback synchronously.
   * @param client Client owning the group.
   * @param callback Group callback installed by the publisher.
   * @param userdata Publisher context installed on the client too.
   * @return Opaque group or nullptr for injected failure.
   */
  avahi::EntryGroup *new_group(avahi::Client *client, avahi::EntryGroupCallback callback, void *userdata) {
    auto &fake = *reinterpret_cast<fake_client_t *>(client);
    if (fake.poll.fixture.fail_group) {
      return nullptr;
    }
    EXPECT_EQ(userdata, fake.poll.fixture.callback_context);
    fake.group = true;
    auto *group = reinterpret_cast<avahi::EntryGroup *>(client);
    callback(group, fake.poll.fixture.initial_group_state, userdata);
    return group;
  }

  /**
   * @brief Record the name and port published by this owner.
   * @param group Entry group.
   * @param interface Interface selection.
   * @param protocol Protocol selection.
   * @param flags Publication flags.
   * @param name Published service name.
   * @param type Published service type.
   * @param domain Optional domain.
   * @param host Optional host.
   * @param port Advertised service port.
   * @return Injected service-add result.
   */
  int add_service(avahi::EntryGroup *group, [[maybe_unused]] avahi::IfIndex interface, [[maybe_unused]] avahi::Protocol protocol, [[maybe_unused]] avahi::PublishFlags flags, const char *name, [[maybe_unused]] const char *type, [[maybe_unused]] const char *domain, [[maybe_unused]] const char *host, std::uint16_t port, ...) {
    auto &fixture = reinterpret_cast<fake_client_t *>(group)->poll.fixture;
    fixture.published_name = name;
    fixture.port = port;
    if (fixture.add_collisions > 0) {
      --fixture.add_collisions;
      return avahi::ERR_COLLISION;
    }
    return fixture.add_result;
  }

  /**
   * @brief Mark every newly created fake group as empty.
   * @param group Entry group.
   * @return One so registration adds the service.
   */
  int empty_group(avahi::EntryGroup *group) {
    return reinterpret_cast<fake_client_t *>(group)->poll.fixture.group_empty;
  }

  /**
   * @brief Accept fake group reset.
   * @param group Entry group.
   * @return Zero.
   */
  int reset_group(avahi::EntryGroup *group) {
    ++reinterpret_cast<fake_client_t *>(group)->poll.fixture.resets;
    return 0;
  }

  /**
   * @brief Record a publication attempt.
   * @param group Entry group.
   * @return Injected commit result.
   */
  int commit_group(avahi::EntryGroup *group) {
    auto &fixture = reinterpret_cast<fake_client_t *>(group)->poll.fixture;
    ++fixture.commits;
    return fixture.commit_result;
  }

  /**
   * @brief Supply an error code for failure logging.
   * @param client Client whose error is queried.
   * @return Negative fake error code.
   */
  int client_error([[maybe_unused]] avahi::Client *client) {
    return -1;
  }

  /**
   * @brief Format a fake error.
   * @param error Error code.
   * @return Constant test error description.
   */
  char *error_string([[maybe_unused]] int error) {
    return const_cast<char *>("fake error");
  }

  /**
   * @brief Build an immutable API table whose fake functions retain this fixture.
   * @param fixture Fake library and publisher state.
   * @return Validated API table.
   */
  std::shared_ptr<const avahi::api_t> make_api(const std::shared_ptr<fake_avahi_t> &fixture) {
    const std::map<std::string_view, dyn::apiproc> functions {
      {"avahi_free", reinterpret_cast<dyn::apiproc>(&free_name)},
      {"avahi_strdup", reinterpret_cast<dyn::apiproc>(&duplicate_name)},
      {"avahi_alternative_service_name", reinterpret_cast<dyn::apiproc>(&alternative_name)},
      {"avahi_strerror", reinterpret_cast<dyn::apiproc>(&error_string)},
      {"avahi_client_new", reinterpret_cast<dyn::apiproc>(&new_client)},
      {"avahi_client_free", reinterpret_cast<dyn::apiproc>(&free_client)},
      {"avahi_client_errno", reinterpret_cast<dyn::apiproc>(&client_error)},
      {"avahi_entry_group_get_client", reinterpret_cast<dyn::apiproc>(&group_client)},
      {"avahi_entry_group_new", reinterpret_cast<dyn::apiproc>(&new_group)},
      {"avahi_entry_group_add_service", reinterpret_cast<dyn::apiproc>(&add_service)},
      {"avahi_entry_group_is_empty", reinterpret_cast<dyn::apiproc>(&empty_group)},
      {"avahi_entry_group_reset", reinterpret_cast<dyn::apiproc>(&reset_group)},
      {"avahi_entry_group_commit", reinterpret_cast<dyn::apiproc>(&commit_group)},
      {"avahi_simple_poll_get", reinterpret_cast<dyn::apiproc>(&get_poll)},
      {"avahi_simple_poll_loop", reinterpret_cast<dyn::apiproc>(&loop_poll)},
      {"avahi_simple_poll_quit", reinterpret_cast<dyn::apiproc>(&quit_poll)},
      {"avahi_simple_poll_new", reinterpret_cast<dyn::apiproc>(&new_poll)},
      {"avahi_simple_poll_free", reinterpret_cast<dyn::apiproc>(&free_poll)},
    };
    return avahi::api_t::load({
      [fixture](const auto &) -> std::shared_ptr<void> {
        return fixture;
      },
      [functions](void *, const dyn::symbols_t &symbols) {
        for (const auto &[destination, name] : symbols) {
          *destination = functions.at(name);
        }
        return 0;
      },
    });
  }
}  // namespace

TEST(AvahiPublisherTest, UnavailableApiDoesNotConstructPublisher) {
  EXPECT_FALSE(platf::publish::start({}, "Test Sunshine", 47989));
}

TEST(AvahiPublisherTest, SynchronousCallbacksUseInstanceContextAndKeepLibrariesAlive) {
  auto fixture = std::make_shared<fake_avahi_t>();
  auto api = make_api(fixture);
  const std::weak_ptr<const avahi::api_t> weak_api = api;
  auto publisher = platf::publish::start(api, "Test Sunshine", 47989);
  ASSERT_TRUE(publisher);
  EXPECT_EQ(fixture->commits, 1);
  EXPECT_EQ(fixture->port, 47989);
  EXPECT_EQ(fixture->published_name, "Test Sunshine");
  api.reset();
  EXPECT_FALSE(weak_api.expired());
  publisher.reset();
  EXPECT_TRUE(weak_api.expired());
  EXPECT_EQ(fixture->polls, 0);
  EXPECT_EQ(fixture->clients, 0);
  EXPECT_EQ(fixture->names, 0);
}

TEST(AvahiPublisherTest, AllocationFailuresReleasePartialStateAndCanBeRetried) {
  auto fixture = std::make_shared<fake_avahi_t>();
  const auto api = make_api(fixture);
  fixture->fail_poll = true;
  EXPECT_FALSE(platf::publish::start(api, "Test Sunshine", 47989));
  fixture->fail_poll = false;
  fixture->fail_name = true;
  EXPECT_FALSE(platf::publish::start(api, "Test Sunshine", 47989));
  fixture->fail_name = false;
  fixture->fail_client = true;
  EXPECT_FALSE(platf::publish::start(api, "Test Sunshine", 47989));
  EXPECT_EQ(fixture->polls, 0);
  EXPECT_EQ(fixture->names, 0);
  fixture->fail_client = false;
  EXPECT_TRUE(platf::publish::start(api, "Test Sunshine", 47989));
}

TEST(AvahiPublisherTest, RegistrationFailuresStopPollingAndReleaseTheClient) {
  auto fixture = std::make_shared<fake_avahi_t>();
  const auto api = make_api(fixture);
  fixture->fail_group = true;
  EXPECT_TRUE(platf::publish::start(api, "Test Sunshine", 47989));
  fixture->fail_group = false;
  fixture->add_result = -1;
  EXPECT_TRUE(platf::publish::start(api, "Test Sunshine", 47989));
  fixture->add_result = 0;
  fixture->commit_result = -1;
  EXPECT_TRUE(platf::publish::start(api, "Test Sunshine", 47989));
  EXPECT_EQ(fixture->polls, 0);
  EXPECT_EQ(fixture->clients, 0);
  EXPECT_EQ(fixture->names, 0);
  EXPECT_GE(fixture->quit_calls, 3);
}

TEST(AvahiPublisherTest, IndependentPublishersHaveDifferentCallbackContexts) {
  auto first_fixture = std::make_shared<fake_avahi_t>();
  auto first = platf::publish::start(make_api(first_fixture), "First", 47989);
  auto second_fixture = std::make_shared<fake_avahi_t>();
  auto second = platf::publish::start(make_api(second_fixture), "Second", 48089);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_NE(first_fixture->callback_context, second_fixture->callback_context);
  first.reset();
  EXPECT_EQ(first_fixture->clients, 0);
  EXPECT_EQ(second_fixture->clients, 1);
  EXPECT_EQ(second_fixture->published_name, "Second");
}

TEST(AvahiPublisherTest, LocalCollisionSelectsAlternativeNameAndRetriesRegistration) {
  auto fixture = std::make_shared<fake_avahi_t>();
  fixture->add_collisions = 1;
  auto publisher = platf::publish::start(make_api(fixture), "Original", 47989);
  ASSERT_TRUE(publisher);
  EXPECT_EQ(fixture->resets, 1);
  EXPECT_EQ(fixture->published_name, "alternative");
  EXPECT_EQ(fixture->commits, 1);
}

TEST(AvahiPublisherTest, EntryGroupStatesUseOwningPublisher) {
  auto fixture = std::make_shared<fake_avahi_t>();
  const auto api = make_api(fixture);
  for (const auto state : {avahi::ENTRY_GROUP_ESTABLISHED, avahi::ENTRY_GROUP_UNCOMMITED, avahi::ENTRY_GROUP_COLLISION, avahi::ENTRY_GROUP_FAILURE}) {
    fixture->initial_group_state = state;
    EXPECT_TRUE(platf::publish::start(api, "Original", 47989));
    EXPECT_EQ(fixture->clients, 0);
  }
  EXPECT_GE(fixture->quit_calls, 1);
}

TEST(AvahiPublisherTest, ClientRegistrationAndFailuresRetainTheirContext) {
  auto fixture = std::make_shared<fake_avahi_t>();
  fixture->initial_client_states = {avahi::CLIENT_CONNECTING, avahi::CLIENT_S_COLLISION, avahi::CLIENT_S_REGISTERING, avahi::CLIENT_S_RUNNING, avahi::CLIENT_S_COLLISION, avahi::CLIENT_S_REGISTERING, avahi::CLIENT_FAILURE};
  auto publisher = platf::publish::start(make_api(fixture), "Original", 47989);
  ASSERT_TRUE(publisher);
  EXPECT_EQ(fixture->resets, 2);
  EXPECT_GE(fixture->quit_calls, 1);
}

TEST(AvahiPublisherTest, NonemptyEntryGroupDoesNotDuplicateRegistration) {
  auto fixture = std::make_shared<fake_avahi_t>();
  fixture->group_empty = false;
  EXPECT_TRUE(platf::publish::start(make_api(fixture), "Original", 47989));
  EXPECT_EQ(fixture->commits, 0);
}

TEST(AvahiPublisherTest, CollisionNameAllocationFailureStopsPublication) {
  auto fixture = std::make_shared<fake_avahi_t>();
  const auto api = make_api(fixture);
  fixture->fail_alternative = true;
  fixture->add_collisions = 1;
  EXPECT_TRUE(platf::publish::start(api, "Original", 47989));
  fixture->initial_group_state = avahi::ENTRY_GROUP_COLLISION;
  EXPECT_TRUE(platf::publish::start(api, "Original", 47989));
  EXPECT_EQ(fixture->names, 0);
  EXPECT_GE(fixture->quit_calls, 2);
}

#endif
