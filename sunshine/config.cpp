#include <fstream>
#include <iostream>
#include <functional>
#include <unordered_map>

#include <boost/asio.hpp>

#include "utility.h"
#include "config.h"

#define CA_DIR "credentials"
#define PRIVATE_KEY_FILE CA_DIR "/cakey.pem"
#define CERTIFICATE_FILE CA_DIR "/cacert.pem"

#define APPS_JSON_PATH SUNSHINE_ASSETS_DIR "/" APPS_JSON
namespace config {
using namespace std::literals;

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
  constqp   = 0x0,       /**< Constant QP mode */
  vbr       = 0x1,       /**< Variable bitrate mode */
  cbr       = 0x2,       /**< Constant bitrate mode */
  cbr_ld_hq = 0x8,       /**< low-delay CBR, high quality */
  cbr_hq    = 0x10,      /**< CBR, high quality (slower) */
  vbr_hq    = 0x20       /**< VBR, high quality (slower) */
};

enum coder_e : int {
  _auto = 0,
  cabac,
  cavlc
};

std::optional<preset_e> preset_from_view(const std::string_view &preset) {
#define _CONVERT_(x) if(preset == #x##sv) return x
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
#define _CONVERT_(x) if(rc == #x##sv) return x
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
  if(coder == "cabac"sv  || coder == "ac"sv) return cabac;
  if(coder == "cavlc"sv  || coder == "vlc"sv) return cavlc;

  return -1;
}
}

namespace amd {
enum quality_e : int {
  _default = 0,
  speed,
  balanced,
  //quality2,
};

enum rc_e : int {
  constqp   = 0x0,       /**< Constant QP mode */
  vbr       = 0x1,       /**< Variable bitrate mode */
  cbr       = 0x2,       /**< Constant bitrate mode */
  cbr_ld_hq = 0x8,       /**< low-delay CBR, high quality */
  cbr_hq    = 0x10,      /**< CBR, high quality (slower) */
  vbr_hq    = 0x20       /**< VBR, high quality (slower) */
};

enum coder_e : int {
  _auto = 0,
  cabac,
  cavlc
};

std::optional<quality_e> quality_from_view(const std::string_view &quality) {
#define _CONVERT_(x) if(quality == #x##sv) return x
  _CONVERT_(speed);
  _CONVERT_(balanced);
  //_CONVERT_(quality2);
  if(quality == "default"sv) return _default;
#undef _CONVERT_
  return std::nullopt;
}

std::optional<rc_e> rc_from_view(const std::string_view &rc) {
#define _CONVERT_(x) if(rc == #x##sv) return x
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
  if(coder == "cabac"sv  || coder == "ac"sv) return cabac;
  if(coder == "cavlc"sv  || coder == "vlc"sv) return cavlc;

  return -1;
}
}

video_t video {
  0, // crf
  28, // qp

  0, // hevc_mode

  1, // min_threads
  {
    "superfast"s, // preset
    "zerolatency"s, // tune
  }, // software

  {
    nv::llhq,
    std::nullopt,
    -1
  }, // nv

  {
    amd::balanced,
    std::nullopt,
    -1
  }, // amd  

  {}, // encoder
  {}, // adapter_name
  {}  // output_name
};

audio_t audio {};

stream_t stream {
  2s, // ping_timeout

  APPS_JSON_PATH,

  10, // fecPercentage
  1 // channels
};

nvhttp_t nvhttp {
  "lan", // origin_pin
  PRIVATE_KEY_FILE,
  CERTIFICATE_FILE,

  boost::asio::ip::host_name(), // sunshine_name,
  "sunshine_state.json"s // file_state
};

input_t input {
  2s, // back_button_timeout
  500ms, // key_repeat_delay
  std::chrono::duration<double> { 1 / 24.9 }  // key_repeat_period
};

sunshine_t sunshine {
  2, // min_log_level
  0 // flags
};

bool whitespace(char ch) {
  return ch == ' ' || ch == '\t';
}

std::string to_string(const char *begin, const char *end) {
  return { begin, (std::size_t)(end - begin) };
}

