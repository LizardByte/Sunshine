#ifndef SUNSHINE_WINDOWS_MISC_H
#define SUNSHINE_WINDOWS_MISC_H

#include <chrono>
#include <string_view>
#include <windows.h>
#include <winnt.h>

namespace platf {
  void
  print_status(const std::string_view &prefix, HRESULT status);
  HDESK
  syncThreadDesktop();

  int64_t
  qpc_counter();

  std::chrono::nanoseconds
  qpc_time_difference(int64_t performance_counter1, int64_t performance_counter2);
}  // namespace platf

#endif