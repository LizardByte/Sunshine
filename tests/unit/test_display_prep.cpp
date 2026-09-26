/**
 * @file tests/unit/test_display_prep.cpp
 * @brief Test src/display_prep.*.
 */
#include "../tests_common.h"

// standard includes
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// local includes
#include <src/display_device.h>
#include <src/display_prep.h>
#include <src/rtsp.h>

namespace {
  /** @brief Records backend calls without modifying a real display. */
  class fake_backend_t: public display_prep::backend_t {
  public:
    std::vector<std::string> events;  ///< Operations observed by the test.
    std::function<void(bool)> on_reverted;  ///< Pending display restoration callback.
    bool do_succeeds {true};  ///< Whether the Do sequence succeeds.
    bool undo_succeeds {true};  ///< Whether the Undo sequence succeeds.
    bool revert_throws {false};  ///< Whether requesting restoration throws.

    bool prepare(const rtsp_stream::launch_session_t &) override {
      events.emplace_back("do");
      return do_succeeds;
    }

    bool undo() override {
      events.emplace_back("undo");
      return undo_succeeds;
    }

    void revert_display(std::function<void(bool)> callback) override {
      events.emplace_back("revert");
      if (revert_throws) {
        throw std::runtime_error("Revert unavailable");
      }
      on_reverted = std::move(callback);
    }

    /** @brief Simulate display restoration or an abandoned persistence state. */
    void complete_revert(bool restored = true) {
      std::exchange(on_reverted, nullptr)(restored);
    }
  };

  using events_t = std::vector<std::string>;

  /** @brief Manager with an observable fake backend. */
  class DisplayPrepTest: public ::testing::Test {
  protected:
    /** @brief Build a manager that owns a fake backend. */
    DisplayPrepTest() {
      auto owned = std::make_unique<fake_backend_t>();
      backend = owned.get();
      manager = std::make_shared<display_prep::manager_t>(std::move(owned));
    }

    fake_backend_t *backend;  ///< Owned by manager. NOSONAR(cpp:S3656): protected members are intentional for test fixture subclassing
    std::shared_ptr<display_prep::manager_t> manager;  ///< Manager under test. NOSONAR(cpp:S3656): protected members are intentional for test fixture subclassing
  };
}  // namespace

TEST_F(DisplayPrepTest, LastLeaseUndoesOnlyAfterDisplayRestore) {
  auto first = manager->acquire({});
  ASSERT_NE(first, nullptr);
  EXPECT_TRUE(first->start());
  auto second = manager->acquire({});  // Shares the prepared output.
  ASSERT_NE(second, nullptr);
  EXPECT_TRUE(second->start());
  EXPECT_EQ(backend->events, events_t({"do"}));

  first->expire();  // A late RTSP timeout must not release an active lease.
  first->finish();
  EXPECT_EQ(manager->state(), display_prep::state_e::prepared);
  second->finish();
  EXPECT_EQ(manager->state(), display_prep::state_e::restoring);
  EXPECT_EQ(backend->events, events_t({"do", "revert"}));

  backend->complete_revert();
  EXPECT_EQ(backend->events, events_t({"do", "revert", "undo"}));
  EXPECT_EQ(manager->state(), display_prep::state_e::idle);

  second->finish();  // Releasing twice is harmless.
  EXPECT_EQ(backend->events.size(), 3);
}

TEST_F(DisplayPrepTest, ExpiredLeaseRestoresAndCannotStart) {
  auto pending = manager->acquire({});
  ASSERT_NE(pending, nullptr);

  pending->expire();
  EXPECT_FALSE(pending->start());
  EXPECT_EQ(manager->state(), display_prep::state_e::restoring);
  backend->complete_revert();
  EXPECT_EQ(manager->state(), display_prep::state_e::idle);
  EXPECT_EQ(backend->events, events_t({"do", "revert", "undo"}));
}

TEST_F(DisplayPrepTest, DroppedLeaseIsReleased) {
  manager->acquire({});

  EXPECT_EQ(manager->state(), display_prep::state_e::restoring);
  backend->complete_revert();
  EXPECT_EQ(backend->events, events_t({"do", "revert", "undo"}));
}

TEST_F(DisplayPrepTest, FailedDoRunsUndoAndAbortsLaunch) {
  backend->do_succeeds = false;

  EXPECT_EQ(manager->acquire({}), nullptr);
  EXPECT_EQ(manager->error(), "A pre-display Do command failed");
  EXPECT_EQ(backend->events, events_t({"do", "undo"}));
  EXPECT_EQ(manager->state(), display_prep::state_e::idle);
}

TEST_F(DisplayPrepTest, FailedDoWithFailedUndoBlocksRetry) {
  backend->do_succeeds = false;
  backend->undo_succeeds = false;

  EXPECT_EQ(manager->acquire({}), nullptr);
  EXPECT_EQ(manager->state(), display_prep::state_e::failed);
  EXPECT_EQ(manager->acquire({}), nullptr);
  EXPECT_EQ(backend->events, events_t({"do", "undo"}));
}

TEST_F(DisplayPrepTest, FailedUndoBlocksAnotherDo) {
  backend->undo_succeeds = false;
  manager->acquire({});
  backend->complete_revert();

  EXPECT_EQ(manager->state(), display_prep::state_e::failed);
  EXPECT_EQ(manager->error(), "Pre-display Undo was incomplete");
  EXPECT_EQ(manager->acquire({}), nullptr);
  EXPECT_EQ(backend->events, events_t({"do", "revert", "undo"}));
}