std::optional<std::pair<std::string, std::string>> parse_line(std::string_view::const_iterator begin, std::string_view::const_iterator end) {
  begin = std::find_if(begin, end, std::not_fn(whitespace));
  end   = std::find(begin, end, '#');
  end   = std::find_if(std::make_reverse_iterator(end), std::make_reverse_iterator(begin), std::not_fn(whitespace)).base();

  auto eq = std::find(begin, end, '=');
  if(eq == end || eq == begin) {
    return std::nullopt;
  }

  auto end_name = std::find_if(std::make_reverse_iterator(eq), std::make_reverse_iterator(begin), std::not_fn(whitespace)).base();
  auto begin_val = std::find_if(eq + 1, end, std::not_fn(whitespace));

  return std::pair { to_string(begin, end_name), to_string(begin_val, end) };
}

std::unordered_map<std::string, std::string> parse_config(std::string_view file_content) {
  std::unordered_map<std::string, std::string> vars;

  auto pos = std::begin(file_content);
  auto end = std::end(file_content);

  while(pos < end) {
    auto newline = std::find_if(pos, end, [](auto ch) { return ch == '\n' || ch == '\r'; });
    auto var = parse_line(pos, newline);

    pos = (*newline == '\r') ? newline + 2 : newline + 1;
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

void int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, int &input) {
  auto it = vars.find(name);

  if(it == std::end(vars)) {
    return;
  }

  auto &val = it->second;
  input = util::from_chars(&val[0], &val[0] + val.size());

  vars.erase(it);
}

void int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::optional<int> &input) {
  auto it = vars.find(name);

  if(it == std::end(vars)) {
    return;
  }

  auto &val = it->second;
  input = util::from_chars(&val[0], &val[0] + val.size());

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
  std::for_each(std::begin(boolean), std::end(boolean), [](char ch)  { return (char)std::tolower(ch);  });

  return
    boolean == "true"sv   ||
    boolean == "yes"sv    ||
    boolean == "enable"sv ||
    (std::find(std::begin(boolean), std::end(boolean), '1') != std::end(boolean));
}
void bool_f(std::unordered_map<std::string, std::string> &vars, const std::string  &name, int &input) {
  std::string tmp;
  string_f(vars, name, tmp);

  if(tmp.empty()) {
    return;
  }

  input = to_bool(tmp) ? 1 : 0;
}

void double_f(std::unordered_map<std::string, std::string> &vars, const std::string  &name, double &input) {
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

void print_help(const char *name) {
  std::cout <<
    "Usage: "sv << name << " [options] [/path/to/configuration_file]"sv << std::endl <<
    "    Any configurable option can be overwritten with: \"name=value\""sv << std::endl << std::endl <<
    "    --help | print help"sv << std::endl << std::endl <<
    "    flags"sv << std::endl <<
    "        -0 | Read PIN from stdin"sv << std::endl <<
    "        -1 | Do not load previously saved state and do retain any state after shutdown"sv << std::endl <<
    "           | Effectively starting as if for the first time without overwriting any pairings with your devices"sv;
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
      default:
        std::cout << "Warning: Unrecognized flag: ["sv << *line << ']' << std::endl;
        ret = -1;
    }

    ++line;
  }

  return ret;
}

