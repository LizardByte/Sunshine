#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/json.hpp>

#include "config.h"
#include "main.h"
#include "utility.h"

#include "platform/common.h"

namespace fs   = std::filesystem;
namespace json = boost::json;
using namespace std::literals;

#define CA_DIR "credentials"
#define PRIVATE_KEY_FILE CA_DIR "/cakey.pem"
#define CERTIFICATE_FILE CA_DIR "/cacert.pem"

#define APPS_JSON_PATH platf::appdata().string() + "/apps.json"
namespace config {

std::string_view to_config_prop_string(config_props propType) {
  switch(propType) {
  case TYPE_INT:
    return "int"sv;
  case TYPE_DOUBLE:
    return "double"sv;
  case TYPE_STRING:
    return "string"sv;
  case TYPE_INT_ARRAY:
    return "int_array"sv;
  case TYPE_STRING_ARRAY:
    return "string_array"sv;
  case TYPE_FILE:
    return "file"sv;
  case TYPE_BOOLEAN:
    return "boolean"sv;
  }

  // avoid warning
  return "custom"sv;
}

std::unordered_map<std::string, std::pair<config_prop, limit>> property_schema = {
  { "qp"s, { config_prop(TYPE_INT, "qp"s, ""s, true, &video.qp), no_limit() } },
  { "min_threads"s, { config_prop(TYPE_INT, "min_threads"s, "Minimum number of threads used by ffmpeg to encode the video."s, true, &video.min_threads), minmax_limit(1, std::thread::hardware_concurrency()) } },
  { "hevc_mode"s, { config_prop(TYPE_INT, "hevc_mode"s, "Allows the client to request HEVC Main or HEVC Main10 video streams."s, true, &video.hevc_mode), minmax_limit(0, 3) } },
  { "sw_preset"s, { config_prop(TYPE_STRING, "sw_preset"s, "Software encoding preset"s, true, &video.sw.preset), no_limit() } },
  { "sw_tune"s, { config_prop(TYPE_STRING, "sw_tune"s, "Software encoding tuning parameters"s, true, &video.sw.tune), no_limit() } },
  { "nv_preset"s, { config_prop(TYPE_STRING, "nv_preset"s, "NVENC preset"s, true), string_limit({ "p1", "p2", "p3", "p4", "p5", "p6", "p7" }) } },
  { "nv_tune"s, { config_prop(TYPE_STRING, "nv_tune"s, "NVENC tune"s, true), string_limit({ "hq", "ul", "ull", "lossless" }) } },
  { "nv_rc"s, { config_prop(TYPE_STRING, "nv_rc"s, "NVENC rate control"s, true), string_limit({ "constqp", "cbr", "vbr" }) } },
  { "nv_coder"s, { config_prop(TYPE_STRING, "nv_coder"s, "NVENC Coder"s, true), string_limit({ "auto", "cabac", "cavlc" }) } },
  { "amd_quality"s, { config_prop(TYPE_STRING, "amd_quality"s, "AMD AMF quality"s, true), string_limit({ "default", "speed", "balanced" }) } },
  { "amd_rc"s, { config_prop(TYPE_STRING, "amd_rc"s, "AMD AMF rate control"s, true), string_limit({ "cqp", "vbr_latency", "vbr_peak", "cbr" }) } },
  { "amd_coder"s, { config_prop(TYPE_INT, "amd_coder"s, ""s, true), string_limit({ "auto", "cabac", "cavlc" }) } },
  { "encoder"s, { config_prop(TYPE_STRING, "encoder"s, "Force a specific encoder"s, true, &video.encoder), string_limit({ "nvenc", "amdvce", "vaapi", "software" }) } },
  { "adapter_name"s, { config_prop(TYPE_STRING, "adapter_name"s, "Select the video card you want to stream with."s, true, &video.adapter_name), video_devices_limit(video_devices_limit_type::ADAPTER) } },
  { "output_name"s, { config_prop(TYPE_STRING, "output_name"s, "Select the display output you want to stream."s, true, &video.output_name), video_devices_limit(video_devices_limit_type::OUTPUT) } },
  { "pkey"s, { config_prop(TYPE_FILE, "pkey"s, "Path to the private key file. The private key must be 2048 bits."s, true, &nvhttp.pkey), no_limit() } },
  { "cert"s, { config_prop(TYPE_STRING, "cert"s, "Path to the certificate file. The certificate must be signed with a 2048 bit key!"s, true, &nvhttp.cert), no_limit() } },
  { "sunshine_name"s, { config_prop(TYPE_STRING, "sunshine_name"s, "The name displayed by Moonlight. If not specified, the PC's hostname is used"s, true, &nvhttp.sunshine_name), no_limit() } },
  { "file_state"s, { config_prop(TYPE_FILE, "file_state"s, "The file where current state of Sunshine is stored."s, true, &nvhttp.file_state), no_limit() } },
  { "external_ip"s, { config_prop(TYPE_STRING, "external_ip"s, ""s, true, &nvhttp.external_ip), no_limit() } },
  { "resolutions"s, { config_prop(TYPE_STRING_ARRAY, "resolutions"s, "Display resolutions reported by Sunshine as supported."s, true, &nvhttp.resolutions), no_limit() } },
  { "fps"s, { config_prop(TYPE_INT_ARRAY, "fps"s, "Supported FPS reported to clients"s, true, &nvhttp.fps), no_limit() } },
  { "audio_sink"s, { config_prop(TYPE_STRING, "audio_sink"s, "The name of the audio sink used for Audio Loopback."s, true, &audio.sink), audio_devices_limit() } },
  { "virtual_sink"s, { config_prop(TYPE_STRING, "virtual_sink"s, "Virtual audio device name (like Steam Streaming Speakers)\r\nAllows Sunshine to stream audio with muted speakers."s, true, &audio.virtual_sink), audio_devices_limit() } },
  { "origin_pin_allowed"s, { config_prop(TYPE_STRING, "origin_pin_allowed"s, ""s, true, &nvhttp.origin_pin_allowed), no_limit() } },
  { "ping_timeout"s, { config_prop(TYPE_INT, "ping_timeout"s, "How long to wait (in milliseconds) for data from Moonlight clients before shutting down the stream"s, true, &stream.ping_timeout), minmax_limit(0, std::numeric_limits<int>::max()) } },
  { "channels"s, { config_prop(TYPE_INT, "channels"s, ""s, true, &video.qp), minmax_limit(1, 24) } },
  { "file_apps"s, { config_prop(TYPE_FILE, "file_apps"s, "Path to apps.json which contains all the necessary configuration for running apps in Sunshine."s, true, &stream.file_apps), no_limit() } },
  { "fec_percentage"s, { config_prop(TYPE_INT, "fec_percentage"s, "Percentage of error correcting packets per data packet in each video frame."s, true, &stream.fec_percentage), minmax_limit(0, 100) } },
  { "keybindings"s, { config_prop(TYPE_STRING_ARRAY, "keybindings"s, ""s, true), no_limit() } },
  { "key_rightalt_to_key_win"s, { config_prop(TYPE_BOOLEAN, "key_rightalt_to_key_win"s, "It may be possible that you cannot send the Windows key from Moonlight directly. \r\n Allows using Right Alt key as the Windows key."s, true), no_limit() } },
  { "back_button_timeout"s, { config_prop(TYPE_INT, "back_button_timeout"s, "Emulate back/select button press on the controller."s, true, &input.back_button_timeout), minmax_limit(-1, std::numeric_limits<int>::max()) } },
  { "key_repeat_frequency"s, { config_prop(TYPE_DOUBLE, "key_repeat_frequency"s, "How often keys repeat every second after delay.\r\nThis configurable option supports decimals"s, true), no_limit() } },
  { "key_repeat_delay"s, { config_prop(TYPE_INT, "key_repeat_delay"s, "Controls how fast keys will repeat themselves after holding\r\nThe initial delay in milliseconds before repeating keys."s, true, &input.key_repeat_delay), minmax_limit(-1, std::numeric_limits<int>::max()) } },
  { "gamepad"s, { config_prop(TYPE_STRING, "gamepad"s, "Default gamepad used"s, true, &input.gamepad), string_limit(platf::supported_gamepads()) } },
  { "port"s, { config_prop(TYPE_INT, "port"s, ""s, true), minmax_limit(1024, 49151) } },
  { "upnp"s, { config_prop(TYPE_BOOLEAN, "upnp"s, "Automatically configure port forwarding"s, true), no_limit() } },
  { "dwmflush"s, { config_prop(TYPE_BOOLEAN, "dwmflush"s, "Improves capture latency during mouse movement.\r\nEnabling this may prevent the client's FPS from exceeding the host monitor's active refresh rate."s, false, &video.dwmflush), no_limit() } },
  { "min_log_level"s, { config_prop(TYPE_STRING, "min_log_level"s, "The minimum log level printed to standard out"s, true), string_limit({ "verbose", "debug", "info", "warning", "error", "fatal", "none", "0", "1", "2", "3", "4", "5", "6" }) } },
};

namespace nv {
#ifdef __APPLE__
// values accurate as of 27/12/2022, but aren't strictly necessary for MacOS build
#define NV_ENC_TUNING_INFO_HIGH_QUALITY 1
#define NV_ENC_TUNING_INFO_LOW_LATENCY 2
#define NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY 3
#define NV_ENC_TUNING_INFO_LOSSLESS 4
#define NV_ENC_PARAMS_RC_CONSTQP 0x0
#define NV_ENC_PARAMS_RC_VBR 0x1
#define NV_ENC_PARAMS_RC_CBR 0x2
#define NV_ENC_H264_ENTROPY_CODING_MODE_CABAC 1
#define NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC 2
#else
#include <ffnvcodec/nvEncodeAPI.h>
#endif

enum preset_e : int {
  p1 = 12, // PRESET_P1, // must be kept in sync with <libavcodec/nvenc.h>
  p2,      // PRESET_P2,
  p3,      // PRESET_P3,
  p4,      // PRESET_P4,
  p5,      // PRESET_P5,
  p6,      // PRESET_P6,
  p7       // PRESET_P7
};

enum tune_e : int {
  hq       = NV_ENC_TUNING_INFO_HIGH_QUALITY,
  ll       = NV_ENC_TUNING_INFO_LOW_LATENCY,
  ull      = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY,
  lossless = NV_ENC_TUNING_INFO_LOSSLESS
};

enum rc_e : int {
  constqp = NV_ENC_PARAMS_RC_CONSTQP, /**< Constant QP mode */
  vbr     = NV_ENC_PARAMS_RC_VBR,     /**< Variable bitrate mode */
  cbr     = NV_ENC_PARAMS_RC_CBR      /**< Constant bitrate mode */
};

enum coder_e : int {
  _auto = 0,
  cabac = NV_ENC_H264_ENTROPY_CODING_MODE_CABAC,
  cavlc = NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC,
};

std::optional<preset_e> preset_from_view(const std::string_view &preset) {
#define _CONVERT_(x) \
  if(preset == #x##sv) return x
  _CONVERT_(p1);
  _CONVERT_(p2);
  _CONVERT_(p3);
  _CONVERT_(p4);
  _CONVERT_(p5);
  _CONVERT_(p6);
  _CONVERT_(p7);
#undef _CONVERT_
  return std::nullopt;
}

std::optional<tune_e> tune_from_view(const std::string_view &tune) {
#define _CONVERT_(x) \
  if(tune == #x##sv) return x
  _CONVERT_(hq);
  _CONVERT_(ll);
  _CONVERT_(ull);
  _CONVERT_(lossless);
#undef _CONVERT_
  return std::nullopt;
}

std::optional<rc_e> rc_from_view(const std::string_view &rc) {
#define _CONVERT_(x) \
  if(rc == #x##sv) return x
  _CONVERT_(constqp);
  _CONVERT_(vbr);
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
} // namespace nv

namespace amd {
#ifdef __APPLE__
// values accurate as of 27/12/2022, but aren't strictly necessary for MacOS build
#define AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED 10
#define AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY 0
#define AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED 5
#define AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED 1
#define AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY 2
#define AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED 0
#define AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP 0
#define AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR 3
#define AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR 2
#define AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR 1
#define AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP 0
#define AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR 1
#define AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR 2
#define AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR 3
#define AMF_VIDEO_ENCODER_UNDEFINED 0
#define AMF_VIDEO_ENCODER_CABAC 1
#define AMF_VIDEO_ENCODER_CALV 2
#else
#include <AMF/components/VideoEncoderHEVC.h>
#include <AMF/components/VideoEncoderVCE.h>
#endif

enum class quality_hevc_e : int {
  speed    = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED,
  quality  = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY,
  balanced = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED
};

enum class quality_h264_e : int {
  speed    = AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED,
  quality  = AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY,
  balanced = AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED
};

enum class rc_hevc_e : int {
  cqp         = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP,
  vbr_latency = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR,
  vbr_peak    = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR,
  cbr         = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR
};

enum class rc_h264_e : int {
  cqp         = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP,
  vbr_latency = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR,
  vbr_peak    = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR,
  cbr         = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR
};

enum coder_e : int {
  _auto = AMF_VIDEO_ENCODER_UNDEFINED,
  cabac = AMF_VIDEO_ENCODER_CABAC,
  cavlc = AMF_VIDEO_ENCODER_CALV
};

std::optional<int> quality_from_view(const std::string_view &quality_type, int codec) {
#define _CONVERT_(x) \
  if(quality_type == #x##sv) return codec == 0 ? (int)quality_hevc_e::x : (int)quality_h264_e::x
  _CONVERT_(quality);
  _CONVERT_(speed);
  _CONVERT_(balanced);
#undef _CONVERT_
  return std::nullopt;
}

std::optional<int> rc_from_view(const std::string_view &rc, int codec) {
#define _CONVERT_(x) \
  if(rc == #x##sv) return codec == 0 ? (int)rc_hevc_e::x : (int)rc_h264_e::x
  _CONVERT_(cqp);
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
    nv::p4,   // preset
    nv::ull,  // tune
    nv::cbr,  // rc
    nv::_auto // coder
  },          // nv

  {
    (int)amd::quality_h264_e::balanced, // quality (h264)
    (int)amd::quality_hevc_e::balanced, // quality (hevc)
    (int)amd::rc_h264_e::vbr_latency,   // rate control (h264)
    (int)amd::rc_hevc_e::vbr_latency,   // rate control (hevc)
    (int)amd::coder_e::_auto,           // coder
  },                                    // amd
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
  "pc", // origin_pin

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
  2,                                                  // min_log_level
  0,                                                  // flags
  platf::appdata().string() + "/sunshine_state.json", // User file
  {},                                                 // username
  {},                                                 // password
  {},                                                 // salt
  platf::appdata().string() + "/sunshine.conf",       // config file
  {},                                                 // cmd args
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

void path_f(std::string input, fs::path &value) {
  // appdata needs to be retrieved once only
  static auto appdata = platf::appdata();

  value = input;

  if(value.is_relative()) {
    value = appdata / input;
  }

  auto dir = value;
  dir.remove_filename();

  // Ensure the directories exists
  if(!fs::exists(dir)) {
    fs::create_directories(dir);
  }
}

void path_f(std::string value, std::string &input) {
  fs::path temp = input;

  path_f(std::move(value), temp);

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

void bool_f(std::string input, bool &value) {
  value = to_bool(input);
}

void double_f(std::string input, double &value) {
  if(input.empty()) {
    return;
  }

  char *c_str_p;
  auto val = std::strtod(input.c_str(), &c_str_p);

  if(c_str_p == input.c_str()) {
    return;
  }

  value = val;
}

void double_between_f(std::string inputValue, double &input, const std::pair<double, double> &range) {
  double temp = input;

  double_f(inputValue, temp);

  TUPLE_2D_REF(lower, upper, range);
  if(temp >= lower && temp <= upper) {
    input = temp;
  }
}

void list_string_f(std::string input, std::vector<std::string> &value) {
  if(input.empty()) {
    return;
  }

  value.clear();

  auto begin = std::cbegin(input);
  if(*begin == '[') {
    ++begin;
  }

  begin = std::find_if_not(begin, std::cend(input), whitespace);
  if(begin == std::cend(input)) {
    return;
  }

  auto pos = begin;
  while(pos < std::cend(input)) {
    if(*pos == '[') {
      pos = skip_list(pos + 1, std::cend(input)) + 1;
    }
    else if(*pos == ']') {
      break;
    }
    else if(*pos == ',') {
      value.emplace_back(begin, pos);
      pos = begin = std::find_if_not(pos + 1, std::cend(input), whitespace);
    }
    else {
      ++pos;
    }
  }

  if(pos != begin) {
    value.emplace_back(begin, pos);
  }
}

void list_int_f(std::string input, std::vector<int> &value) {
  std::vector<std::string> list;
  list_string_f(std::move(input), list);

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
    value.emplace_back(tmp);
  }
}

void map_int_int_f(std::string &input, std::unordered_map<int, int> &value) {
  std::vector<int> list;
  list_int_f(input, list);

  // The list needs to be a multiple of 2
  if(list.size() % 2) {
    return;
  }

  int x = 0;
  while(x < list.size()) {
    auto key = list[x++];
    auto val = list[x++];

    value.emplace(key, val);
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

void save_config(json::object configJson) {
  std::stringstream configStream;

  for(auto &[name, val] : configJson) {
    auto it = property_schema.find(name);
    if(it == property_schema.end()) {
      BOOST_LOG(warning) << "Tried to set property " << name << " but property doesn\'t exist.";
      continue;
    }
    if(val.is_null()) {
      BOOST_LOG(warning) << "Property" << name << "has no value. Skipping.";
      continue;
    }

    config_prop any_prop = it->second.first;
    std::string valueToSave;
    switch(any_prop.prop_type) {
    case TYPE_STRING:
      if(val.is_string()) {
        valueToSave = std::string { val.as_string() };
      }
      break;
    case TYPE_FILE: {
      if(val.is_string()) {
        path_f(std::string { val.as_string() }, valueToSave);
      }
      break;
    }
    case TYPE_INT: {
      if(val.is_int64()) {
        valueToSave = std::to_string(val.as_int64());
      }
      break;
    }
    case TYPE_INT_ARRAY: {
      if(val.is_array()) {
        auto arr          = val.as_array();
        bool isValidValue = true;
        for(auto &element : arr) {
          if(!element.is_int64()) {
            isValidValue = false;
            break;
          }
        }
        if(isValidValue) {
          valueToSave = json::serialize(val);
        }
      }
      break;
    }
    case TYPE_STRING_ARRAY: {
      if(val.is_array()) {
        auto arr          = val.as_array();
        bool isValidValue = true;
        for(auto &element : arr) {
          if(!element.is_string()) {
            isValidValue = false;
            break;
          }
        }
        if(isValidValue) {
          valueToSave = json::serialize(val);
        }
      }
      break;
    }
    case TYPE_BOOLEAN: {
      if(val.is_bool()) {
        valueToSave = val.as_bool() ? "true" : "false";
      }
      break;
    }
    case TYPE_DOUBLE: {
      if(val.is_double()) {
        valueToSave = std::to_string(val.as_double());
      }
      break;
    }
    }

    bool isValidValue = it->second.second->check(valueToSave);
    if(isValidValue) {
      configStream << name << " = " << val << std::endl;
    }
    else {
      BOOST_LOG(error) << "Property" << name << "has invalid value. Skipping.";
      continue;
    }
  }

  write_file(config::sunshine.config_file.c_str(), configStream.str());
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

void handle_custom_config_prop(const config_prop &prop, std::string val) {
  if(prop.name == "min_log_level") {
    set_min_log_level(val);
  }
  else if(prop.name == "nv_rc") {
    video.nv.rc = nv::rc_from_view(val);
  }
  else if(prop.name == "nv_preset") {
    video.nv.preset = nv::preset_from_view(val);
  }
  else if(prop.name == "nv_tune") {
    video.nv.tune = nv::tune_from_view(val);
  }
  else if(prop.name == "nv_coder") {
    video.nv.coder = nv::coder_from_view(val);
  }
  else if(prop.name == "amd_rc") {
    video.amd.rc_h264 = amd::rc_from_view(val, 1);
    video.amd.rc_hevc = amd::rc_from_view(val, 0);
  }
  else if(prop.name == "amd_quality") {
    video.amd.quality_h264 = amd::quality_from_view(val, 1);
    video.amd.quality_hevc = amd::quality_from_view(val, 0);
  }
  else if(prop.name == "amd_coder") {
    video.amd.coder = amd::coder_from_view(val);
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
    fs::copy_file(SUNSHINE_ASSETS_DIR "/apps.json", stream.file_apps);
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
    if(any_prop.value != nullptr) {
      switch(any_prop.prop_type) {
      case TYPE_STRING:
        *reinterpret_cast<std::string *>(any_prop.value) = val;
        break;
      case TYPE_FILE:
        path_f(val, *reinterpret_cast<std::string *>(any_prop.value));
        break;
      case TYPE_INT:
        int_f(val, *reinterpret_cast<int *>(any_prop.value));
        break;
      case TYPE_INT_ARRAY:
        list_int_f(val, *reinterpret_cast<std::vector<int> *>(any_prop.value));
        break;
      case TYPE_STRING_ARRAY:
        list_string_f(val, *reinterpret_cast<std::vector<std::string> *>(any_prop.value));
        break;
      case TYPE_BOOLEAN:
        bool_f(val, *reinterpret_cast<bool *>(any_prop.value));
        break;
      case TYPE_DOUBLE:
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

  // create appdata folder if it does not exist
  if(!boost::filesystem::exists(platf::appdata().string())) {
    boost::filesystem::create_directory(platf::appdata().string());
  }

  // create config file if it does not exist
  if(!fs::exists(sunshine.config_file)) {
    std::ofstream { sunshine.config_file }; // create empty config file
  }

  auto vars = parse_config(read_file(sunshine.config_file.c_str()));

  for(auto &[name, value] : cmd_vars) {
    vars.insert_or_assign(name, std::move(value));
  }

  apply_config(std::move(vars));

  return 0;
}
} // namespace config
