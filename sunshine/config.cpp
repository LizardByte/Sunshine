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

#define APPS_JSON_PATH SUNSHINE_CONFIG_DIR "/" APPS_JSON
namespace config {

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

  {}, // encoder
  {}, // adapter_name
  {}, // output_name
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
  2,                                    // min_log_level
  0,                                    // flags
  {},                                   // User file
  {},                                   // Username
  {},                                   // Password
  {},                                   // Password Salt
  SUNSHINE_CONFIG_DIR "/sunshine.conf", // config file
  {},                                   // cmd args
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

void string_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::string &input) {
  auto it = vars.find(name);
  if(it == std::end(vars)) {
    return;
  }

  input = std::move(it->second);

  vars.erase(it);
}

void string_restricted_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::string &input, const std::vector<std::string_view> &allowed_vals) {
  std::string temp;
  string_f(vars, name, temp);

  for(auto &allowed_val : allowed_vals) {
    if(temp == allowed_val) {
      input = std::move(temp);
      return;
    }
  }
}

void path_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, fs::path &input) {
  // appdata needs to be retrieved once only
  static auto appdata = platf::appdata();

  std::string temp;
  string_f(vars, name, temp);

  if(!temp.empty()) {
    input = temp;
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

void path_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::string &input) {
  fs::path temp = input;

  path_f(vars, name, temp);

  input = temp.string();
}

void int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, int &input) {
  auto it = vars.find(name);

  if(it == std::end(vars)) {
    return;
  }

  std::string_view val = it->second;

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

  vars.erase(it);
}

void int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::optional<int> &input) {
  auto it = vars.find(name);

  if(it == std::end(vars)) {
    return;
  }

  std::string_view val = it->second;

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

  vars.erase(it);
}

template<class F>
void int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, int &input, F &&f) {
  std::string tmp;
  string_f(vars, name, tmp);
  if(!tmp.empty()) {
    input = f(tmp);
  }
}

template<class F>
void int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::optional<int> &input, F &&f) {
  std::string tmp;
  string_f(vars, name, tmp);
  if(!tmp.empty()) {
    input = f(tmp);
  }
}

void int_between_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, int &input, const std::pair<int, int> &range) {
  int temp = input;

  int_f(vars, name, temp);

  TUPLE_2D_REF(lower, upper, range);
  if(temp >= lower && temp <= upper) {
    input = temp;
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

void bool_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, bool &input) {
  std::string tmp;
  string_f(vars, name, tmp);

  if(tmp.empty()) {
    return;
  }

  input = to_bool(tmp);
}

void double_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, double &input) {
  std::string tmp;
  string_f(vars, name, tmp);

  if(tmp.empty()) {
    return;
  }

  char *c_str_p;
  auto val = std::strtod(tmp.c_str(), &c_str_p);

  if(c_str_p == tmp.c_str()) {
    return;
  }

  input = val;
}

void double_between_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, double &input, const std::pair<double, double> &range) {
  double temp = input;

  double_f(vars, name, temp);

  TUPLE_2D_REF(lower, upper, range);
  if(temp >= lower && temp <= upper) {
    input = temp;
  }
}

