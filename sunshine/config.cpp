#include <iostream>
#include <fstream>
#include <streambuf>
#include <iterator>
#include <functional>
#include <unordered_map>

#include "utility.h"
#include "config.h"

#define CA_DIR SUNSHINE_ASSETS_DIR "/demoCA"
#define PRIVATE_KEY_FILE CA_DIR    "/cakey.pem"
#define CERTIFICATE_FILE CA_DIR    "/cacert.pem"


namespace config {
using namespace std::literals;
video_t video {
  16, // max_b_frames
  24, // gop_size
  35, // crf

  4, // threads

  "baseline"s, // profile
  "superfast"s, // preset
  "zerolatency"s // tune
};

stream_t stream {
  2s // ping_timeout
};

nvhttp_t nvhttp {
  PRIVATE_KEY_FILE,
  CERTIFICATE_FILE,

  "03904e64-51da-4fb3-9afd-a9f7ff70fea4", // unique_id
  "devices.xml" // file_devices
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

  auto pos = std::begin(file_content) - 1;
  auto end = std::end(file_content);

  while(pos <= end) {
    auto newline = std::find(pos, end, '\n');
    auto var = parse_line(pos, *(newline - 1) == '\r' ? newline - 1 : newline);

    pos = newline + 1;
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
}

void int_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, int &input) {
  auto it = vars.find(name);

  if(it == std::end(vars)) {
    return;
  }

  auto &val = it->second;
  input = util::from_chars(&val[0], &val[0] + val.size());
}

void parse_file(const char *file) {
  std::ifstream in(file);

  auto vars = parse_config(std::string {
    // Quick and dirty
    std::istreambuf_iterator<char>(in),
    std::istreambuf_iterator<char>()
  });

  for(auto &[name, val] : vars) {
    std::cout << "["sv << name << "] -- ["sv << val << "]"sv << std::endl;
  }

  int_f(vars, "max_b_frames", video.max_b_frames);
  int_f(vars, "gop_size", video.gop_size);
  int_f(vars, "crf", video.crf);
  int_f(vars, "threads", video.threads);
  string_f(vars, "profile", video.profile);
  string_f(vars, "preset", video.preset);
  string_f(vars, "tune", video.tune);

  string_f(vars, "pkey", nvhttp.pkey);
  string_f(vars, "cert", nvhttp.cert);
  string_f(vars, "unique_id", nvhttp.unique_id);
  string_f(vars, "file_devices", nvhttp.file_devices);
  string_f(vars, "external_ip", nvhttp.external_ip);

  int to = -1;
  int_f(vars, "ping_timeout", to);
  if(to > 0) {
    stream.ping_timeout = std::chrono::milliseconds(to);
  }
}


}
