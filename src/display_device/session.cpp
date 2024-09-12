// standard includes
#include <boost/optional/optional_io.hpp>
#include <boost/process.hpp>
#include <thread>

// local includes
#include "src/globals.h"
#include "session.h"
#include "src/confighttp.h"
#include "src/platform/common.h"
#include "src/rtsp.h"
#include "to_string.h"

namespace display_device {

  class session_t::StateRetryTimer {
  public:
    /**
     * @brief A constructor for the timer.
     * @param mutex A shared mutex for synchronization.
     * @warning Because we are keeping references to shared parameters, we MUST ensure they outlive this object!
     */
    StateRetryTimer(std::mutex &mutex):
        mutex { mutex }, timer_thread {
          std::thread { [this]() {
            std::unique_lock<std::mutex> lock { this->mutex };
            while (keep_alive) {
              can_wake_up = false;
              if (next_wake_up_time) {
                // We're going to sleep forever until manually woken up or the time elapses
                sleep_cv.wait_until(lock, *next_wake_up_time, [this]() { return can_wake_up; });
              }
              else {
                // We're going to sleep forever until manually woken up
                sleep_cv.wait(lock, [this]() { return can_wake_up; });
              }

              if (next_wake_up_time) {
                // Timer has just been started, or we have waited for the required amount of time.
                // We can check which case it is by comparing time points.

                const auto now { std::chrono::steady_clock::now() };
                if (now < *next_wake_up_time) {
                  // Thread has been woken up manually to synchronize the time points.
                  // We do nothing and just go back to waiting with a new time point.
                }
                else {
                  next_wake_up_time = boost::none;

                  const auto result { !this->retry_function || this->retry_function() };
                  if (!result) {
                    next_wake_up_time = now + this->timeout_duration;
                  }
                }
              }
              else {
                // Timer has been stopped.
                // We do nothing and just go back to waiting until notified (unless we are killing the thread).
              }
            }
          } }
        } {
    }

    /**
     * @brief A destructor for the timer that gracefully shuts down the thread.
     */
    ~StateRetryTimer() {
      {
        std::lock_guard lock { mutex };
        keep_alive = false;
        next_wake_up_time = boost::none;
        wake_up_thread();
      }

      timer_thread.join();
    }

    /**
     * @brief Start or stop the timer thread.
     * @param retry_function Function to be executed every X seconds.
     *                       If the function returns true, the loop is stopped.
     *                       If the function is of type nullptr_t, the loop is stopped.
     * @warning This method does NOT acquire the mutex! It is intended to be used from places
     *          where the mutex has already been locked.
     */
    void
    setup_timer(std::function<bool()> retry_function) {
      this->retry_function = std::move(retry_function);

      if (this->retry_function) {
        next_wake_up_time = std::chrono::steady_clock::now() + timeout_duration;
      }
      else {
        if (!next_wake_up_time) {
          return;
        }

        next_wake_up_time = boost::none;
      }

      wake_up_thread();
    }

  private:
    /**
     * @brief Manually wake up the thread.
     */
    void
    wake_up_thread() {
      can_wake_up = true;
      sleep_cv.notify_one();
    }

    std::mutex &mutex; /**< A reference to a shared mutex. */
    std::chrono::seconds timeout_duration { 5 }; /**< A retry time for the timer. */
    std::function<bool()> retry_function; /**< Function to be executed until it succeeds. */

    std::thread timer_thread; /**< A timer thread. */
    std::condition_variable sleep_cv; /**< Condition variable for waking up thread. */

    bool can_wake_up { false }; /**< Safeguard for the condition variable to prevent sporadic thread wake ups. */
    bool keep_alive { true }; /**< A kill switch for the thread when it has been woken up. */
    boost::optional<std::chrono::steady_clock::time_point> next_wake_up_time; /**< Next time point for thread to wake up. */
  };

  session_t::deinit_t::~deinit_t() {
    session_t::get().restore_state();
  }

  session_t &
  session_t::get() {
    static session_t session;
    return session;
  }

  std::unique_ptr<session_t::deinit_t>
  session_t::init() {
    const auto devices { enum_available_devices() };
    const auto vdd_devices { display_device::find_device_by_friendlyname(zako_name) };
    if (!devices.empty()) {
      BOOST_LOG(info) << "Available display devices: " << to_string(devices);
      // 大多数哔叽本开机默认虚拟屏优先导致黑屏 
      if (!vdd_devices.empty() && devices.size() > 1) {
        session_t::get().disable_vdd();
        Sleep(2333);
      }
    }

    session_t::get().settings.set_filepath(platf::appdata() / "original_display_settings.json");

    session_t::get().restore_state();
    return std::make_unique<deinit_t>();
  }

