#include "sunshine/platform/common.h"
#include "sunshine/platform/macos/av_audio.h"

#include "sunshine/config.h"
#include "sunshine/main.h"

namespace platf {
using namespace std::literals;

struct av_mic_t : public mic_t {
  AVAudio *av_audio_capture;

  ~av_mic_t() {
    [av_audio_capture release];
  }

  capture_e sample(std::vector<std::int16_t> &sample_in) override {
    auto sample_size = sample_in.size();

    uint32_t length        = 0;
    void *byteSampleBuffer = TPCircularBufferTail(&av_audio_capture->audioSampleBuffer, &length);

    while(length < sample_size * sizeof(std::int16_t)) {
      [av_audio_capture.samplesArrivedSignal wait];
      byteSampleBuffer = TPCircularBufferTail(&av_audio_capture->audioSampleBuffer, &length);
    }

    const int16_t *sampleBuffer = (int16_t *)byteSampleBuffer;
    std::vector<int16_t> vectorBuffer(sampleBuffer, sampleBuffer + sample_size);

    std::copy_n(std::begin(vectorBuffer), sample_size, std::begin(sample_in));

    TPCircularBufferConsume(&av_audio_capture->audioSampleBuffer, sample_size * sizeof(std::int16_t));

    return capture_e::ok;
  }
};

struct macos_audio_control_t : public audio_control_t {
  AVCaptureDevice *audio_capture_device;

public:
  int set_sink(const std::string &sink) override {
    BOOST_LOG(warning) << "audio_control_t::set_sink() unimplemented: "sv << sink;
    return 0;
  }

  std::unique_ptr<mic_t> microphone(const std::uint8_t *mapping, int channels, std::uint32_t sample_rate, std::uint32_t frame_size) override {
    auto mic               = std::make_unique<av_mic_t>();
    const char *audio_sink = "";

    if(!config::audio.sink.empty()) {
      audio_sink = config::audio.sink.c_str();
    }

    if((audio_capture_device = [AVAudio findMicrophone:[NSString stringWithUTF8String:audio_sink]]) == nullptr) {
      BOOST_LOG(error) << "opening microphone '"sv << audio_sink << "' failed. Please set a valid input source in the Sunshine config."sv;
      BOOST_LOG(error) << "Available inputs:"sv;

      for(NSString *name in [AVAudio microphoneNames]) {
        BOOST_LOG(error) << "\t"sv << [name UTF8String];
      }

      return nullptr;
    }

    mic->av_audio_capture = [[AVAudio alloc] init];

    if([mic->av_audio_capture setupMicrophone:audio_capture_device sampleRate:sample_rate frameSize:frame_size channels:channels]) {
      BOOST_LOG(error) << "Failed to setup microphone."sv;
      return nullptr;
    }

    return mic;
  }

  std::optional<sink_t> sink_info() override {
    sink_t sink;

    return sink;
  }
};

std::unique_ptr<audio_control_t> audio_control() {
  return std::make_unique<macos_audio_control_t>();
}
}
