#ifndef SUNSHINE_WINDOWS_MISC_H
#define SUNSHINE_WINDOWS_MISC_H

#include <windows.h>
#include <winnt.h>
#include <string_view>

namespace platf {
void print_status(const std::string_view &prefix, HRESULT status);
HDESK syncThreadDesktop();
}

#endif