#ifndef SUNSHINE_WINDOWS_MISC_H
#define SUNSHINE_WINDOWS_MISC_H

// This is so that boost process library is forward declared.
#include "src/platform/common.h"
#include <string_view>
#include <windows.h>
#include <winnt.h>

namespace platf {
  void
  print_status(const std::string_view &prefix, HRESULT status);
  HDESK
  syncThreadDesktop();

  boost::process::child
  run_priveleged(const std::string &cmd, boost::filesystem::path &working_dir, boost::process::environment &env, FILE *file, std::error_code &ec, boost::process::group *group);
}  // namespace platf

#endif