void list_string_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::vector<std::string> &input) {
  std::string string;
  string_f(vars, name, string);

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

void list_int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::vector<int> &input) {
  std::vector<std::string> list;
  list_string_f(vars, name, list);

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

void map_int_int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::unordered_map<int, int> &input) {
  std::vector<int> list;
  list_int_f(vars, name, list);

  // The list needs to be a multiple of 2
  if(list.size() % 2) {
    std::cout << "Warning: expected "sv << name << " to have a multiple of two elements --> not "sv << list.size() << std::endl;
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

void apply_config(std::unordered_map<std::string, std::string> &&vars) {
  if(!fs::exists(stream.file_apps.c_str())) {
    fs::copy_file(SUNSHINE_DEFAULT_DIR "/" APPS_JSON, stream.file_apps);
  }

  for(auto &[name, val] : vars) {
    std::cout << "["sv << name << "] -- ["sv << val << ']' << std::endl;
  }

  int_f(vars, "qp", video.qp);
  int_f(vars, "min_threads", video.min_threads);
  int_between_f(vars, "hevc_mode", video.hevc_mode, { 0, 3 });
  string_f(vars, "sw_preset", video.sw.preset);
  string_f(vars, "sw_tune", video.sw.tune);
  int_f(vars, "nv_preset", video.nv.preset, nv::preset_from_view);
  int_f(vars, "nv_rc", video.nv.rc, nv::rc_from_view);
  int_f(vars, "nv_coder", video.nv.coder, nv::coder_from_view);

  int_f(vars, "amd_quality", video.amd.quality, amd::quality_from_view);

  std::string rc;
  string_f(vars, "amd_rc", rc);
  int_f(vars, "amd_coder", video.amd.coder, amd::coder_from_view);
  if(!rc.empty()) {
    video.amd.rc_h264 = amd::rc_h264_from_view(rc);
    video.amd.rc_hevc = amd::rc_hevc_from_view(rc);
  }

  string_f(vars, "encoder", video.encoder);
  string_f(vars, "adapter_name", video.adapter_name);
  string_f(vars, "output_name", video.output_name);

  path_f(vars, "pkey", nvhttp.pkey);
  path_f(vars, "cert", nvhttp.cert);
  string_f(vars, "sunshine_name", nvhttp.sunshine_name);

  path_f(vars, "file_state", nvhttp.file_state);

  // Must be run after "file_state"
  config::sunshine.credentials_file = config::nvhttp.file_state;
  path_f(vars, "credentials_file", config::sunshine.credentials_file);

  string_f(vars, "external_ip", nvhttp.external_ip);
  list_string_f(vars, "resolutions"s, nvhttp.resolutions);
  list_int_f(vars, "fps"s, nvhttp.fps);

  string_f(vars, "audio_sink", audio.sink);
  string_f(vars, "virtual_sink", audio.virtual_sink);

  string_restricted_f(vars, "origin_pin_allowed", nvhttp.origin_pin_allowed, { "pc"sv, "lan"sv, "wan"sv });
  string_restricted_f(vars, "origin_web_ui_allowed", nvhttp.origin_web_ui_allowed, { "pc"sv, "lan"sv, "wan"sv });

  int to = -1;
  int_between_f(vars, "ping_timeout", to, { -1, std::numeric_limits<int>::max() });
  if(to != -1) {
    stream.ping_timeout = std::chrono::milliseconds(to);
  }

  int_between_f(vars, "channels", stream.channels, { 1, std::numeric_limits<int>::max() });

  path_f(vars, "file_apps", stream.file_apps);
  int_between_f(vars, "fec_percentage", stream.fec_percentage, { 1, 255 });

  map_int_int_f(vars, "keybindings"s, input.keybindings);

  // This config option will only be used by the UI
  // When editing in the config file itself, use "keybindings"
  bool map_rightalt_to_win = false;
  bool_f(vars, "key_rightalt_to_key_win", map_rightalt_to_win);

  if(map_rightalt_to_win) {
    input.keybindings.emplace(0xA5, 0x5B);
  }

  to = std::numeric_limits<int>::min();
  int_f(vars, "back_button_timeout", to);

  if(to > std::numeric_limits<int>::min()) {
    input.back_button_timeout = std::chrono::milliseconds { to };
  }

  double repeat_frequency { 0 };
  double_between_f(vars, "key_repeat_frequency", repeat_frequency, { 0, std::numeric_limits<double>::max() });

  if(repeat_frequency > 0) {
    config::input.key_repeat_period = std::chrono::duration<double> { 1 / repeat_frequency };
  }

  to = -1;
  int_f(vars, "key_repeat_delay", to);
  if(to >= 0) {
    input.key_repeat_delay = std::chrono::milliseconds { to };
  }

  string_restricted_f(vars, "gamepad"s, input.gamepad, platf::supported_gamepads());

  int port = sunshine.port;
  int_f(vars, "port"s, port);
  sunshine.port = (std::uint16_t)port;

  bool upnp = false;
  bool_f(vars, "upnp"s, upnp);

  if(upnp) {
    config::sunshine.flags[config::flag::UPNP].flip();
  }

  std::string log_level_string;
  string_f(vars, "min_log_level", log_level_string);

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

  auto it = vars.find("flags"s);
  if(it != std::end(vars)) {
    apply_flags(it->second.c_str());

    vars.erase(it);
  }

  if(sunshine.min_log_level <= 3) {
    for(auto &[var, _] : vars) {
      std::cout << "Warning: Unrecognized configurable option ["sv << var << ']' << std::endl;
    }
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
    fs::copy_file(SUNSHINE_DEFAULT_DIR "/sunshine.conf", sunshine.config_file);
  }

  auto vars = parse_config(read_file(sunshine.config_file.c_str()));

  for(auto &[name, value] : cmd_vars) {
    vars.insert_or_assign(std::move(name), std::move(value));
  }

  apply_config(std::move(vars));

  return 0;
}
} // namespace config