  void
  session_t::configure_display(const config::video_t &config, const rtsp_stream::launch_session_t &session, bool is_reconfigure) {
    std::lock_guard lock { mutex };

    const auto parsed_config { make_parsed_config(config, session, is_reconfigure) };
    if (!parsed_config) {
      BOOST_LOG(error) << "Failed to parse configuration for the the display device settings!";
      return;
    }

    if (settings.is_changing_settings_going_to_fail()) {
      timer->setup_timer([this, config_copy = *parsed_config]() {
        if (settings.is_changing_settings_going_to_fail()) {
          BOOST_LOG(warning) << "Applying display settings will fail - retrying later...";
          return false;
        }

        const auto result { settings.apply_config(config_copy) };
        if (!result) {
          BOOST_LOG(warning) << "Failed to apply display settings - will stop trying, but will allow stream to continue.";

          // WARNING! After call to the method below, this lambda function is no be longer valid!
          // DO NOT access anything from the capture list!
          restore_state_impl();
        }
        return true;
      });

      BOOST_LOG(warning) << "It is already known that display settings cannot be changed. Allowing stream to start without changing the settings, but will retry changing settings later...";
      return;
    }

    const auto result { settings.apply_config(*parsed_config) };
    if (result) {
      timer->setup_timer(nullptr);
    }
    else {
      restore_state_impl();
    }
  }

  void
  session_t::enable_vdd() {
    boost::process::environment _env = boost::this_process::environment();
    auto working_dir = boost::filesystem::path();

    std::error_code ec;
    std::string cmd = "C:\\Program Files\\Sunshine\\tools\\device-toggler.exe 1 2 \"Virtual Display with HDR\"";

    auto child = platf::run_command(true, true, cmd, working_dir, _env, nullptr, ec, nullptr);
    if (ec) {
      BOOST_LOG(warning) << "Couldn't run cmd ["sv << cmd << "]: System: "sv << ec.message();
    }
    else {
      BOOST_LOG(info) << "Executing idd cmd ["sv << cmd << "]"sv;
      child.detach();
    }
  }

  void
  session_t::disable_vdd() {
    boost::process::environment _env = boost::this_process::environment();
    auto working_dir = boost::filesystem::path();

    std::error_code ec;
    std::string cmd = "C:\\Program Files\\Sunshine\\tools\\device-toggler.exe 2 2 \"Virtual Display with HDR\"";

    auto child = platf::run_command(true, true, cmd, working_dir, _env, nullptr, ec, nullptr);
    if (ec) {
      BOOST_LOG(warning) << "Couldn't run cmd ["sv << cmd << "]: System: "sv << ec.message();
    }
    else {
      BOOST_LOG(info) << "Executing idd cmd ["sv << cmd << "]"sv;
      child.detach();
    }
  }

  void
  session_t::prepare_vdd(parsed_config_t &config, const rtsp_stream::launch_session_t &session) {
    // resolutions and fps from parsed
    std::stringstream new_setting;
    new_setting << to_string(*config.resolution) << "@" << to_string(*config.refresh_rate);
    bool need_reset_vdd = display_device::session_t::get().last_vdd_setting != new_setting.str();
    BOOST_LOG(info) << "last_vdd_setting/new_setting from parsed: "sv << display_device::session_t::get().last_vdd_setting << "/" << new_setting.str();

    if (need_reset_vdd) {
      std::stringstream resolutions;
      std::stringstream fps;
      resolutions << "[1920x1080," << to_string(*config.resolution) << "]";
      fps << "[60," << to_string(*config.refresh_rate) << "]";

      confighttp::saveVddSettings(resolutions.str(), fps.str(), config::video.adapter_name);
      BOOST_LOG(info) << "Set Client request res to VDD: "sv << new_setting.str();
      display_device::session_t::get().last_vdd_setting = new_setting.str();
    }

    auto device_zako = display_device::find_device_by_friendlyname(zako_name);
    if (device_zako.empty()) {
      session_t::get().enable_vdd();
    }
    else if (need_reset_vdd) {
      session_t::get().disable_vdd();
      Sleep(2333);
      session_t::get().enable_vdd();
    }

    device_zako = display_device::find_device_by_friendlyname(zako_name);
    while (device_zako.empty()) {
      device_zako = display_device::find_device_by_friendlyname(zako_name);
      Sleep(233);
    }

    if (!device_zako.empty()) {
      config.device_id = device_zako;
      config::video.output_name = device_zako;
    }
  }

  void
  session_t::restore_state() {
    std::lock_guard lock { mutex };
    restore_state_impl();
  }

  void
  session_t::reset_persistence() {
    std::lock_guard lock { mutex };

    settings.reset_persistence();
    timer->setup_timer(nullptr);
  }

  void
  session_t::restore_state_impl() {
    if (!settings.is_changing_settings_going_to_fail() && settings.revert_settings()) {
      timer->setup_timer(nullptr);
    }
    else {
      if (settings.is_changing_settings_going_to_fail()) {
        BOOST_LOG(warning) << "Reverting display settings will fail - retrying later...";
      }

      timer->setup_timer([this]() {
        if (settings.is_changing_settings_going_to_fail()) {
          BOOST_LOG(warning) << "Reverting display settings will still fail - retrying later...";
          return false;
        }
        return settings.revert_settings();
      });
    }
  }

  session_t::session_t():
      timer { std::make_unique<StateRetryTimer>(mutex) } {
  }

}  // namespace display_device
