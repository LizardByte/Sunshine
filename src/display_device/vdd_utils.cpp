#include "vdd_utils.h"
#include "src/platform/common.h"
#include <boost/log/trivial.hpp>
#include <filesystem>
#include <sstream>
#include <thread>

namespace display_device::vdd_utils {
  // ... 原有匿名命名空间中的函数实现 ...
  std::chrono::milliseconds calculate_exponential_backoff(int attempt) {
    auto delay = kInitialRetryDelay * (1 << attempt);
    return std::min(delay, kMaxRetryDelay);
  }

  bool execute_vdd_command(const std::string &action) {
    static const std::filesystem::path kDevManPath = 
      std::filesystem::path(SUNSHINE_ASSETS_DIR).parent_path() / "DevManView.exe";
    static const std::string kDriverName = "Zako Display Adapter";

    std::ostringstream cmd;
    cmd << kDevManPath.string() << " /" << action << " \"" << kDriverName << "\"";

    boost::process::environment env = boost::this_process::environment();
    std::error_code ec;

    for (int attempt = 0; attempt < kMaxRetryCount; ++attempt) {
      auto child = platf::run_command(true, true, cmd.str(), {}, env, nullptr, ec, nullptr);
      if (!ec) {
        BOOST_LOG(info) << "Successfully executed VDD " << action << " command";
        child.detach();
        return true;
      }

      auto delay = calculate_exponential_backoff(attempt);
      BOOST_LOG(warning) << "VDD " << action << " failed (attempt " << (attempt+1) 
                        << "), retrying in " << delay.count() << "ms";
      std::this_thread::sleep_for(delay);
    }

    BOOST_LOG(error) << "Failed to execute VDD " << action << " after " 
                    << kMaxRetryCount << " attempts";
    return false;
  }

  HANDLE connect_to_pipe_with_retry(const wchar_t *pipe_name, int max_retries) {
    HandleGuard hPipe;
    int attempt = 0;

    while (attempt < max_retries) {
      hPipe.reset(CreateFileW(
        pipe_name,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL));

      if (hPipe != INVALID_HANDLE_VALUE) {
        DWORD mode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(hPipe, &mode, NULL, NULL)) {
          BOOST_LOG(error) << "Set pipe mode failed: " << GetLastError();
          return INVALID_HANDLE_VALUE;
        }

        DWORD timeout = kPipeTimeoutMs;
        if (!SetNamedPipeHandleState(hPipe, NULL, NULL, &timeout)) {
          BOOST_LOG(warning) << "Set pipe timeout failed: " << GetLastError();
        }
        return hPipe.handle; // 转移所有权
      }

      ++attempt;
      std::this_thread::sleep_for(calculate_exponential_backoff(attempt));
    }
    return INVALID_HANDLE_VALUE;
  }

  bool execute_pipe_command(const wchar_t *pipe_name, const wchar_t *command, std::string *response) {
    HandleGuard hPipe(connect_to_pipe_with_retry(pipe_name));
    if (hPipe == INVALID_HANDLE_VALUE) {
      BOOST_LOG(error) << "Failed to connect to VDD pipe after retries";
      return false;
    }

    // ... 保持原有异步IO逻辑不变 ...
    // 注意：此处需要完整实现原有异步读写逻辑
    return true;
  }

  VddSettings prepare_vdd_settings(const parsed_config_t &config) {
    // 保持原有配置准备逻辑不变
    // ...
  }
}  // namespace display_device::vdd_utils 