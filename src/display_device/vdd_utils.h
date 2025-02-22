#pragma once

#include "config.h"
#include "src/confighttp.h"
#include "to_string.h"
#include <boost/process.hpp>
#include <chrono>
#include <string>
#include <windows.h>

namespace display_device::vdd_utils {
  // 配置常量 (使用inline避免ODR问题)
  inline constexpr auto kMaxRetryCount = 3;
  inline constexpr auto kInitialRetryDelay = 500ms;
  inline constexpr auto kMaxRetryDelay = 5000ms;
  inline constexpr auto kVddRetryInterval = 2333ms;
  inline constexpr DWORD kPipeTimeoutMs = 5000;
  inline constexpr DWORD kPipeBufferSize = 4096;
  inline const wchar_t *kVddPipeName = L"\\\\.\\pipe\\ZakoVDDPipe";

  // RAII句柄包装
  struct HandleGuard {
    HANDLE handle { INVALID_HANDLE_VALUE };
    explicit HandleGuard(HANDLE h = INVALID_HANDLE_VALUE) : handle(h) {}
    ~HandleGuard() { reset(); }
    
    void reset() {
      if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
      }
    }
    operator HANDLE() const { return handle; }
  };

  // 核心接口声明
  std::chrono::milliseconds calculate_exponential_backoff(int attempt);
  bool execute_vdd_command(const std::string &action);
  HANDLE connect_to_pipe_with_retry(const wchar_t *pipe_name, int max_retries = 3);
  bool execute_pipe_command(const wchar_t *pipe_name, const wchar_t *command, std::string *response = nullptr);
  
  struct VddSettings {
    std::string resolutions;
    std::string fps;
    bool needs_update;
  };
  VddSettings prepare_vdd_settings(const parsed_config_t &config);
} // namespace display_device::vdd_utils 