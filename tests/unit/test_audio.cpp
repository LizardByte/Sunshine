/**
 * @file tests/unit/test_audio.cpp
 * @brief Test src/audio.*.
 */
#include "../tests_common.h"

#include <src/audio.h>

using namespace audio;

struct AudioTest: PlatformTestSuite, testing::WithParamInterface<std::tuple<std::basic_string_view<char>, config_t>> {
  void SetUp() override {
    m_config = std::get<1>(GetParam());
    m_mail = std::make_shared<safe::mail_raw_t>();
    // Check if audio capture is available on this platform/configuration,
    // mirroring the pattern in EncoderTest::SetUp which skips when a hardware
    // encoder is unavailable. On macOS with an empty audio sink the microphone()
    // call exercises the Core Audio system-tap path; if the tap cannot be set up
    // (e.g. permission not yet granted, or running in a VM without real audio
    // hardware), microphone() returns nullptr and we skip rather than hang.
    auto control = platf::audio_control();
    if (!control) {
      GTEST_SKIP() << "Audio control not available";
    }
    const auto &stream = audio::stream_configs[audio::STEREO];
    constexpr std::uint32_t kProbeFrameSize = 240;  // 5 ms at 48 kHz
    auto mic = control->microphone(stream.mapping, stream.channelCount, stream.sampleRate, kProbeFrameSize, false, false);
    if (!mic) {
      GTEST_SKIP() << "Audio capture not available";
    }
  }

  config_t m_config;
  safe::mail_t m_mail;
};

constexpr std::bitset<config_t::MAX_FLAGS> config_flags(const int flag = -1) {
  auto result = std::bitset<config_t::MAX_FLAGS>();
  if (flag >= 0) {
    result.set(flag);
  }
  return result;
}

INSTANTIATE_TEST_SUITE_P(
  Configurations,
  AudioTest,
  testing::Values(
    std::make_tuple("HIGH_STEREO", config_t {5, 2, 0x3, {0}, config_flags(config_t::HIGH_QUALITY)}),
    std::make_tuple("SURROUND51", config_t {5, 6, 0x3F, {0}, config_flags()}),
    std::make_tuple("SURROUND71", config_t {5, 8, 0x63F, {0}, config_flags()}),
    std::make_tuple("SURROUND51_CUSTOM", config_t {5, 6, 0x3F, {6, 4, 2, {0, 1, 4, 5, 2, 3}}, config_flags(config_t::CUSTOM_SURROUND_PARAMS)})
  ),
  [](const auto &info) {
    return std::string(std::get<0>(info.param));
  }
);

TEST_P(AudioTest, TestEncode) {
  std::thread timer([&] {
    // Terminate the audio capture after 100 ms
    std::this_thread::sleep_for(100ms);
    const auto shutdown_event = m_mail->event<bool>(mail::shutdown);
    const auto audio_packets = m_mail->queue<packet_t>(mail::audio_packets);
    shutdown_event->raise(true);
    audio_packets->stop();
  });
  std::thread capture([&] {
    const auto packets = m_mail->queue<packet_t>(mail::audio_packets);
    const auto shutdown_event = m_mail->event<bool>(mail::shutdown);
    while (const auto packet = packets->pop()) {
      if (shutdown_event->peek()) {
        break;
      }
      if (auto packet_data = packet->second; packet_data.size() == 0) {
        FAIL() << "Empty packet data";
      }
    }
  });
  audio::capture(m_mail, m_config, nullptr);

  timer.join();
  capture.join();
}
