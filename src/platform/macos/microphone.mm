/**
 * @file src/platform/macos/microphone.mm
 * @brief Definitions for microphone capture on macOS.
 */
// local includes
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/platform/macos/av_audio.h"

namespace platf {
  using namespace std::literals;

  struct av_mic_t: public mic_t {
    AVAudio *av_audio_capture {};

    ~av_mic_t() override {
      [av_audio_capture release];
    }

    capture_e sample(std::vector<float> &sample_in) override {
      auto sample_size = sample_in.size();

      uint32_t length = 0;
      void *byteSampleBuffer = TPCircularBufferTail(&av_audio_capture->audioSampleBuffer, &length);

      while (length < sample_size * sizeof(float)) {
        [av_audio_capture.samplesArrivedSignal wait];
        byteSampleBuffer = TPCircularBufferTail(&av_audio_capture->audioSampleBuffer, &length);
      }

      const float *sampleBuffer = (float *) byteSampleBuffer;
      std::vector<float> vectorBuffer(sampleBuffer, sampleBuffer + sample_size);

      std::copy_n(std::begin(vectorBuffer), sample_size, std::begin(sample_in));

      TPCircularBufferConsume(&av_audio_capture->audioSampleBuffer, sample_size * sizeof(float));

      return capture_e::ok;
    }
  };

  struct av_mic_output_t: public mic_output_t {
    AVAudio *av_audio_output;
    std::string device_name;
    bool started = false;

    av_mic_output_t(int channels, std::uint32_t sample_rate, const std::string &dev_name) 
      : device_name(dev_name) {
      
      av_audio_output = [[AVAudio alloc] init];
      
      AVCaptureDevice *output_device = nullptr;
      if (!device_name.empty() && device_name != "default") {
        output_device = [AVAudio findMicrophone:[NSString stringWithUTF8String:device_name.c_str()]];
      }
      
      if (!output_device) {
        output_device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];
      }
      
      if ([av_audio_output setupMicrophone:output_device sampleRate:sample_rate frameSize:960 channels:channels]) {
        BOOST_LOG(error) << "Failed to setup microphone output device."sv;
        [av_audio_output release];
        av_audio_output = nullptr;
      }
    }

    int output_samples(const std::vector<float> &frame_buffer) override {
      if (!av_audio_output || !started) {
        return -1;
      }

      // For macOS, we would need to implement audio output through Core Audio
      // This is a simplified placeholder - real implementation would need
      // Audio Queue Services or Audio Unit for output
      BOOST_LOG(debug) << "Outputting " << frame_buffer.size() << " audio samples"sv;
      return 0;
    }

    int start() override {
      started = av_audio_output != nullptr;
      return started ? 0 : -1;
    }

    int stop() override {
      started = false;
      return 0;
    }

    ~av_mic_output_t() override {
      stop();
      if (av_audio_output) {
        [av_audio_output release];
      }
    }
  };

  struct macos_audio_control_t: public audio_control_t {
    AVCaptureDevice *audio_capture_device {};

  public:
    int set_sink(const std::string &sink) override {
      BOOST_LOG(warning) << "audio_control_t::set_sink() unimplemented: "sv << sink;
      return 0;
    }

    std::unique_ptr<mic_t> microphone(const std::uint8_t *mapping, int channels, std::uint32_t sample_rate, std::uint32_t frame_size) override {
      auto mic = std::make_unique<av_mic_t>();
      const char *audio_sink = "";

      if (!config::audio.sink.empty()) {
        audio_sink = config::audio.sink.c_str();
      }

      if ((audio_capture_device = [AVAudio findMicrophone:[NSString stringWithUTF8String:audio_sink]]) == nullptr) {
        BOOST_LOG(error) << "opening microphone '"sv << audio_sink << "' failed. Please set a valid input source in the Sunshine config."sv;
        BOOST_LOG(error) << "Available inputs:"sv;

        for (NSString *name in [AVAudio microphoneNames]) {
          BOOST_LOG(error) << "\t"sv << [name UTF8String];
        }

        return nullptr;
      }

      mic->av_audio_capture = [[AVAudio alloc] init];

      if ([mic->av_audio_capture setupMicrophone:audio_capture_device sampleRate:sample_rate frameSize:frame_size channels:channels]) {
        BOOST_LOG(error) << "Failed to setup microphone."sv;
        return nullptr;
      }

      return mic;
    }

    std::unique_ptr<mic_output_t> mic_output(int channels, std::uint32_t sample_rate, const std::string &device_name) override {
      return std::make_unique<av_mic_output_t>(channels, sample_rate, device_name);
    }

    bool is_sink_available(const std::string &sink) override {
      BOOST_LOG(warning) << "audio_control_t::is_sink_available() unimplemented: "sv << sink;
      return true;
    }

    std::optional<sink_t> sink_info() override {
      sink_t sink;

      return sink;
    }

    int create_virtual_microphone(const std::string &virtual_mic_name = "sunshine-virtual-mic") override {
      BOOST_LOG(warning) << "Virtual microphone creation not implemented on macOS yet";
      return -1;
    }
    
    int setup_virtual_mic_loopback(const std::string &virtual_mic_name = "sunshine-virtual-mic") override {
      BOOST_LOG(warning) << "Virtual microphone loopback not implemented on macOS yet";
      return -1;
    }
  };

  std::unique_ptr<audio_control_t> audio_control() {
    return std::make_unique<macos_audio_control_t>();
  }
}  // namespace platf
