/**
 * @file src/display_prep.cpp
 * @brief Implements preparation leases and configured command execution.
 */
#include "display_prep.h"

// standard includes
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

// local includes
#include "display_device.h"
#include "logging.h"
#include "process.h"
#include "rtsp.h"

namespace display_prep {
  namespace {
    constexpr auto RESTORE_WAIT = std::chrono::seconds {15};  ///< Maximum wait for a preceding stream to restore.

    /** @brief Runs configured commands using Sunshine's process launcher. */
    class command_backend_t: public backend_t {
    public:
      /**
       * @brief Construct the backend.
       * @param commands Ordered Do/Undo command pairs.
       */
      explicit command_backend_t(std::vector<config::prep_cmd_t> commands):
          commands_(std::move(commands)) {}

      bool prepare(const rtsp_stream::launch_session_t &session) override {
        attempted_ = 0;
        if (std::ranges::any_of(commands_, [](const auto &cmd) {
              return !cmd.do_cmd.empty() && cmd.undo_cmd.empty();
            })) {
          BOOST_LOG(::error) << "Every pre-display Do command requires an Undo command";
          return false;
        }

        env_ = boost::process::v1::environment {boost::this_process::environment()};
        proc::add_client_env(env_, session);
        return std::ranges::all_of(commands_, [this](const auto &cmd) {
          // Count a failed command too: it may have changed state before exiting.
          ++attempted_;
          return cmd.do_cmd.empty() || run(cmd.do_cmd, cmd.elevated);
        });
      }

      bool undo() override {
        bool success = true;
        for (auto i = attempted_; i-- > 0;) {
          const auto &cmd = commands_[i];
          if (!cmd.do_cmd.empty()) {
            success = run(cmd.undo_cmd, cmd.elevated) && success;
          }
        }
        if (success) {
          attempted_ = 0;
        }
        return success;
      }

      void revert_display(std::function<void(bool)> on_reverted) override {
        display_device::revert_configuration(std::move(on_reverted));
      }

    private:
      /**
       * @brief Run a command and wait for it to exit.
       * @param raw Command line.
       * @param elevated Whether to run elevated.
       * @return True if the command exited with code 0.
       */
      bool run(const std::string &raw, bool elevated) {
        const auto cmd = proc::prepare_command(raw);
        BOOST_LOG(info) << "Executing pre-display command: [" << cmd << ']';
        auto working_dir = proc::find_working_directory(cmd, env_);
        std::error_code ec;
        auto child = platf::run_command(elevated, true, cmd, working_dir, env_, nullptr, ec, nullptr);
        if (!ec) {
          child.wait(ec);
        }
        if (ec) {
          BOOST_LOG(::error) << "Pre-display command [" << cmd << "] failed: " << ec.message();
          return false;
        }
        if (const auto code = child.exit_code(); code != 0) {
          BOOST_LOG(::error) << "Pre-display command [" << cmd << "] exited with code " << code;
          return false;
        }
        return true;
      }

      std::vector<config::prep_cmd_t> commands_;  ///< Ordered Do/Undo command pairs.
      std::size_t attempted_ {0};  ///< Number of leading commands whose Do was attempted.
      boost::process::v1::environment env_;  ///< Environment of the first client.
    };

    /**
     * @brief Return the manager shared by all configured launches.
     * @return Shared manager for this Sunshine process.
     */
    std::shared_ptr<manager_t> runtime_manager() {
      static const auto manager = std::make_shared<manager_t>(std::make_unique<command_backend_t>(config::sunshine.pre_display_prep_cmds));
      return manager;
    }
  }  // namespace

  lease_t::lease_t(std::shared_ptr<manager_t> manager):
      manager_(std::move(manager)) {}

  lease_t::~lease_t() {
    finish();
  }

  bool lease_t::start() {
    std::lock_guard lock(manager_->mutex_);
    if (phase_ == phase_e::released) {
      return false;
    }
    phase_ = phase_e::active;
    return true;
  }

  void lease_t::expire() {
    manager_->release(*this, true);
  }

  void lease_t::finish() {
    manager_->release(*this, false);
  }

