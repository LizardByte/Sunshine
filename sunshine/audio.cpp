#include <thread>

#include <opus/opus_multistream.h>

#include "platform/common.h"

#include "utility.h"
#include "thread_safe.h"
#include "audio.h"
#include "main.h"

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

void encodeThread(packet_queue_t packets, sample_queue_t samples, config_t config, void *channel_data) {
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
      BOOST_LOG(error) << opus_strerror(bytes);
      packets->stop();

      return;
    }

    packet.fake_resize(bytes);
    packets->raise(channel_data, std::move(packet));
  }
}

void capture(safe::signal_t *shutdown_event, packet_queue_t packets, config_t config, void *channel_data) {
  auto samples = std::make_shared<sample_queue_t::element_type>(30);
  std::thread thread { encodeThread, packets, samples, config, channel_data };

  auto fg = util::fail_guard([&]() {
    samples->stop();
    thread.join();

    shutdown_event->view();
  });

  //FIXME: Pick correct opus_stream_config_t based on config.channels
  auto stream = &stereo;

  auto mic = platf::microphone(stream->sampleRate);
  if(!mic) {
    BOOST_LOG(error) << "Couldn't create audio input"sv ;

    return;
  }

  auto frame_size = config.packetDuration * stream->sampleRate / 1000;
  int samples_per_frame = frame_size * stream->channelCount;

  while(!shutdown_event->peek()) {
    std::vector<std::int16_t> sample_buffer;
    sample_buffer.resize(samples_per_frame);

    auto status = mic->sample(sample_buffer);
    switch(status) {
      case platf::capture_e::ok:
        break;
      case platf::capture_e::timeout:
        continue;
      case platf::capture_e::reinit:
        mic.reset();
        mic = platf::microphone(stream->sampleRate);
        if(!mic) {
          BOOST_LOG(error) << "Couldn't re-initialize audio input"sv ;

          return;
        }
        return;
      default:
        return;
    }

    samples->raise(std::move(sample_buffer));
  }
}
}
