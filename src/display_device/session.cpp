// standard includes
#include <boost/optional/optional_io.hpp>
#include <boost/process.hpp>
#include <future>
#include <thread>

// local includes
#include "session.h"
#include "src/confighttp.h"
#include "src/globals.h"
#include "src/platform/common.h"
#include "src/rtsp.h"
#include "to_string.h"
#include "vdd_utils.h"

namespace display_device {

  class session_t::StateRetryTimer {
  public:
    /**
     * @brief A constructor for the timer.
     * @param mutex A shared mutex for synchronization.
     * @warning Because we are keeping references to shared parameters, we MUST ensure they outlive this object!
     */
    StateRetryTimer(std::mutex &mutex, std::chrono::seconds timeout = std::chrono::seconds { 5 }):
        mutex { mutex }, timeout_duration { timeout }, timer_thread {
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
    // Stop vdd timer before destruction
    session_t::get().vdd_timer->setup_timer(nullptr);
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
    const auto vdd_device { display_device::find_device_by_friendlyname(zako_name) };
    if (!devices.empty()) {
      BOOST_LOG(info) << "Available display devices: " << to_string(devices);
      zako_device_id = vdd_device;
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
      timer->setup_timer([this, config_copy = *parsed_config, &session]() {
        if (settings.is_changing_settings_going_to_fail()) {
          BOOST_LOG(warning) << "Applying display settings will fail - retrying later...";
          return false;
        }

        const auto result { settings.apply_config(config_copy, session) };
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

    const auto result { settings.apply_config(*parsed_config, session) };
    if (result) {
      timer->setup_timer(nullptr);
    }
    else {
      restore_state_impl();
    }
  }

  bool
  session_t::create_vdd_monitor() {
    return vdd_utils::create_vdd_monitor();
  }

  bool
  session_t::destroy_vdd_monitor() {
    return vdd_utils::destroy_vdd_monitor();
  }

  void
  session_t::enable_vdd() {
    vdd_utils::enable_vdd();
  }

  void
  session_t::disable_vdd() {
    vdd_utils::disable_vdd();
  }

  void
  session_t::disable_enable_vdd() {
    vdd_utils::disable_enable_vdd();
  }

  bool
  session_t::is_display_on() {
    return vdd_utils::is_display_on();
  }

  void
  session_t::toggle_display_power() {
    vdd_utils::toggle_display_power();
  }

  void
  session_t::prepare_vdd(parsed_config_t &config, const rtsp_stream::launch_session_t &session) {
    auto vdd_settings = vdd_utils::prepare_vdd_settings(config);
    bool should_toggle_vdd = false;

    BOOST_LOG(debug) << "VDD设置状态: 需要更新=" << (vdd_settings.needs_update ? "是" : "否")
                     << ", 新设置=" << (config.resolution ? to_string(*config.resolution) + "@" + to_string(*config.refresh_rate) : "无")
                     << ", 上次VDD设置=" << (last_vdd_setting.empty() ? "无" : last_vdd_setting);

    if (vdd_settings.needs_update && config.resolution) {
      std::string new_setting = to_string(*config.resolution) + "@" + to_string(*config.refresh_rate);
      should_toggle_vdd = (last_vdd_setting != new_setting);

      if (should_toggle_vdd) {
        if (confighttp::saveVddSettings(vdd_settings.resolutions, vdd_settings.fps,
              config::video.adapter_name)) {
          BOOST_LOG(info) << "成功保存VDD设置: " << new_setting;
          last_vdd_setting = new_setting;
        }
        else {
          BOOST_LOG(error) << "保存VDD设置失败，保持原有配置";
          should_toggle_vdd = false;  // 取消后续的驱动重载操作
        }
      }
    }

    if (should_toggle_vdd) {
      BOOST_LOG(info) << "配置已更改，重新加载VDD驱动...";
      vdd_utils::reload_driver();
      std::this_thread::sleep_for(vdd_utils::kVddRetryInterval);
    }

    auto device_zako = display_device::find_device_by_friendlyname(zako_name);
    if (device_zako.empty()) {
      BOOST_LOG(info) << "没有找到VDD设备，开始创建虚拟显示器...";
      create_vdd_monitor();
      std::this_thread::sleep_for(233ms);
    }

    const bool device_found = vdd_utils::retry_with_backoff(
      [&device_zako]() {
        device_zako = display_device::find_device_by_friendlyname(zako_name);
        return !device_zako.empty();
      },
      { .max_attempts = 10,
        .initial_delay = 233ms,
        .max_delay = 1000ms,
        .context = "等待VDD设备初始化" });

    // 失败后处理
    if (!device_found) {
      BOOST_LOG(error) << "VDD设备初始化失败，尝试重置驱动";
      disable_enable_vdd();
      std::this_thread::sleep_for(2s);

      // 统一重试逻辑
      constexpr int max_retries = 3;
      bool final_success = false;

      for (int retry = 1; retry <= max_retries; ++retry) {
        // 创建显示器并检查结果
        BOOST_LOG(info) << "正在执行第" << retry << "次VDD恢复尝试...";
        const bool create_success = create_vdd_monitor();

        if (!create_success) {
          BOOST_LOG(error) << "创建虚拟显示器失败，尝试" << retry << "/" << max_retries;
          if (retry < max_retries) {
            std::this_thread::sleep_for(std::chrono::seconds(1 << retry));  // 指数退避策略：2,4,8秒
            continue;
          }
          break;
        }

        const bool check_success = vdd_utils::retry_with_backoff(
          [&device_zako]() {
            device_zako = display_device::find_device_by_friendlyname(zako_name);
            return !device_zako.empty();
          },
          { .max_attempts = 5,
            .initial_delay = 233ms,
            .max_delay = 2000ms,
            .context = "最终设备检查" });

        if (check_success) {
          BOOST_LOG(info) << "VDD设备恢复成功！";
          final_success = true;
          break;
        }

        BOOST_LOG(error) << "VDD设备检测失败，正在第" << retry << "/" << max_retries << "次重试...";

        if (retry < max_retries) {
          std::this_thread::sleep_for(std::chrono::seconds(1 << retry));  // 指数退避策略
        }
      }

      if (!final_success) {
        BOOST_LOG(error) << "VDD设备最终初始化失败，请检查：\n"
                         << "1. 显卡驱动是否正常\n"
                         << "2. 设备管理器中的虚拟设备状态\n"
                         << "3. 系统事件日志中的相关错误";
        disable_enable_vdd();  // 最终回退操作
      }
    }

    // 更新设备配置
    if (!device_zako.empty()) {
      config.device_id = device_zako;
      config::video.output_name = device_zako;
      BOOST_LOG(info) << "成功配置VDD设备: " << device_zako;
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
    static int retry_count = 0;
    constexpr int max_retries = 5;

    if (!settings.is_changing_settings_going_to_fail() && settings.revert_settings()) {
      timer->setup_timer(nullptr);
      retry_count = 0;
    }
    else {
      if (settings.is_changing_settings_going_to_fail()) {
        BOOST_LOG(warning) << "Reverting display settings will fail - retrying later...";
      }

      timer->setup_timer([this]() {
        if (retry_count >= max_retries) {
          BOOST_LOG(warning) << "Max retry count (" << max_retries << ") reached for reverting display settings";
          return true;
        }

        if (settings.is_changing_settings_going_to_fail()) {
          BOOST_LOG(warning) << "Reverting display settings will still fail - retrying later...";
          retry_count++;
          return false;
        }
        retry_count = 0;
        return settings.revert_settings();
      });
    }
  }

  session_t::session_t():
      timer { std::make_unique<StateRetryTimer>(mutex) },
      vdd_timer { std::make_unique<StateRetryTimer>(mutex, std::chrono::minutes(2)) },
      last_toggle_time { std::chrono::steady_clock::now() },
      debounce_interval { 2000 } {
    // Start vdd timer to check session status
    // vdd_timer->setup_timer([this]() {
    //   if (!display_device::find_device_by_friendlyname(zako_name).empty() && config::video.preferUseVdd && !is_session_active()) {
    //     BOOST_LOG(info) << "No active session detected, disabling VDD";
    //     destroy_vdd_monitor();
    //   }
    //   return false;
    // });
  }

  bool
  session_t::is_session_active() {
    return rtsp_stream::session_count() > 0;
  }

}  // namespace display_device