TEST_F(DisplayPrepTest, ReconnectWaitsForPreviousRestoration) {
  manager->acquire({});

  auto reconnect = std::async(std::launch::async, [this] {
    return manager->acquire({});
  });
  EXPECT_EQ(reconnect.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
  backend->complete_revert();
  auto next = reconnect.get();
  ASSERT_NE(next, nullptr);
  EXPECT_EQ(backend->events, events_t({"do", "revert", "undo", "do"}));
  next->finish();
  backend->complete_revert();
}

TEST_F(DisplayPrepTest, ResetPersistenceDoesNotRunUndo) {
  manager->acquire({});
  backend->complete_revert(false);

  EXPECT_EQ(manager->state(), display_prep::state_e::failed);
  EXPECT_EQ(backend->events, events_t({"do", "revert"}));
  EXPECT_EQ(manager->acquire({}), nullptr);
}

TEST_F(DisplayPrepTest, RevertRequestFailureDoesNotEscapeLeaseDestructor) {
  backend->revert_throws = true;
  manager->acquire({});

  EXPECT_EQ(manager->state(), display_prep::state_e::failed);
  EXPECT_EQ(backend->events, events_t({"do", "revert"}));
}

TEST(DisplayPrep, NullBackendIsRejected) {
  EXPECT_THROW(display_prep::manager_t {nullptr}, std::invalid_argument);
}

TEST(DisplayPrep, RevertCallbackRunsWhenDisplayDeviceIsUnavailable) {
  // Tests never initialize display_device, so restoration completes immediately.
  std::optional<bool> result;
  display_device::revert_configuration([&result](bool restored) {
    result = restored;
  });
  EXPECT_EQ(result, true);
}

namespace {
  /** @brief Command backend tests that run real shell commands. */
  class DisplayPrepCommandTest: public ::testing::Test {
  protected:
    void SetUp() override {
      log_ = std::filesystem::temp_directory_path() / std::format("sunshine_display_prep_test_{}.log", std::chrono::steady_clock::now().time_since_epoch().count());  // NOSONAR(cpp:S5443): safe for tests
      std::filesystem::remove(log_);
    }

    void TearDown() override {
      std::filesystem::remove(log_);
    }

    /** @brief Command that appends a word to the log and exits with a code. */
    std::string append(const std::string &word, int exit_code = 0) const {
#ifdef _WIN32
      return std::format(R"(cmd /C "echo {}>> "{}" & exit {}")", word, log_.string(), exit_code);
#else
      return std::format(R"(sh -c "echo {} >> '{}'; exit {}")", word, log_.string(), exit_code);
#endif
    }

    /** @brief Read logged words in order. */
    std::vector<std::string> logged() const {
      std::vector<std::string> words;
      std::ifstream in(log_);
      for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
          line.pop_back();
        }
        words.push_back(line);
      }
      return words;
    }

    /** @brief Build a Do/Undo pair. */
    static config::prep_cmd_t pair(std::string do_cmd, std::string undo_cmd) {
      return {std::move(do_cmd), std::move(undo_cmd), false};
    }

    std::filesystem::path log_;  ///< File written by test commands. NOSONAR(cpp:S3656): protected members are intentional for test fixture subclassing
  };
}  // namespace

TEST_F(DisplayPrepCommandTest, RunsDoInOrderAndUndoInReverse) {
  auto backend = display_prep::make_command_backend({pair(append("do1"), append("undo1")), pair(append("do2"), append("undo2"))});

  EXPECT_TRUE(backend->prepare({}));
  EXPECT_TRUE(backend->undo());
  EXPECT_EQ(logged(), events_t({"do1", "do2", "undo2", "undo1"}));
}

TEST_F(DisplayPrepCommandTest, ReceivesClientEnvironment) {
#ifdef _WIN32
  const std::string client_width = "%SUNSHINE_CLIENT_WIDTH%";
#else
  const std::string client_width = "$SUNSHINE_CLIENT_WIDTH";
#endif
  auto backend = display_prep::make_command_backend({pair(append(client_width), append("undo"))});
  rtsp_stream::launch_session_t session {};
  session.width = 1648;

  EXPECT_TRUE(backend->prepare(session));
  EXPECT_TRUE(backend->undo());
  EXPECT_EQ(logged(), events_t({"1648", "undo"}));
}

TEST_F(DisplayPrepCommandTest, FailedDoStopsAndUndoesAttemptedCommands) {
  auto backend = display_prep::make_command_backend({
    pair(append("do1"), append("undo1")),
    pair(append("do2", 1), append("undo2")),
    pair(append("do3"), append("undo3")),
  });

  EXPECT_FALSE(backend->prepare({}));
  EXPECT_TRUE(backend->undo());
  EXPECT_EQ(logged(), events_t({"do1", "do2", "undo2", "undo1"}));
}

TEST_F(DisplayPrepCommandTest, FailedUndoIsReported) {
  auto backend = display_prep::make_command_backend({pair(append("do1"), append("undo1", 1))});

  EXPECT_TRUE(backend->prepare({}));
  EXPECT_FALSE(backend->undo());
}

TEST_F(DisplayPrepCommandTest, RejectsDoWithoutUndo) {
  auto backend = display_prep::make_command_backend({pair(append("do1"), "")});

  EXPECT_FALSE(backend->prepare({}));
  EXPECT_TRUE(backend->undo());
  EXPECT_TRUE(logged().empty());
}
