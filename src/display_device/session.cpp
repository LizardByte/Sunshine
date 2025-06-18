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
#include "src/platform/windows/display_device/windows_utils.h"

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
    // session_t::get().vdd_timer->setup_timer(nullptr);
    session_t::get().restore_state();
  }

  session_t &
  session_t::get() {
    static session_t session;
    return session;
  }

  std::unique_ptr<session_t::deinit_t>
  session_t::init() {
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
  session_t::update_vdd_resolution(const parsed_config_t &config, const vdd_utils::VddSettings &vdd_settings) {
    const auto new_setting = to_string(*config.resolution) + "@" + to_string(*config.refresh_rate);

    if (last_vdd_setting == new_setting) {
      BOOST_LOG(debug) << "VDD配置未变更: " << new_setting;
      return;
    }

    if (!confighttp::saveVddSettings(vdd_settings.resolutions, vdd_settings.fps, config::video.adapter_name)) {
      BOOST_LOG(error) << "VDD配置保存失败 [resolutions: " << vdd_settings.resolutions << " fps: " << vdd_settings.fps << "]";
      return;
    }

    last_vdd_setting = new_setting;
    BOOST_LOG(info) << "VDD配置更新完成: " << new_setting;

    // 配置变更后执行驱动重载
    BOOST_LOG(info) << "重新加载VDD驱动...";
    vdd_utils::reload_driver();
    std::this_thread::sleep_for(1500ms);
  }

  void
  session_t::prepare_vdd(parsed_config_t &config, const rtsp_stream::launch_session_t &session) {
    auto vdd_settings = vdd_utils::prepare_vdd_settings(config);
    const bool has_new_resolution = vdd_settings.needs_update && config.resolution;

    BOOST_LOG(debug) << "VDD配置状态: needs_update=" << std::boolalpha << vdd_settings.needs_update
                     << ", new_setting=" << (config.resolution ? to_string(*config.resolution) + "@" + to_string(*config.refresh_rate) : "none")
                     << ", last_vdd_setting=" << (last_vdd_setting.empty() ? "none" : last_vdd_setting);

    if (has_new_resolution) update_vdd_resolution(config, vdd_settings);

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
        .initial_delay = 100ms,
        .max_delay = 500ms,
        .context = "等待VDD设备初始化" });

    // 失败后优化处理流程
    if (!device_found) {
      BOOST_LOG(error) << "VDD设备初始化失败，尝试重置驱动";
      disable_enable_vdd();
      std::this_thread::sleep_for(2s);

      for (int retry = 1; retry <= 3; ++retry) {
        BOOST_LOG(info) << "正在执行第" << retry << "次VDD恢复尝试...";

        if (!create_vdd_monitor()) {
          BOOST_LOG(error) << "创建虚拟显示器失败，尝试" << retry << "/3";
          if (retry < 3) {
            std::this_thread::sleep_for(std::chrono::seconds(1 << retry));
            continue;
          }
          break;
        }

        if (vdd_utils::retry_with_backoff(
              [&device_zako]() {
                device_zako = display_device::find_device_by_friendlyname(zako_name);
                return !device_zako.empty();
              },
              { .max_attempts = 5,
                .initial_delay = 233ms,
                .max_delay = 2000ms,
                .context = "最终设备检查" })) {
          BOOST_LOG(info) << "VDD设备恢复成功！";
          break;
        }

        BOOST_LOG(error) << "VDD设备检测失败，正在第" << retry << "/3次重试...";
        if (retry < 3) std::this_thread::sleep_for(std::chrono::seconds(1 << retry));
      }

      if (device_zako.empty()) {
        BOOST_LOG(error) << "VDD设备最终初始化失败，请检查显卡驱动和设备状态";
        disable_enable_vdd();
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
    // 检测RDP会话
    if (w_utils::is_any_rdp_session_active()) {
      BOOST_LOG(info) << "Detected RDP remote session, disabling display settings recovery";
      timer->setup_timer(nullptr); // 禁用定时器
      return;
    }

    if (!settings.is_changing_settings_going_to_fail() && settings.revert_settings()) {
      timer->setup_timer(nullptr);
    }
    else {
      if (settings.is_changing_settings_going_to_fail()) {
        BOOST_LOG(warning) << "Try reverting display settings will fail - retrying later...";
      }

      // 限制重试次数，避免无限循环
      static int retry_count = 0;
      const int max_retries = 20;

      timer->setup_timer([this]() {
        if (settings.is_changing_settings_going_to_fail()) {
          retry_count++;
          if (retry_count >= max_retries) {
            BOOST_LOG(warning) << "已达到最大重试次数，停止尝试恢复显示设置";
            return true; // 返回true停止重试
          }
          BOOST_LOG(warning) << "Timer: Reverting display settings will still fail - retrying later... (Count: " << retry_count << "/" << max_retries << ")";
          return false;
        }

        // 只恢复一次
        auto result = settings.revert_settings();
        BOOST_LOG(info) << "尝试恢复显示设置" << (result ? "成功" : "失败") << "，不再重试";
        return true;
      });
    }
  }

  session_t::session_t():
    timer { std::make_unique<StateRetryTimer>(mutex) } {
  }
}  // namespace display_device
