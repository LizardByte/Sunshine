#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <unordered_map>

#include <boost/asio.hpp>

#include "config.h"
#include "main.h"
#include "utility.h"

#include "platform/common.h"

namespace fs = std::filesystem;
using namespace std::literals;

#define CA_DIR "credentials"
#define PRIVATE_KEY_FILE CA_DIR "/cakey.pem"
#define CERTIFICATE_FILE CA_DIR "/cacert.pem"

#define APPS_JSON_PATH SUNSHINE_CONFIG_DIR "/apps.json"
namespace config {

std::string_view to_config_prop_string(config_props propType) {
  switch(propType) {
  case INT:
    return "int"sv;
  case DOUBLE:
    return "double"sv;
  case STRING:
    return "string"sv;
  case INT_ARRAY:
    return "int_array"sv;
  case STRING_ARRAY:
    return "string_array"sv;
  case FILE:
    return "file"sv;
  case BOOLEAN:
    return "boolean"sv;
  }

  // avoid warning
  return "custom"sv;
}

std::unordered_map<std::string, std::pair<config_prop, limit>> property_schema = {
  { "qp"s, { config_prop(INT, "qp"s, ""s, true, &video.qp), no_limit() } },
  { "min_threads"s, { config_prop(INT, "min_threads"s, "Minimum number of threads used by ffmpeg to encode the video."s, true, &video.min_threads), no_limit() } },
  { "hevc_mode"s, { config_prop(INT, "hevc_mode"s, "Allows the client to request HEVC Main or HEVC Main10 video streams."s, true, &video.hevc_mode), string_limit({ "0", "1", "2", "3" }) } },
  { "sw_preset"s, { config_prop(STRING, "sw_preset"s, "Software encoding preset"s, true, &video.sw.preset), no_limit() } },
  { "sw_tune"s, { config_prop(STRING, "sw_tune"s, "Software encoding tuning parameters"s, true, &video.sw.tune), no_limit() } },
  { "nv_preset"s, { config_prop(INT, "nv_preset"s, "NVENC preset"s, true), no_limit() } },
  { "nv_rc"s, { config_prop(INT, "nv_rc"s, "NVENC rate control"s, true), no_limit() } },
  { "nv_coder"s, { config_prop(INT, "nv_coder"s, "NVENC Coder"s, true, &video.nv.coder), string_limit({ "auto", "cabac", "cavlc" }) } },
  { "amd_quality"s, { config_prop(INT, "amd_quality"s, "AMD AMF quality"s, true, &video.amd.quality), string_limit({ "default", "speed", "balanced" }) } },
  { "amd_rc"s, { config_prop(STRING, "amd_rc"s, "AMD AMF rate control"s, true), string_limit({ "constqp", "vbr_latency", "vbr_peak", "cbr" }) } },
  { "amd_coder"s, { config_prop(INT, "amd_coder"s, ""s, true, &video.amd.coder), string_limit({ "auto", "cabac", "cavlc" }) } },
  { "encoder"s, { config_prop(STRING, "encoder"s, "Force a specific encoder"s, true, &video.encoder), string_limit({ "nvenc", "amdvce", "vaapi", "software" }) } },
  { "adapter_name"s, { config_prop(STRING, "adapter_name"s, "Select the video card you want to stream with."s, true, &video.adapter_name), no_limit() } },
  { "output_name"s, { config_prop(STRING, "output_name"s, "Select the display output you want to stream."s, true, &video.output_name), no_limit() } },
  { "pkey"s, { config_prop(FILE, "pkey"s, "Path to the private key file. The private key must be 2048 bits."s, true, &nvhttp.pkey), no_limit() } },
  { "cert"s, { config_prop(STRING, "cert"s, "Path to the certificate file. The certificate must be signed with a 2048 bit key!"s, true, &nvhttp.cert), no_limit() } },
  { "sunshine_name"s, { config_prop(STRING, "sunshine_name"s, "The name displayed by Moonlight. If not specified, the PC's hostname is used"s, true, &nvhttp.sunshine_name), no_limit() } },
  { "file_state"s, { config_prop(FILE, "file_state"s, "The file where current state of Sunshine is stored."s, true, &nvhttp.file_state), no_limit() } },
  { "external_ip"s, { config_prop(STRING, "external_ip"s, ""s, true, &nvhttp.external_ip), no_limit() } },
  { "resolutions"s, { config_prop(STRING_ARRAY, "resolutions"s, "Display resolutions reported by Sunshine as supported."s, true, &nvhttp.resolutions), no_limit() } },
  { "fps"s, { config_prop(INT_ARRAY, "fps"s, "Supported FPS reported to clients"s, true, &nvhttp.fps), no_limit() } },
  { "audio_sink"s, { config_prop(STRING, "audio_sink"s, "The name of the audio sink used for Audio Loopback."s, true, &audio.sink), no_limit() } },
  { "virtual_sink"s, { config_prop(STRING, "virtual_sink"s, "Virtual audio device name (like Steam Streaming Speakers)\r\nAllows Sunshine to stream audio with muted speakers."s, true, &audio.virtual_sink), no_limit() } },
  { "origin_pin_allowed"s, { config_prop(STRING, "origin_pin_allowed"s, ""s, true, &nvhttp.origin_pin_allowed), no_limit() } },
  { "ping_timeout"s, { config_prop(INT, "ping_timeout"s, "How long to wait (in milliseconds) for data from Moonlight clients before shutting down the stream"s, true, &stream.ping_timeout), no_limit() } },
  { "channels"s, { config_prop(INT, "channels"s, ""s, true, &video.qp), minmax_limit(1, 24) } },
  { "file_apps"s, { config_prop(FILE, "file_apps"s, "Path to apps.json which contains all the necessary configuration for running apps in Sunshine."s, true, &stream.file_apps), no_limit() } },
  { "fec_percentage"s, { config_prop(INT, "fec_percentage"s, "Percentage of error correcting packets per data packet in each video frame."s, true, &stream.fec_percentage), no_limit() } },
  { "keybindings"s, { config_prop(STRING_ARRAY, "keybindings"s, ""s, true), no_limit() } },
  { "key_rightalt_to_key_win"s, { config_prop(BOOLEAN, "key_rightalt_to_key_win"s, "It may be possible that you cannot send the Windows key from Moonlight directly. \r\n Allows using Right Alt key as the Windows key."s, true), no_limit() } },
  { "back_button_timeout"s, { config_prop(INT, "back_button_timeout"s, "Emulate back/select button press on the controller."s, true, &input.back_button_timeout), minmax_limit(-1, std::numeric_limits<int>::max()) } },
  { "key_repeat_frequency"s, { config_prop(DOUBLE, "key_repeat_frequency"s, "How often keys repeat every second after delay.\r\nThis configurable option supports decimals"s, true), no_limit() } },
  { "key_repeat_delay"s, { config_prop(INT, "key_repeat_delay"s, "Controls how fast keys will repeat themselves after holding\r\nThe initial delay in milliseconds before repeating keys."s, true, &input.key_repeat_delay), minmax_limit(-1, std::numeric_limits<int>::max()) } },
  { "gamepad"s, { config_prop(STRING, "gamepad"s, "Default gamepad used"s, true, &input.gamepad), string_limit(platf::supported_gamepads()) } },
  { "port"s, { config_prop(INT, "port"s, ""s, true), minmax_limit(1024, 49151) } },
  { "upnp"s, { config_prop(BOOLEAN, "upnp"s, "Automatically configure port forwarding"s, true), no_limit() } },
  { "dwmflush"s, { config_prop(BOOLEAN, "dwmflush"s, "Improves capture latency during mouse movement.\r\nEnabling this may prevent the client's FPS from exceeding the host monitor's active refresh rate."s, false, &video.dwmflush), no_limit() } },
  { "min_log_level"s, { config_prop(STRING, "min_log_level"s, "The minimum log level printed to standard out"s, true), string_limit({ "verbose", "debug", "info", "warning", "error", "fatal", "none", "0", "1", "2", "3", "4", "5", "6" }) } },
};

namespace nv {
enum preset_e : int {
  _default = 0,
  slow,
  medium,
  fast,
  hp,
  hq,
  bd,
  ll_default,
  llhq,
  llhp,
  lossless_default, // lossless presets must be the last ones
  lossless_hp,
};

enum rc_e : int {
  constqp   = 0x0,  /**< Constant QP mode */
  vbr       = 0x1,  /**< Variable bitrate mode */
  cbr       = 0x2,  /**< Constant bitrate mode */
  cbr_ld_hq = 0x8,  /**< low-delay CBR, high quality */
  cbr_hq    = 0x10, /**< CBR, high quality (slower) */
  vbr_hq    = 0x20  /**< VBR, high quality (slower) */
};

enum coder_e : int {
  _auto = 0,
  cabac,
  cavlc
};

std::optional<preset_e> preset_from_view(const std::string_view &preset) {
#define _CONVERT_(x) \
  if(preset == #x##sv) return x
  _CONVERT_(slow);
  _CONVERT_(medium);
  _CONVERT_(fast);
  _CONVERT_(hp);
  _CONVERT_(bd);
  _CONVERT_(ll_default);
  _CONVERT_(llhq);
  _CONVERT_(llhp);
  _CONVERT_(lossless_default);
  _CONVERT_(lossless_hp);
  if(preset == "default"sv) return _default;
#undef _CONVERT_
  return std::nullopt;
}

std::optional<rc_e> rc_from_view(const std::string_view &rc) {
#define _CONVERT_(x) \
  if(rc == #x##sv) return x
  _CONVERT_(constqp);
  _CONVERT_(vbr);
  _CONVERT_(cbr);
  _CONVERT_(cbr_hq);
  _CONVERT_(vbr_hq);
  _CONVERT_(cbr_ld_hq);
#undef _CONVERT_
  return std::nullopt;
}

int coder_from_view(const std::string_view &coder) {
  if(coder == "auto"sv) return _auto;
  if(coder == "cabac"sv || coder == "ac"sv) return cabac;
  if(coder == "cavlc"sv || coder == "vlc"sv) return cavlc;

  return -1;
}
} // namespace nv

namespace amd {
enum quality_e : int {
  _default = 0,
  speed,
  balanced,
};

enum class rc_hevc_e : int {
  constqp,     /**< Constant QP mode */
  vbr_latency, /**< Latency Constrained Variable Bitrate */
  vbr_peak,    /**< Peak Contrained Variable Bitrate */
  cbr,         /**< Constant bitrate mode */
};

enum class rc_h264_e : int {
  constqp,     /**< Constant QP mode */
  cbr,         /**< Constant bitrate mode */
  vbr_peak,    /**< Peak Contrained Variable Bitrate */
  vbr_latency, /**< Latency Constrained Variable Bitrate */
};

enum coder_e : int {
  _auto = 0,
  cabac,
  cavlc
};

std::optional<quality_e> quality_from_view(const std::string_view &quality) {
#define _CONVERT_(x) \
  if(quality == #x##sv) return x
  _CONVERT_(speed);
  _CONVERT_(balanced);
  if(quality == "default"sv) return _default;
#undef _CONVERT_
  return std::nullopt;
}

std::optional<int> rc_h264_from_view(const std::string_view &rc) {
#define _CONVERT_(x) \
  if(rc == #x##sv) return (int)rc_h264_e::x
  _CONVERT_(constqp);
  _CONVERT_(vbr_latency);
  _CONVERT_(vbr_peak);
  _CONVERT_(cbr);
#undef _CONVERT_
  return std::nullopt;
}

std::optional<int> rc_hevc_from_view(const std::string_view &rc) {
#define _CONVERT_(x) \
  if(rc == #x##sv) return (int)rc_hevc_e::x
  _CONVERT_(constqp);
  _CONVERT_(vbr_latency);
  _CONVERT_(vbr_peak);
  _CONVERT_(cbr);
#undef _CONVERT_
  return std::nullopt;
}

int coder_from_view(const std::string_view &coder) {
  if(coder == "auto"sv) return _auto;
  if(coder == "cabac"sv || coder == "ac"sv) return cabac;
  if(coder == "cavlc"sv || coder == "vlc"sv) return cavlc;

  return -1;
}
} // namespace amd

namespace vt {

enum coder_e : int {
  _auto = 0,
  cabac,
  cavlc
};

int coder_from_view(const std::string_view &coder) {
  if(coder == "auto"sv) return _auto;
  if(coder == "cabac"sv || coder == "ac"sv) return cabac;
  if(coder == "cavlc"sv || coder == "vlc"sv) return cavlc;

  return -1;
}

int allow_software_from_view(const std::string_view &software) {
  if(software == "allowed"sv || software == "forced") return 1;

  return 0;
}

int force_software_from_view(const std::string_view &software) {
  if(software == "forced") return 1;

  return 0;
}

int rt_from_view(const std::string_view &rt) {
  if(rt == "disabled" || rt == "off" || rt == "0") return 0;

  return 1;
}

} // namespace vt

video_t video {
  28, // qp

  0, // hevc_mode

  1, // min_threads
  {
    "superfast"s,   // preset
    "zerolatency"s, // tune
  },                // software

  {
    nv::llhq,
    std::nullopt,
    -1 }, // nv

  {
    amd::balanced,
    std::nullopt,
    std::nullopt,
    -1 }, // amd

  {
    0,
    0,
    1,
    -1 }, // vt

  {},  // encoder
  {},  // adapter_name
  {},  // output_name
  true // dwmflush
};

audio_t audio {};

stream_t stream {
  10s, // ping_timeout

  APPS_JSON_PATH,

  20, // fecPercentage
  1   // channels
};

nvhttp_t nvhttp {
  "pc",  // origin_pin
  "lan", // origin web manager

  PRIVATE_KEY_FILE,
  CERTIFICATE_FILE,

  boost::asio::ip::host_name(), // sunshine_name,
  "sunshine_state.json"s,       // file_state
  {},                           // external_ip
  {
    "352x240"s,
    "480x360"s,
    "858x480"s,
    "1280x720"s,
    "1920x1080"s,
    "2560x1080"s,
    "3440x1440"s
    "1920x1200"s,
    "3860x2160"s,
    "3840x1600"s,
  }, // supported resolutions

  { 10, 30, 60, 90, 120 }, // supported fps
};

input_t input {
  {
    { 0x10, 0xA0 },
    { 0x11, 0xA2 },
    { 0x12, 0xA4 },
  },
  2s,                                         // back_button_timeout
  500ms,                                      // key_repeat_delay
  std::chrono::duration<double> { 1 / 24.9 }, // key_repeat_period

  {
    platf::supported_gamepads().front().data(),
    platf::supported_gamepads().front().size(),
  }, // Default gamepad
};

sunshine_t sunshine {
  2,                                          // min_log_level
  0,                                          // flags
  SUNSHINE_CONFIG_DIR "/sunshine_creds.data", // User file
  3660s,                                      // Token lifetime in seconds
  SUNSHINE_CONFIG_DIR "/sunshine.conf",       // config file
  {},                                         // cmd args
  47989,
};

bool endline(char ch) {
  return ch == '\r' || ch == '\n';
}

bool space_tab(char ch) {
  return ch == ' ' || ch == '\t';
}

bool whitespace(char ch) {
  return space_tab(ch) || endline(ch);
}

std::string to_string(const char *begin, const char *end) {
  std::string result;

  KITTY_WHILE_LOOP(auto pos = begin, pos != end, {
    auto comment = std::find(pos, end, '#');
    auto endl    = std::find_if(comment, end, endline);

    result.append(pos, comment);

    pos = endl;
  })

  return result;
}

template<class It>
It skip_list(It skipper, It end) {
  int stack = 1;
  while(skipper != end && stack) {
    if(*skipper == '[') {
      ++stack;
    }
    if(*skipper == ']') {
      --stack;
    }

    ++skipper;
  }

  return skipper;
}

std::pair<
  std::string_view::const_iterator,
  std::optional<std::pair<std::string, std::string>>>
parse_option(std::string_view::const_iterator begin, std::string_view::const_iterator end) {
  begin     = std::find_if_not(begin, end, whitespace);
  auto endl = std::find_if(begin, end, endline);
  auto endc = std::find(begin, endl, '#');
  endc      = std::find_if(std::make_reverse_iterator(endc), std::make_reverse_iterator(begin), std::not_fn(whitespace)).base();

  auto eq = std::find(begin, endc, '=');
  if(eq == endc || eq == begin) {
    return std::make_pair(endl, std::nullopt);
  }

  auto end_name  = std::find_if_not(std::make_reverse_iterator(eq), std::make_reverse_iterator(begin), space_tab).base();
  auto begin_val = std::find_if_not(eq + 1, endc, space_tab);

  if(begin_val == endl) {
    return std::make_pair(endl, std::nullopt);
  }

  // Lists might contain newlines
  if(*begin_val == '[') {
    endl = skip_list(begin_val + 1, end);
    if(endl == end) {
      std::cout << "Warning: Config option ["sv << to_string(begin, end_name) << "] Missing ']'"sv;

      return std::make_pair(endl, std::nullopt);
    }
  }

  return std::make_pair(
    endl,
    std::make_pair(to_string(begin, end_name), to_string(begin_val, endl)));
}

std::unordered_map<std::string, std::string> parse_config(const std::string_view &file_content) {
  std::unordered_map<std::string, std::string> vars;

  auto pos = std::begin(file_content);
  auto end = std::end(file_content);

  while(pos < end) {
    // auto newline = std::find_if(pos, end, [](auto ch) { return ch == '\n' || ch == '\r'; });
    TUPLE_2D(endl, var, parse_option(pos, end));

    pos = endl;
    if(pos != end) {
      pos += (*pos == '\r') ? 2 : 1;
    }

    if(!var) {
      continue;
    }

    vars.emplace(std::move(*var));
  }

  return vars;
}

void path_f(std::string val, fs::path &input) {
  // appdata needs to be retrieved once only
  static auto appdata = platf::appdata();

  if(!val.empty()) {
    input = val;
  }

  if(input.is_relative()) {
    input = appdata / input;
  }

  auto dir = input;
  dir.remove_filename();

  // Ensure the directories exists
  if(!fs::exists(dir)) {
    fs::create_directories(dir);
  }
}
void path_f(std::string val, std::string &input) {
  fs::path temp = input;

  path_f(val, temp);

  input = temp.string();
}

void int_f(std::string val, int &input) {

  // If value is something like: "756" instead of 756
  if(val.size() >= 2 && val[0] == '"') {
    val = val.substr(1, val.size() - 2);
  }

  // If that integer is in hexadecimal
  if(val.size() >= 2 && val.substr(0, 2) == "0x"sv) {
    input = util::from_hex<int>(val.substr(2));
  }
  else {
    input = util::from_view(val);
  }
}

bool to_bool(std::string &boolean) {
  std::for_each(std::begin(boolean), std::end(boolean), [](char ch) { return (char)std::tolower(ch); });

  return boolean == "true"sv ||
         boolean == "yes"sv ||
         boolean == "enable"sv ||
         boolean == "enabled"sv ||
         boolean == "on"sv ||
         (std::find(std::begin(boolean), std::end(boolean), '1') != std::end(boolean));
}

void bool_f(std::string val, bool &input) {
  input = to_bool(val);
}

void double_f(std::string value, double &input) {

  if(value.empty()) {
    return;
  }

  char *c_str_p;
  auto val = std::strtod(value.c_str(), &c_str_p);

  if(c_str_p == value.c_str()) {
    return;
  }

  input = val;
}

void double_between_f(std::string value, double &input, const std::pair<double, double> &range) {
  double temp = input;

  double_f(value, temp);

  TUPLE_2D_REF(lower, upper, range);
  if(temp >= lower && temp <= upper) {
    input = temp;
  }
}

void list_string_f(std::string string, std::vector<std::string> &input) {

  if(string.empty()) {
    return;
  }

  input.clear();

  auto begin = std::cbegin(string);
  if(*begin == '[') {
    ++begin;
  }

  begin = std::find_if_not(begin, std::cend(string), whitespace);
  if(begin == std::cend(string)) {
    return;
  }

  auto pos = begin;
  while(pos < std::cend(string)) {
    if(*pos == '[') {
      pos = skip_list(pos + 1, std::cend(string)) + 1;
    }
    else if(*pos == ']') {
      break;
    }
    else if(*pos == ',') {
      input.emplace_back(begin, pos);
      pos = begin = std::find_if_not(pos + 1, std::cend(string), whitespace);
    }
    else {
      ++pos;
    }
  }

  if(pos != begin) {
    input.emplace_back(begin, pos);
  }
}

void list_int_f(std::string val, std::vector<int> &input) {
  std::vector<std::string> list;
  list_string_f(val, list);

  for(auto &el : list) {
    std::string_view val = el;

    // If value is something like: "756" instead of 756
    if(val.size() >= 2 && val[0] == '"') {
      val = val.substr(1, val.size() - 2);
    }

    int tmp;

    // If the integer is a hexadecimal
    if(val.size() >= 2 && val.substr(0, 2) == "0x"sv) {
      tmp = util::from_hex<int>(val.substr(2));
    }
    else {
      tmp = util::from_view(val);
    }
    input.emplace_back(tmp);
  }
}

void map_int_int_f(std::string val, std::unordered_map<int, int> &input) {
  std::vector<int> list;
  list_int_f(val, list);

  // The list needs to be a multiple of 2
  if(list.size() % 2) {
    return;
  }

  int x = 0;
  while(x < list.size()) {
    auto key = list[x++];
    auto val = list[x++];

    input.emplace(key, val);
  }
}

int apply_flags(const char *line) {
  int ret = 0;
  while(*line != '\0') {
    switch(*line) {
    case '0':
      config::sunshine.flags[config::flag::PIN_STDIN].flip();
      break;
    case '1':
      config::sunshine.flags[config::flag::FRESH_STATE].flip();
      break;
    case '2':
      config::sunshine.flags[config::flag::FORCE_VIDEO_HEADER_REPLACE].flip();
      break;
    case 'p':
      config::sunshine.flags[config::flag::UPNP].flip();
      break;
    default:
      std::cout << "Warning: Unrecognized flag: ["sv << *line << ']' << std::endl;
      ret = -1;
    }

    ++line;
  }

  return ret;
}

void set_min_log_level(std::string log_level_string) {
  if(!log_level_string.empty()) {
    if(log_level_string == "verbose"sv) {
      sunshine.min_log_level = 0;
    }
    else if(log_level_string == "debug"sv) {
      sunshine.min_log_level = 1;
    }
    else if(log_level_string == "info"sv) {
      sunshine.min_log_level = 2;
    }
    else if(log_level_string == "warning"sv) {
      sunshine.min_log_level = 3;
    }
    else if(log_level_string == "error"sv) {
      sunshine.min_log_level = 4;
    }
    else if(log_level_string == "fatal"sv) {
      sunshine.min_log_level = 5;
    }
    else if(log_level_string == "none"sv) {
      sunshine.min_log_level = 6;
    }
    else {
      // accept digit directly
      auto val = log_level_string[0];
      if(val >= '0' && val < '7') {
        sunshine.min_log_level = val - '0';
      }
    }
  }
}

void save_config(std::unordered_map<std::string, std::string> &&vars) {
  std::stringstream configStream;

  for(auto &[name, val] : vars) {
    auto it = property_schema.find(name);
    if(it == property_schema.end()) {
      BOOST_LOG(warning) << "Tried to set property " << name << " but property doesn\'t exist.";
      continue;
    }
    if(val.empty()) {
      BOOST_LOG(warning) << "Property" << name << "has empty value. Skipping.";
      continue;
    }

    config_prop any_prop = it->second.first;

    bool isValidValue = it->second.second->check(val);
    if(isValidValue == false) {
      BOOST_LOG(error) << "Property" << name << "has invalid value. Skipping.";
      continue;
    }
    switch(any_prop.prop_type) {
    case STRING:
      isValidValue = true;
      break;
    case FILE: {
      std::string temp_file = "";
      path_f(val, temp_file);
      isValidValue = temp_file != "";
      break;
    }
    case INT: {
      int temp_int = std::numeric_limits<int>::max();
      int_f(val, temp_int);
      isValidValue = temp_int != std::numeric_limits<int>::max();
      break;
    }
    case INT_ARRAY: {
      std::vector<int> temp_int_vec;
      list_int_f(val, temp_int_vec);
      isValidValue = temp_int_vec.size() > 0;
      break;
    }
    case STRING_ARRAY: {
      std::vector<std::string> temp_str_vec;
      list_string_f(val, temp_str_vec);
      isValidValue = temp_str_vec.size() > 0;
      break;
    }
    case BOOLEAN: {
      bool temp_bool = false;
      bool_f(val, temp_bool);
      isValidValue = temp_bool;
      break;
    }
    case DOUBLE: {
      double temp_double = std::numeric_limits<double>::max();
      double_f(val, temp_double);
      isValidValue = temp_double != std::numeric_limits<double>::max();
      break;
    }
    }

    if(isValidValue) {
      configStream << name << " = " << val << std::endl;
    }
  }

  write_file(config::sunshine.config_file.c_str(), configStream.str());
}

void handle_custom_config_prop(config_prop prop, std::string val) {
  if(prop.name == "min_log_level") {
    set_min_log_level(val);
  }
  else if(prop.name == "amd_rc") {
    video.amd.rc_h264 = amd::rc_h264_from_view(val);
    video.amd.rc_hevc = amd::rc_hevc_from_view(val);
  }
  else if(prop.name == "keybindings") {
    map_int_int_f(val, input.keybindings);
  }
  else if(prop.name == "key_repeat_frequency") {
    double repeat_frequency { 0 };
    double_between_f(val, repeat_frequency, { 0, std::numeric_limits<double>::max() });

    if(repeat_frequency > 0) {
      config::input.key_repeat_period = std::chrono::duration<double> { 1 / repeat_frequency };
    }
  }
  else if(prop.name == "key_rightalt_to_key_win") {
    bool map_rightalt_to_win = false;
    bool_f(val, map_rightalt_to_win);

    if(map_rightalt_to_win) {
      input.keybindings.emplace(0xA5, 0x5B);
    }
  }
  else if(prop.name == "port") {
    int port = sunshine.port;
    int_f(val, port);
    sunshine.port = (std::uint16_t)port;
  }
  else if(prop.name == "upnp") {
    bool upnp = false;
    bool_f(val, upnp);

    if(upnp) {
      config::sunshine.flags[config::flag::UPNP].flip();
    }
  }
}

void apply_config(std::unordered_map<std::string, std::string> &&vars) {
  if(!fs::exists(stream.file_apps.c_str())) {
    fs::copy_file(SUNSHINE_CONFIG_DIR "/apps.json", stream.file_apps);
  }

  for(auto &[name, val] : vars) {
    std::cout << "["sv << name << "] -- ["sv << val << ']' << std::endl;
    auto it = property_schema.find(name);
    if(it == property_schema.end()) {
      BOOST_LOG(warning) << "Tried to set property " << name << " but property doesn\'t exist.";
      continue;
    }
    if(val.empty()) {
      BOOST_LOG(warning) << "Property" << name << "has empty value. Skipping.";
      continue;
    }

    config_prop any_prop = it->second.first;
    if(any_prop.value != NULL) {
      switch(any_prop.prop_type) {
      case STRING:
        *reinterpret_cast<std::string *>(any_prop.value) = val;
        break;
      case FILE:
        path_f(val, *reinterpret_cast<std::string *>(any_prop.value));
        break;
      case INT:
        int_f(val, *reinterpret_cast<int *>(any_prop.value));
        break;
      case INT_ARRAY:
        list_int_f(val, *reinterpret_cast<std::vector<int> *>(any_prop.value));
        break;
      case STRING_ARRAY:
        list_string_f(val, *reinterpret_cast<std::vector<std::string> *>(any_prop.value));
        break;
      case BOOLEAN:
        bool_f(val, *reinterpret_cast<bool *>(any_prop.value));
        break;
      case DOUBLE:
        double_f(val, *reinterpret_cast<double *>(any_prop.value));
        break;
      }
    }
    else {
      handle_custom_config_prop(any_prop, val);
    }
  }
  auto it = vars.find("flags"s);
  if(it != std::end(vars)) {
    apply_flags(it->second.c_str());
  }
}

int parse(int argc, char *argv[]) {
  std::unordered_map<std::string, std::string> cmd_vars;

  for(auto x = 1; x < argc; ++x) {
    auto line = argv[x];

    if(line == "--help"sv) {
      print_help(*argv);
      return 1;
    }
    else if(*line == '-') {
      if(*(line + 1) == '-') {
        sunshine.cmd.name = line + 2;
        sunshine.cmd.argc = argc - x - 1;
        sunshine.cmd.argv = argv + x + 1;

        break;
      }
      if(apply_flags(line + 1)) {
        print_help(*argv);
        return -1;
      }
    }
    else {
      auto line_end = line + strlen(line);

      auto pos = std::find(line, line_end, '=');
      if(pos == line_end) {
        sunshine.config_file = line;
      }
      else {
        TUPLE_EL(var, 1, parse_option(line, line_end));
        if(!var) {
          print_help(*argv);
          return -1;
        }

        TUPLE_EL_REF(name, 0, *var);

        auto it = cmd_vars.find(name);
        if(it != std::end(cmd_vars)) {
          cmd_vars.erase(it);
        }

        cmd_vars.emplace(std::move(*var));
      }
    }
  }

  if(!fs::exists(sunshine.config_file)) {
    fs::copy_file(SUNSHINE_CONFIG_DIR "/sunshine.conf", sunshine.config_file);
  }

  auto vars = parse_config(read_file(sunshine.config_file.c_str()));

  for(auto &[name, value] : cmd_vars) {
    vars.insert_or_assign(std::move(name), std::move(value));
  }

  apply_config(std::move(vars));

  return 0;
}
} // namespace config