void apply_config(std::unordered_map<std::string, std::string> &&vars) {
  for(auto &[name, val] : vars) {
    std::cout << "["sv << name << "] -- ["sv << val << ']' << std::endl;
  }

  int_f(vars, "crf", video.crf);
  int_f(vars, "qp", video.qp);
  int_f(vars, "min_threads", video.min_threads);
  int_between_f(vars, "hevc_mode", video.hevc_mode, {
    0, 3
  });
  string_f(vars, "sw_preset", video.sw.preset);
  string_f(vars, "sw_tune", video.sw.tune);
  int_f(vars, "nv_preset", video.nv.preset, nv::preset_from_view);
  int_f(vars, "nv_rc", video.nv.rc, nv::rc_from_view);
  int_f(vars, "nv_coder", video.nv.coder, nv::coder_from_view);

  int_f(vars, "amd_quality", video.amd.quality, amd::quality_from_view);
  int_f(vars, "amd_rc", video.amd.rc, amd::rc_from_view);
  int_f(vars, "amd_coder", video.amd.coder, amd::coder_from_view);

  string_f(vars, "encoder", video.encoder);
  string_f(vars, "adapter_name", video.adapter_name);
  string_f(vars, "output_name", video.output_name);

  string_f(vars, "pkey", nvhttp.pkey);
  string_f(vars, "cert", nvhttp.cert);
  string_f(vars, "sunshine_name", nvhttp.sunshine_name);
  string_f(vars, "file_state", nvhttp.file_state);
  string_f(vars, "external_ip", nvhttp.external_ip);

  string_f(vars, "audio_sink", audio.sink);

  string_restricted_f(vars, "origin_pin_allowed", nvhttp.origin_pin_allowed, {
    "pc"sv, "lan"sv, "wan"sv
  });

  int to = -1;
  int_between_f(vars, "ping_timeout", to, {
    -1, std::numeric_limits<int>::max()
  });
  if(to != -1) {
    stream.ping_timeout = std::chrono::milliseconds(to);
  }

  int_between_f(vars, "channels", stream.channels, {
    1, std::numeric_limits<int>::max()
  });

  string_f(vars, "file_apps", stream.file_apps);
  int_between_f(vars, "fec_percentage", stream.fec_percentage, {
    1, 100
  });

  to = std::numeric_limits<int>::min();
  int_f(vars, "back_button_timeout", to);

  if(to > std::numeric_limits<int>::min()) {
    input.back_button_timeout = std::chrono::milliseconds { to };
  }

  double repeat_frequency { 0 };
  double_between_f(vars, "key_repeat_frequency", repeat_frequency, {
    0, std::numeric_limits<double>::max()
  });

  if(repeat_frequency > 0) {
    config::input.key_repeat_period = std::chrono::duration<double> {1 / repeat_frequency };
  }

  to = -1;
  int_f(vars, "key_repeat_delay", to);
  if(to >= 0) {
    input.key_repeat_delay = std::chrono::milliseconds { to };
  }

  std::string log_level_string;
  string_restricted_f(vars, "min_log_level", log_level_string, {
    "verbose"sv, "debug"sv, "info"sv, "warning"sv, "error"sv, "fatal"sv, "none"sv
  });

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
  }

  auto it = vars.find("flags"s);
  if(it != std::end(vars)) {
    apply_flags(it->second.c_str());

    vars.erase(it);
  }

  if(sunshine.min_log_level <= 3) {
    for(auto &[var,_] : vars) {
      std::cout << "Warning: Unrecognized configurable option ["sv << var << ']' << std::endl;
    }
  }
}

int parse(int argc, char *argv[]) {
  const char *config_file = SUNSHINE_ASSETS_DIR "/sunshine.conf";

  std::unordered_map<std::string, std::string> cmd_vars;

  for(auto x = argc -1; x > 0; --x) {
    auto line = argv[x];

    if(line == "--help"sv) {
      print_help(*argv);
      return 1;
    }
    else if(*line == '-') {
      if(apply_flags(line + 1)) {
        print_help(*argv);
        return -1;
      }
    }
    else {
      auto line_end = line + strlen(line);

      auto pos = std::find(line, line_end, '=');
      if(pos == line_end) {
        config_file = line;
      }
      else {
        auto var = parse_line(line, line_end);
        if(!var) {
          print_help(*argv);
          return -1;
        }

        cmd_vars.emplace(std::move(*var));
      }
    }
  }

  std::ifstream in { config_file };

  if(!in.is_open()) {
    std::cout << "Error: Couldn't open "sv << config_file << std::endl;

    return -1;
  }

  auto vars = parse_config(std::string {
    // Quick and dirty
    std::istreambuf_iterator<char>(in),
    std::istreambuf_iterator<char>()
  });

  for(auto &[name,value] : cmd_vars) {
    vars.insert_or_assign(std::move(name), std::move(value));
  }

  apply_config(std::move(vars));

  return 0;
}
}
