// standard includes
#include <boost/optional/optional_io.hpp>
#include <boost/process.hpp>
#include <thread>

// local includes
#include "session.h"
#include "src/confighttp.h"
#include "src/globals.h"
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
      // 大多数哔叽本开机默认虚拟屏优先导致黑屏
      // 但是如果只有一个显示器，或者指定使用vdd, 那么就不需要关闭虚拟屏
      if (!vdd_device.empty() && config::video.output_name != vdd_device && devices.size() > 1) {
        session_t::get().disable_vdd();
        std::this_thread::sleep_for(2333ms);
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

  namespace {
    constexpr auto kMaxRetryCount = 3;
    constexpr auto kInitialRetryDelay = 500ms;
    constexpr auto kMaxRetryDelay = 5000ms;

    std::chrono::milliseconds
    calculate_exponential_backoff(int attempt) {
      auto delay = kInitialRetryDelay * (1 << attempt);
      return std::min(delay, kMaxRetryDelay);
    }

    bool
    execute_vdd_command(const std::string &action) {
      static const std::string kDevManPath = "C:\\Program Files\\Sunshine\\tools\\DevManView.exe";
      static const std::string kDriverName = "Virtual Display Driver";

      boost::process::environment _env = boost::this_process::environment();
      auto working_dir = boost::filesystem::path();
      std::error_code ec;

      std::string cmd = kDevManPath + " /" + action + " \"" + kDriverName + "\"";

      for (int attempt = 0; attempt < kMaxRetryCount; ++attempt) {
        auto child = platf::run_command(true, true, cmd, working_dir, _env, nullptr, ec, nullptr);
        if (!ec) {
          BOOST_LOG(info) << "Successfully executed VDD " << action << " command";
          child.detach();
          return true;
        }

        auto delay = calculate_exponential_backoff(attempt);
        BOOST_LOG(warning) << "Failed to execute VDD " << action << " command (attempt "
                           << attempt + 1 << "), retrying in " << delay.count() << "ms";
        std::this_thread::sleep_for(delay);
      }

      BOOST_LOG(error) << "Failed to execute VDD " << action << " command after "
                       << kMaxRetryCount << " attempts";
      return false;
    }

    struct VddSettings {
      std::string resolutions;
      std::string fps;
      bool needs_update;
    };

    VddSettings
    prepare_vdd_settings(const parsed_config_t &config) {
      auto is_res_cached = false;
      auto is_fps_cached = false;
      std::ostringstream res_stream, fps_stream;

      res_stream << '[';
      fps_stream << '[';

      // 检查分辨率是否已缓存
      for (const auto &res : config::nvhttp.resolutions) {
        res_stream << res << ',';
        if (config.resolution && res == to_string(*config.resolution)) {
          is_res_cached = true;
        }
      }

      // 检查帧率是否已缓存
      for (const auto &fps : config::nvhttp.fps) {
        fps_stream << fps << ',';
        if (config.refresh_rate && std::to_string(fps) == to_string(*config.refresh_rate)) {
          is_fps_cached = true;
        }
      }

      // 如果需要更新设置
      bool needs_update = (!is_res_cached || !is_fps_cached) && config.resolution;
      if (needs_update) {
        if (!is_res_cached) {
          res_stream << to_string(*config.resolution);
        }
        if (!is_fps_cached) {
          fps_stream << to_string(*config.refresh_rate);
        }
      }

      // 移除最后的逗号并添加结束括号
      auto res_str = res_stream.str();
      auto fps_str = fps_stream.str();
      if (res_str.back() == ',') res_str.pop_back();
      if (fps_str.back() == ',') fps_str.pop_back();
      res_str += ']';
      fps_str += ']';

      return { res_str, fps_str, needs_update };
    }
  }  // namespace

  void
  session_t::enable_vdd() {
    execute_vdd_command("enable");
  }

  void
  session_t::disable_vdd() {
    execute_vdd_command("disable");
  }

  void
  session_t::disable_enable_vdd() {
    execute_vdd_command("disable_enable");
  }

  void
  session_t::prepare_vdd(parsed_config_t &config, const rtsp_stream::launch_session_t &session) {
    BOOST_LOG(info) << "准备配置VDD，可用分辨率数量: " << config::nvhttp.resolutions.size();

    // 准备VDD设置
    auto vdd_settings = prepare_vdd_settings(config);

    // 检查是否需要切换VDD设置
    bool should_toggle_vdd = false;
    if (vdd_settings.needs_update && config.resolution) {
      std::string new_setting =
        to_string(*config.resolution) + "@" + to_string(*config.refresh_rate);

      BOOST_LOG(info) << "VDD设置 - 当前/新: " << last_vdd_setting << "/" << new_setting;

      should_toggle_vdd = (last_vdd_setting != new_setting);
      if (should_toggle_vdd) {
        confighttp::saveVddSettings(vdd_settings.resolutions, vdd_settings.fps,
          config::video.adapter_name);
        BOOST_LOG(info) << "更新VDD设置为: " << new_setting;
        last_vdd_setting = new_setting;
      }
    }

    bool should_reset_zako_hdr = false;
    auto device_zako = display_device::find_device_by_friendlyname(zako_name);
    if (device_zako.empty()) {
      // 解锁后启动vdd，避免捕获不到流串流黑屏
      if (settings.is_changing_settings_going_to_fail()) {
        std::thread([this]() {
          while (settings.is_changing_settings_going_to_fail()) {
            std::this_thread::sleep_for(777ms);
            BOOST_LOG(warning) << "等待设置解锁以启用VDD...";
          }
          enable_vdd();
          config::video.output_name = zako_device_id;
        }).detach();

        config.device_id = zako_device_id;
        return;
      }
      enable_vdd();
    }
    else if (should_toggle_vdd) {
      disable_enable_vdd();
      std::this_thread::sleep_for(2333ms);
      should_reset_zako_hdr = true;
    }

    // 等待VDD设备就绪
    const int max_retries = 50;
    int retry_count = 0;
    while (device_zako.empty() && retry_count < max_retries) {
      BOOST_LOG(info) << "查找VDD设备重试次数: " << retry_count;
      retry_count += 1;
      device_zako = display_device::find_device_by_friendlyname(zako_name);
      std::this_thread::sleep_for(233ms);
    }

    // 更新设备配置
    if (!device_zako.empty()) {
      config.device_id = device_zako;
      config::video.output_name = device_zako;

      // 解决热切换可能造成的HDR映射异常
      if (should_reset_zako_hdr && session.enable_hdr) {
        std::thread { [this, device_zako]() {
          display_device::set_hdr_states({ { device_zako, hdr_state_e::disabled } });
          BOOST_LOG(info) << "Reset HDR stat for: "sv << device_zako;
          std::this_thread::sleep_for(1s);
          display_device::set_hdr_states({ { device_zako, hdr_state_e::enabled } });
        } }
          .detach();
      }
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
      timer { std::make_unique<StateRetryTimer>(mutex) },
      vdd_timer { std::make_unique<StateRetryTimer>(mutex, std::chrono::minutes(2)) } {
    // Start vdd timer to check session status
    vdd_timer->setup_timer([this]() {
      if (!display_device::find_device_by_friendlyname(zako_name).empty() && config::video.preferUseVdd && !is_session_active()) {
        BOOST_LOG(info) << "No active session detected, disabling VDD";
        disable_vdd();
      }
      return false;  // Keep timer running
    });
  }

  bool
  session_t::is_session_active() {
    std::lock_guard lock { mutex };
    return rtsp_stream::session_count() > 0;
  }

}  // namespace display_device
