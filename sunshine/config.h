#ifndef SUNSHINE_CONFIG_H
#define SUNSHINE_CONFIG_H

#include <chrono>
#include <string>
#include <bitset>
#include <optional>

namespace config {
struct video_t {
  // ffmpeg params
  int crf; // higher == more compression and less quality
  int qp; // higher == more compression and less quality, ignored if crf != 0

  int hevc_mode;

  int min_threads; // Minimum number of threads/slices for CPU encoding
  struct {
    std::string preset;
    std::string tune;
  } sw;

  struct {
    std::optional<int> preset;
    std::optional<int> rc;
    int coder;
  } nv;

  struct {
    std::optional<int> quality;
    std::optional<int> rc;
    int coder;
  } amd;

  std::string encoder;
  std::string adapter_name;
  std::string output_name;
};

struct audio_t {
  std::string sink;
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
};

struct input_t {
  std::chrono::milliseconds back_button_timeout;
  std::chrono::milliseconds key_repeat_delay;
  std::chrono::duration<double> key_repeat_period;
};

namespace flag {
enum flag_e : std::size_t {
  PIN_STDIN = 0, // Read PIN from stdin instead of http
  FRESH_STATE, // Do not load or save state
  FLAG_SIZE
};
}

struct sunshine_t {
  int min_log_level;

  std::bitset<flag::FLAG_SIZE> flags;
};

extern video_t video;
extern audio_t audio;
extern stream_t stream;
extern nvhttp_t nvhttp;
extern input_t input;
extern sunshine_t sunshine;

int parse(int argc, char *argv[]);
}

#endif
