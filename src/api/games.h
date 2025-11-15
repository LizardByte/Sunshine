#pragma once
#include <string>

namespace sunshine {
namespace api {

std::string get_detected_games();
std::string get_platform_games(const std::string& platform);
std::string get_available_platforms();

} // namespace api
} // namespace sunshine
