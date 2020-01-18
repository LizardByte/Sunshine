#ifndef SUNSHINE_CONFIG_H
#define SUNSHINE_CONFIG_H

#include <chrono>
#include <string>

namespace config {
struct video_t {
  // ffmpeg params
  int crf; // higher == more compression and less quality
  int qp; // higher == more compression and less quality, ignored if crf != 0

  int threads; // Number threads used by ffmpeg

  std::string profile;
  std::string preset;
  std::string tune;
};

struct audio_t {
  std::string sink;
};

struct stream_t {
  std::chrono::milliseconds ping_timeout;

  std::string file_apps;

  int fec_percentage;
};

struct nvhttp_t {
  // Could be any of the following values:
  // pc|lan|wan
  std::string origin_pin_allowed;

  std::string pkey; // must be 2048 bits
  std::string cert; // must be signed with a key of 2048 bits

  std::string sunshine_name;

  std::string unique_id; //UUID
  std::string file_devices;

  std::string external_ip;
};

struct input_t {
  std::chrono::milliseconds back_button_timeout;
};

struct sunshine_t {
  int min_log_level;
};

extern video_t video;
extern audio_t audio;
extern stream_t stream;
extern nvhttp_t nvhttp;
extern input_t input;
extern sunshine_t sunshine;

void parse_file(const char *file);
}

#endif
