#include <thread>
#include <iostream>

#include <opus/opus_multistream.h>

#include "platform/common.h"

#include "utility.h"
#include "thread_safe.h"
#include "audio.h"

namespace audio {
using namespace std::literals;
using opus_t = util::safe_ptr<OpusMSEncoder, opus_multistream_encoder_destroy>;
using sample_queue_t = std::shared_ptr<safe::queue_t<std::vector<std::int16_t>>>;

struct opus_stream_config_t {
  std::int32_t sampleRate;
  int channelCount;
  int streams;
  int coupledStreams;
  const std::uint8_t *mapping;
};

constexpr std::uint8_t map_stereo[] { 0, 1 };
constexpr std::uint8_t map_surround51[] {0, 4, 1, 5, 2, 3};
constexpr std::uint8_t map_high_surround51[] {0, 1, 2, 3, 4, 5};
constexpr auto SAMPLE_RATE = 48000;
static opus_stream_config_t stereo = {
    SAMPLE_RATE,
    2,
    1,
    1,
    map_stereo
};

static opus_stream_config_t Surround51 = {
    SAMPLE_RATE,
    6,
    4,
    2,
    map_surround51
};

static opus_stream_config_t HighSurround51 = {
    SAMPLE_RATE,
    6,
    6,
    0,
    map_high_surround51
};

void encodeThread(std::shared_ptr<safe::queue_t<packet_t>> packets, sample_queue_t samples, config_t config) {
  //FIXME: Pick correct opus_stream_config_t based on config.channels
  auto stream = &stereo;
   opus_t opus { opus_multistream_encoder_create(
     stream->sampleRate,
     stream->channelCount,
     stream->streams,
     stream->coupledStreams,
     stream->mapping,
     OPUS_APPLICATION_AUDIO,
     nullptr)
   };

  auto frame_size = config.packetDuration * stream->sampleRate / 1000;
  while(auto sample = samples->pop()) {
    packet_t packet { 16*1024 }; // 16KB

    int bytes = opus_multistream_encode(opus.get(), sample->data(), frame_size, std::begin(packet), packet.size());
    if(bytes < 0) {
      std::cout << "Error: "sv << opus_strerror(bytes) << std::endl;
      exit(7);
    }

    packet.fake_resize(bytes);
    packets->raise(std::move(packet));
  }
}

void capture(std::shared_ptr<safe::queue_t<packet_t>> packets, config_t config) {
  auto samples = std::make_shared<sample_queue_t::element_type>();

  auto mic = platf::microphone();
  if(!mic) {
    std::cout << "Error creating audio input"sv << std::endl;
  }

  //FIXME: Pick correct opus_stream_config_t based on config.channels
  auto stream = &stereo;

  auto frame_size = config.packetDuration * stream->sampleRate / 1000;
  int samples_per_frame = frame_size * stream->channelCount;

  std::thread thread { encodeThread, packets, samples, config };
  while(packets->running()) {
    auto sample = mic->sample(samples_per_frame);

    samples->raise(std::move(sample));
  }

  samples->stop();
  thread.join();
}
}
