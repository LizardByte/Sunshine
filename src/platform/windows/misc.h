#ifndef SUNSHINE_WINDOWS_MISC_H
#define SUNSHINE_WINDOWS_MISC_H

#include <string_view>
#include <windows.h>
#include <winnt.h>

namespace platf {
void print_status(const std::string_view &prefix, HRESULT status);
HDESK syncThreadDesktop();
} // namespace platf

#endif