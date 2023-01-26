#pragma once

#include <string_view>
#include <windows.h>
#include <winnt.h>

namespace platf {
void print_status(const std::string_view &prefix, HRESULT status);
HDESK syncThreadDesktop();
} // namespace platf
