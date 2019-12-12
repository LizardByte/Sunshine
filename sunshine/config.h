#ifndef SUNSHINE_CONFIG_H
#define SUNSHINE_CONFIG_H

#include <chrono>
#include <string>

namespace config {
struct video_t {
  // ffmpeg params
  int max_b_frames;
  int gop_size;
  int crf; // higher == more compression and less quality
  int qp; // higher == more compression and less quality, ignored if crf != 0

  int threads; // Number threads used by ffmpeg

  std::string profile;
  std::string preset;
  std::string tune;
};

struct stream_t {
  std::chrono::milliseconds ping_timeout;

  int fec_percentage;
};

struct nvhttp_t {
  std::string pkey; // must be 2048 bits
  std::string cert; // must be signed with a key of 2048 bits

  std::string unique_id; //UUID
  std::string file_devices;

  std::string external_ip;
};

extern video_t video;
extern stream_t stream;
extern nvhttp_t nvhttp;

void parse_file(const char *file);
}

#endif
