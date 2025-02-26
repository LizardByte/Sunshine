#pragma once

// 需要定义WIN32_LEAN_AND_MEAN避免头文件冲突
#define WIN32_LEAN_AND_MEAN

// 头文件包含顺序很重要
#include <chrono>
#include <functional>
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <windows.h>

#include "parsed_config.h"
#include "src/config.h"
#include "src/display_device/display_device.h"

using namespace std::chrono_literals;

namespace display_device {

  namespace vdd_utils {

    // 常量定义
    constexpr auto kMaxRetryCount = 3;
    constexpr auto kInitialRetryDelay = 500ms;
    constexpr auto kMaxRetryDelay = 5000ms;
    extern const wchar_t *kVddPipeName;
    extern const DWORD kPipeTimeoutMs;
    extern const DWORD kPipeBufferSize;
    extern const std::chrono::milliseconds kDefaultDebounceInterval;

    // 指数退避计算
    std::chrono::milliseconds
    calculate_exponential_backoff(int attempt);

    // VDD命令执行
    bool
    execute_vdd_command(const std::string &action);

    // 管道相关函数
    HANDLE
    connect_to_pipe_with_retry(const wchar_t *pipe_name, int max_retries = 3);

    bool
    execute_pipe_command(const wchar_t *pipe_name, const wchar_t *command, std::string *response = nullptr);

    // 驱动重载函数
    bool
    reload_driver();

    // 创建VDD监视器
    bool
    create_vdd_monitor();

    // 销毁VDD监视器
    bool
    destroy_vdd_monitor();

    // 启用VDD驱动
    void
    enable_vdd();

    // 禁用VDD驱动
    void
    disable_vdd();

    // 禁用并重新启用VDD驱动
    void
    disable_enable_vdd();

    // 切换显示器电源状态
    void
    toggle_display_power();

    // 检查显示器是否打开
    bool
    is_display_on();

    // 重试配置结构
    struct RetryConfig {
      int max_attempts;
      std::chrono::milliseconds initial_delay;
      std::chrono::milliseconds max_delay;
      std::string_view context;
    };

    // 重试函数模板
    template <typename Func>
    bool
    retry_with_backoff(Func check_func, const RetryConfig &config) {
      int attempt = 0;
      auto delay = config.initial_delay;

      while (attempt < config.max_attempts) {
        if (check_func()) {
          return true;
        }

        ++attempt;
        if (attempt < config.max_attempts) {
          delay = std::min(config.max_delay, delay * 2);
          std::this_thread::sleep_for(delay);
        }
      }
      return false;
    }

    // VDD设置结构
    struct VddSettings {
      std::string resolutions;
      std::string fps;
      bool needs_update;
    };

    // 准备VDD设置
    VddSettings
    prepare_vdd_settings(const parsed_config_t &config);
  }  // namespace vdd_utils
}  // namespace display_device