  manager_t::manager_t(std::unique_ptr<backend_t> backend):
      backend_(std::move(backend)) {
    if (!backend_) {
      throw std::invalid_argument("A pre-display command backend is required");
    }
  }

  std::shared_ptr<lease_t> manager_t::acquire(const rtsp_stream::launch_session_t &session) {
    // Allocate before Do: an allocation failure must not strand a prepared output.
    auto lease = std::shared_ptr<lease_t>(new lease_t(shared_from_this()));  // NOSONAR(cpp:S5950): the constructor is private
    std::unique_lock lock(mutex_);
    if (!restored_.wait_for(lock, RESTORE_WAIT, [this] {
          return state_ != state_e::restoring;
        })) {
      error_ = "Previous display restoration is still pending";
      return nullptr;
    }
    if (state_ == state_e::failed) {
      return nullptr;
    }

    if (leases_ == 0) {
      bool prepared = false;
      try {
        prepared = backend_->prepare(session);
      } catch (const std::exception &e) {
        BOOST_LOG(::error) << "Pre-display Do threw: " << e.what();
      }
      if (!prepared) {
        const bool undone = undo_safely();
        state_ = undone ? state_e::idle : state_e::failed;
        error_ = undone ? "A pre-display Do command failed" : "A pre-display Do command failed and Undo was incomplete";
        return nullptr;
      }
    }

    ++leases_;
    lease->phase_ = lease_t::phase_e::pending;
    state_ = state_e::prepared;
    error_.clear();
    return lease;
  }

  void manager_t::release(lease_t &lease, bool pending_only) {
    using enum lease_t::phase_e;
    {
      std::lock_guard lock(mutex_);
      if (lease.phase_ == released || (pending_only && lease.phase_ == active)) {
        return;
      }

      lease.phase_ = released;
      if (--leases_ > 0) {
        return;
      }
      state_ = state_e::restoring;
    }

    // Undo only after the display no longer depends on the prepared output.
    try {
      backend_->revert_display([self = shared_from_this()](bool restored) {
        self->finish_restore(restored);
      });
    } catch (const std::exception &e) {
      std::lock_guard lock(mutex_);
      state_ = state_e::failed;
      error_ = std::string {"Could not request display restoration: "} + e.what();
      restored_.notify_all();
    }
  }

  void manager_t::finish_restore(bool restored) {
    using enum state_e;
    if (!restored) {
      std::lock_guard lock(mutex_);
      state_ = failed;
      error_ = "Display persistence was reset before restoration; pre-display Undo requires manual recovery";
      restored_.notify_all();
      return;
    }
    const bool undone = undo_safely();
    std::lock_guard lock(mutex_);
    state_ = undone ? idle : failed;
    error_ = undone ? "" : "Pre-display Undo was incomplete";
    restored_.notify_all();
  }

  bool manager_t::undo_safely() {
    try {
      return backend_->undo();
    } catch (const std::exception &e) {
      BOOST_LOG(::error) << "Pre-display Undo threw: " << e.what();
      return false;
    }
  }

  state_e manager_t::state() const {
    std::lock_guard lock(mutex_);
    return state_;
  }

  std::string manager_t::error() const {
    std::lock_guard lock(mutex_);
    return error_;
  }

  std::unique_ptr<backend_t> make_command_backend(std::vector<config::prep_cmd_t> commands) {
    return std::make_unique<command_backend_t>(std::move(commands));
  }

  std::shared_ptr<lease_t> prepare(const rtsp_stream::launch_session_t &session) {
    if (config::sunshine.pre_display_prep_cmds.empty()) {
      return nullptr;
    }
    auto manager = runtime_manager();
    auto lease = manager->acquire(session);
    if (!lease) {
      throw std::runtime_error(manager->error());
    }
    return lease;
  }

  bool owns_display_restoration() {
    if (config::sunshine.pre_display_prep_cmds.empty()) {
      return false;
    }
    const auto state = runtime_manager()->state();
    return state == state_e::prepared || state == state_e::restoring;
  }
}  // namespace display_prep
