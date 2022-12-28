#ifndef SUNSHINE_CONFIG_H
#define SUNSHINE_CONFIG_H

#include "src/platform/common.h"
#include <any>
#include <bitset>
#include <boost/property_tree/ptree.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace config {
namespace pt = boost::property_tree;

void list_int_f(std::string val, std::vector<int> &input);
void list_string_f(std::string string, std::vector<std::string> &input);
void int_f(std::string val, int &input);

struct ILimit {

  virtual void to(pt::ptree &tree) const      = 0;
  virtual bool check(std::string value) const = 0;
  virtual ~ILimit()                           = default;
};

struct limit {

  template<typename LimitType>
  limit(LimitType &&object)
      : storage { std::forward<LimitType>(object) }, get { [](std::any &object) -> ILimit & { return std::any_cast<LimitType &>(object); } } {}

  ILimit *operator->() { return &get(storage); }

private:
  std::any storage;
  ILimit &(*get)(std::any &);
};

struct no_limit : ILimit {

  void to(pt::ptree &tree) const override {
    tree.put("type", "none");
  }

  bool check(std::string value) const override { return true; }
};

struct minmax_limit : ILimit {
  int min;
  int max;

  void to(pt::ptree &tree) const override {
    tree.put("type", "minmax");
    tree.put("min", min);
    tree.put("max", max);
  }

  bool check(std::string value) const override {
    int val = std::numeric_limits<int>::max();
    int_f(value, val);

    return val >= this->min && val <= this->max;
  }

  minmax_limit(int min, int max) : min(min), max(max) {}
};

struct audio_devices_limit : ILimit {

  void to(pt::ptree &tree) const override {
    tree.put("type", "audio_device_list");
    std::vector<platf::audio_device_t> devices = platf::audio_control()->available_audio_devices();

    pt::ptree array;
    for(auto &device : devices) {
      pt::ptree element;
      element.put("id", device.id);
      element.put("name", device.name);
      array.push_back(pt::ptree::value_type("", element));
    }
    tree.put_child("devices", array);
  }

  bool check(std::string value) const override {
    std::vector<platf::audio_device_t> devices = platf::audio_control()->available_audio_devices();

    for(auto &device : devices) {
      if(device.id == value)
        return true;
    }
    return false;
  }

  audio_devices_limit() = default;
};

enum video_devices_limit_type {
  ADAPTER,
  OUTPUT
};

struct video_devices_limit : ILimit {

  video_devices_limit_type type;

  void to(pt::ptree &tree) const override {
    tree.put("type", "video_devices_list");
    std::vector<platf::display_device_t> devices = platf::available_outputs();

    pt::ptree array;
    for(const auto &device : devices) {
      pt::ptree device_tree, outputArray;
      device_tree.put("name", device.adapterName);
      for(const auto &outputName : device.outputNames) {
        outputArray.push_back(pt::ptree::value_type("", outputName));
      }
      device_tree.put_child("outputs", outputArray);

      array.push_back(pt::ptree::value_type("", device_tree));
    }
    tree.put_child("devices", array);
  }

  bool check(std::string value) const override {
    std::vector<platf::display_device_t> devices = platf::available_outputs();

    for(auto &device : devices) {
      if(type == video_devices_limit_type::OUTPUT) {
        for(auto &output : device.outputNames) {
          if(output == value)
            return true;
        }
      }
      else if(device.adapterName == value && type == video_devices_limit_type::ADAPTER)
        return true;
    }
    return false;
  }

  explicit video_devices_limit(video_devices_limit_type type) : type(type) {};
};

struct string_limit : ILimit {
  std::vector<std::string_view> values;

  void to(pt::ptree &tree) const override {
    tree.put("type", "string");

    pt::ptree array;
    for(auto &str : values) {
      array.push_back(pt::ptree::value_type("", str.data()));
    }
    tree.put_child("values", array);
  }

  bool check(std::string value) const override {
    std::vector<std::string> list;
    list_string_f(value, list);

    for(const auto &i : list) {
      if(std::find(this->values.begin(), this->values.end(), i) == this->values.end()) return false;
    }
    return true;
  }

