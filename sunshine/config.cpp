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
video_t video {
  0, // crf
  28, // qp

  2, // min_threads

  0, // hevc_mode
  "superfast"s, // preset
  "zerolatency"s, // tune
  {}, // adapter_name
  {} // output_name
};

audio_t audio {};

stream_t stream {
  2s, // ping_timeout

  APPS_JSON_PATH,

  10 // fecPercentage
};

nvhttp_t nvhttp {
  "lan", // origin_pin
  PRIVATE_KEY_FILE,
  CERTIFICATE_FILE,

  boost::asio::ip::host_name(), // sunshine_name,
  "sunshine_state.json"s // file_state
};

input_t input {
  2s
};

sunshine_t sunshine {
  2 // min_log_level
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

  auto end_name = std::find_if(std::make_reverse_iterator(eq - 1), std::make_reverse_iterator(begin), std::not_fn(whitespace)).base();
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

void int_between_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, int &input, const std::pair<int, int> &range) {
  int temp = input;

  int_f(vars, name, temp);

  TUPLE_2D_REF(lower, upper, range);
  if(temp >= lower && temp <= upper) {
    input = temp;
  }
}

void parse_file(const char *file) {
  std::ifstream in(file);

  auto vars = parse_config(std::string {
    // Quick and dirty
    std::istreambuf_iterator<char>(in),
    std::istreambuf_iterator<char>()
  });

  for(auto &[name, val] : vars) {
    std::cout << "["sv << name << "] -- ["sv << val << ']' << std::endl;
  }

  int_f(vars, "crf", video.crf);
  int_f(vars, "qp", video.qp);
  int_f(vars, "min_threads", video.min_threads);
  int_between_f(vars, "hevc_mode", video.hevc_mode, {
    0, 2
  });
  string_f(vars, "preset", video.preset);
  string_f(vars, "tune", video.tune);
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
  int_f(vars, "ping_timeout", to);
  if(to > 0) {
    stream.ping_timeout = std::chrono::milliseconds(to);
  }
  string_f(vars, "file_apps", stream.file_apps);
  int_between_f(vars, "fec_percentage", stream.fec_percentage, {
    1, 100
  });

  to = std::numeric_limits<int>::min();
  int_f(vars, "back_button_timeout", to);

  if(to > std::numeric_limits<int>::min()) {
    input.back_button_timeout = std::chrono::milliseconds {to };
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

  if(sunshine.min_log_level <= 3) {
    for(auto &[var,_] : vars) {
      std::cout << "Warning: Unrecognized configurable option ["sv << var << ']' << std::endl;
    }
  }
}


}
