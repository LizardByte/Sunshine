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
#include "src/system_tray.h"
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

    constexpr auto kVddRetryInterval = 2333ms;
    const wchar_t *kVddPipeName = L"\\\\.\\pipe\\ZakoVDDPipe";
    const DWORD kPipeTimeoutMs = 5000;
    const DWORD kPipeBufferSize = 4096;

    HANDLE
    connect_to_pipe_with_retry(const wchar_t *pipe_name, int max_retries = 3) {
      HANDLE hPipe = INVALID_HANDLE_VALUE;
      int attempt = 0;
      auto retry_delay = kInitialRetryDelay;

      while (attempt < max_retries) {
        hPipe = CreateFileW(
          pipe_name,
          GENERIC_READ | GENERIC_WRITE,
          0,
          NULL,
          OPEN_EXISTING,
          FILE_FLAG_OVERLAPPED,  // 使用异步IO
          NULL);

        if (hPipe != INVALID_HANDLE_VALUE) {
          DWORD mode = PIPE_READMODE_MESSAGE;
          if (SetNamedPipeHandleState(hPipe, &mode, NULL, NULL)) {
            return hPipe;
          }
          CloseHandle(hPipe);
        }

        ++attempt;
        retry_delay = calculate_exponential_backoff(attempt);
        std::this_thread::sleep_for(retry_delay);
      }
      return INVALID_HANDLE_VALUE;
    }

    bool
    execute_pipe_command(const wchar_t *pipe_name, const wchar_t *command, std::string *response = nullptr) {
      auto hPipe = connect_to_pipe_with_retry(pipe_name);
      if (hPipe == INVALID_HANDLE_VALUE) {
        BOOST_LOG(error) << "连接MTT虚拟显示管道失败，已重试多次";
        return false;
      }

      // 异步IO结构体
      OVERLAPPED overlapped = { 0 };
      overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

      struct HandleGuard {
        HANDLE handle;
        ~HandleGuard() {
          if (handle) CloseHandle(handle);
        }
      } event_guard { overlapped.hEvent };

      // 发送命令（使用宽字符版本）
      DWORD bytesWritten;
      size_t cmd_len = (wcslen(command) + 1) * sizeof(wchar_t);  // 包含终止符
      if (!WriteFile(hPipe, command, (DWORD) cmd_len, &bytesWritten, &overlapped)) {
        if (GetLastError() != ERROR_IO_PENDING) {
          BOOST_LOG(error) << L"发送" << command << L"命令失败，错误代码: " << GetLastError();
          return false;
        }

        // 等待写入完成
        DWORD waitResult = WaitForSingleObject(overlapped.hEvent, kPipeTimeoutMs);
        if (waitResult != WAIT_OBJECT_0) {
          BOOST_LOG(error) << L"发送" << command << L"命令超时";
          return false;
        }
      }

      // 读取响应
      if (response) {
        char buffer[kPipeBufferSize];
        DWORD bytesRead;
        if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, &overlapped)) {
          if (GetLastError() != ERROR_IO_PENDING) {
            BOOST_LOG(warning) << "读取响应失败，错误代码: " << GetLastError();
            return false;
          }

          DWORD waitResult = WaitForSingleObject(overlapped.hEvent, kPipeTimeoutMs);
          if (waitResult == WAIT_OBJECT_0 && GetOverlappedResult(hPipe, &overlapped, &bytesRead, FALSE)) {
            buffer[bytesRead] = '\0';
            *response = std::string(buffer, bytesRead);
          }
        }
      }

      return true;
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

  bool
  session_t::create_vdd_monitor() {
    std::string response;
    if (!execute_pipe_command(kVddPipeName, L"CREATEMONITOR", &response)) {
      BOOST_LOG(error) << "创建虚拟显示器失败";
      return false;
    }
#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
    system_tray::update_tray_vmonitor_checked(1);
#endif
    BOOST_LOG(info) << "创建虚拟显示器完成，响应: " << response;
    return true;
  }

  bool
  session_t::destroy_vdd_monitor() {
    std::string response;
    if (!execute_pipe_command(kVddPipeName, L"DESTROYMONITOR", &response)) {
      BOOST_LOG(error) << "销毁虚拟显示器失败";
      return false;
    }

#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
    system_tray::update_tray_vmonitor_checked(0);
#endif

    BOOST_LOG(info) << "销毁虚拟显示器完成，响应: " << response;
    return true;
  }

  bool
  reload_driver() {
    std::string response;
    if (!execute_pipe_command(kVddPipeName, L"RELOAD_DRIVER", &response)) {
      return false;
    }
    return true;

    // if (response != "FinishInit") {
    //   BOOST_LOG(error) << "驱动重载异常，响应: " << response;
    //   return false;
    // }

    // BOOST_LOG(info) << "开始创建虚拟显示器";
    // return session_t::get().create_vdd_monitor();
  }

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
    reload_driver();
  }

  bool
  session_t::is_display_on() {
    std::lock_guard lock { mutex };
    return !display_device::find_device_by_friendlyname(zako_name).empty();
  }

  std::chrono::steady_clock::time_point last_toggle_time;
  std::chrono::milliseconds debounce_interval { 5000 };  // 5000毫秒防抖间隔

  void
  session_t::toggle_display_power() {
    auto now = std::chrono::steady_clock::now();

    if (now - last_toggle_time < debounce_interval) {
      BOOST_LOG(debug) << "忽略快速重复的显示器开关请求";
      return;
    }

    last_toggle_time = now;

    if (!is_display_on()) {
      if (create_vdd_monitor()) {
        // 启动新线程处理确认弹窗和超时
        std::thread([this]() {
          // Windows弹窗确认
          auto future = std::async(std::launch::async, []() {
            return MessageBoxW(nullptr,
                     L"已创建虚拟显示器，是否继续使用？",
                     L"显示器确认",
                     MB_YESNO | MB_ICONQUESTION) == IDYES;
          });

          // 等待20秒超时
          if (future.wait_for(20s) != std::future_status::ready || !future.get()) {
            BOOST_LOG(info) << "用户未确认或超时，自动销毁虚拟显示器";
            // 强制关闭消息框
            HWND hwnd = GetActiveWindow();
            if (hwnd && IsWindow(hwnd)) {
              PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            destroy_vdd_monitor();
          }
        }).detach();  // 分离线程自动管理
      }
    }
    else {
      destroy_vdd_monitor();
    }
  }

  void
  session_t::prepare_vdd(parsed_config_t &config, const rtsp_stream::launch_session_t &session) {
    auto vdd_settings = prepare_vdd_settings(config);
    bool should_toggle_vdd = false;

    if (vdd_settings.needs_update && config.resolution) {
      std::string new_setting = to_string(*config.resolution) + "@" + to_string(*config.refresh_rate);
      should_toggle_vdd = (last_vdd_setting != new_setting);

      if (should_toggle_vdd) {
        confighttp::saveVddSettings(vdd_settings.resolutions, vdd_settings.fps,
          config::video.adapter_name);
        last_vdd_setting = new_setting;
      }
    }

    if (should_toggle_vdd) {
      disable_enable_vdd();
      std::this_thread::sleep_for(kVddRetryInterval);
    }

    auto device_zako = display_device::find_device_by_friendlyname(zako_name);
    if (device_zako.empty()) {
      create_vdd_monitor();
      std::this_thread::sleep_for(233ms);
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