  explicit string_limit(std::vector<std::string_view> values) : values(std::move(values)) {}
};

enum config_props : int {
  TYPE_INT,
  TYPE_INT_ARRAY,
  TYPE_STRING,
  TYPE_STRING_ARRAY,
  TYPE_BOOLEAN,
  TYPE_FILE,
  TYPE_DOUBLE
};
struct config_prop {
  enum config_props prop_type;
  std::string name;
  std::string description;
  bool required;
  std::string translatedName;
  void *value;
  config_prop(enum config_props prop_type, std::string name, std::string description, bool required) : prop_type(prop_type), name(std::move(name)), description(std::move(description)), required(required), value(nullptr) {}
  config_prop(enum config_props prop_type, std::string name, std::string description, bool required, void *value) : prop_type(prop_type), name(std::move(name)), description(std::move(description)), required(required), value(value) {}
};

extern std::unordered_map<std::string, std::pair<config_prop, limit>> property_schema;

struct video_t {
  // ffmpeg params
  int qp; // higher == more compression and less quality

  int hevc_mode;

  int min_threads; // Minimum number of threads/slices for CPU encoding
  struct {
    std::string preset;
    std::string tune;
  } sw;

  struct {
    std::optional<int> preset;
    std::optional<int> tune;
    std::optional<int> rc;
    int coder;
  } nv;

  struct {
    std::optional<int> quality_h264;
    std::optional<int> quality_hevc;
    std::optional<int> rc_h264;
    std::optional<int> rc_hevc;
    int coder;
  } amd;

  struct {
    int allow_sw;
    int require_sw;
    int realtime;
    int coder;
  } vt;

  std::string encoder;
  std::string adapter_name;
  std::string output_name;
  bool dwmflush;
};

struct audio_t {
  std::string sink;
  std::string virtual_sink;
};

struct stream_t {
  std::chrono::milliseconds ping_timeout;

  std::string file_apps;

  int fec_percentage;

  // max unique instances of video and audio streams
  int channels;
};

struct nvhttp_t {
  // Could be any of the following values:
  // pc|lan|wan
  std::string origin_pin_allowed;

  std::string pkey; // must be 2048 bits
  std::string cert; // must be signed with a key of 2048 bits

  std::string sunshine_name;

  std::string file_state;

  std::string external_ip;
  std::vector<std::string> resolutions;
  std::vector<int> fps;
};

struct input_t {
  std::unordered_map<int, int> keybindings;

  std::chrono::milliseconds back_button_timeout;
  std::chrono::milliseconds key_repeat_delay;
  std::chrono::duration<double> key_repeat_period;

  std::string gamepad;
};

namespace flag {
enum flag_e : std::size_t {
  PIN_STDIN = 0,              // Read PIN from stdin instead of http
  FRESH_STATE,                // Do not load or save state
  FORCE_VIDEO_HEADER_REPLACE, // force replacing headers inside video data
  UPNP,                       // Try Universal Plug 'n Play
  CONST_PIN,                  // Use "universal" pin
  FLAG_SIZE
};
}

struct sunshine_t {
  int min_log_level;
  std::bitset<flag::FLAG_SIZE> flags;
  std::string credentials_file;

  std::string username;
  std::string password;
  std::string salt;

  std::string config_file;

  struct cmd_t {
    std::string name;
    int argc;
    char **argv;
  } cmd;

  std::uint16_t port;
  std::string log_file;
};

extern video_t video;
extern audio_t audio;
extern stream_t stream;
extern nvhttp_t nvhttp;
extern input_t input;
extern sunshine_t sunshine;

int parse(int argc, char *argv[]);
std::string_view to_config_prop_string(config_props propType);
void apply_config(std::unordered_map<std::string, std::string> &&vars);
void save_config(std::unordered_map<std::string, std::string> &&vars);
std::unordered_map<std::string, std::string> parse_config(const std::string_view &file_content);
} // namespace config
#endif
