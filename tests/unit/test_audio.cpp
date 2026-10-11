/**
 * @file tests/unit/test_audio.cpp
 * @brief Test src/audio.*.
 */

// test includes
#include "../tests_common.h"

// local includes
#include <src/audio.h>

using namespace audio;

TEST(AudioLayoutTest, CustomMappingRemainsContiguousAndUsesAllEightChannels) {
  const stream_params_t params {8, 6, 2, {0, 1, 4, 5, 2, 3, 6, 7}};
  auto copy = params;
  EXPECT_EQ(copy.channelCount, 8);
  EXPECT_EQ(copy.streams, 6);
  EXPECT_EQ(copy.coupledStreams, 2);
  EXPECT_EQ(copy.mapping.size(), 8U);
  EXPECT_EQ(copy.mapping.back(), 7);
  EXPECT_EQ(copy.mapping.data() + 7, &copy.mapping[7]);
  copy.mapping[0] = 7;
  EXPECT_EQ(params.mapping[0], 0);
  EXPECT_EQ(stream_configs.size(), MAX_STREAM_CONFIG);
  EXPECT_EQ(stream_configs[HIGH_SURROUND71].channelCount, 8);
}

struct AudioTest: PlatformTestSuite, testing::WithParamInterface<std::tuple<std::basic_string_view<char>, config_t>> {
  void SetUp() override {
    BaseTest::SetUp();
    m_config = std::get<1>(GetParam());
    m_mail = std::make_shared<safe::mail_raw_t>();
  }

  config::settings_t settings;  ///< Host audio settings owned by this fixture.
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
    std::make_tuple("HIGH_STEREO", config_t {.packetDuration = 5, .channels = 2, .mask = 0x3, .customStreamParams = {}, .flags = config_flags(config_t::HIGH_QUALITY)}),
    std::make_tuple("SURROUND51", config_t {.packetDuration = 5, .channels = 6, .mask = 0x3F, .customStreamParams = {}, .flags = config_flags()}),
    std::make_tuple("SURROUND71", config_t {.packetDuration = 5, .channels = 8, .mask = 0x63F, .customStreamParams = {}, .flags = config_flags()}),
    std::make_tuple("SURROUND51_CUSTOM", config_t {5, 6, 0x3F, {6, 4, 2, {0, 1, 4, 5, 2, 3}}, config_flags(config_t::CUSTOM_SURROUND_PARAMS)})
  ),
  [](const auto &info) {
    return std::string(std::get<0>(info.param));
  }
);

TEST_P(AudioTest, TestEncode) {
  std::jthread timer([&] {
    // Terminate the audio capture after 100 ms
    std::this_thread::sleep_for(100ms);
    const auto shutdown_event = m_mail->event<bool>(mail::shutdown);
    const auto audio_packets = m_mail->queue<packet_t>(mail::audio_packets);
    shutdown_event->raise(true);
    audio_packets->stop();
  });
  std::jthread capture([&] {
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
  audio::service_t audios {settings, m_mail};
  audios.capture(m_mail, m_config, nullptr);

  timer.join();
  capture.join();
}

namespace {
  /** @brief Fake controller retaining observed routing and initialization state. */
  struct fake_audio_control_t: platf::audio_control_t {
    std::vector<std::string> &restored;  ///< Sink restoration requests observed by the fixture.
    std::optional<platf::sink_t> sinks;  ///< Sink discovery result injected by the fixture.

    /** @brief Bind fixture state. @param restored Observed sink changes. @param sinks Sink discovery result. */
    fake_audio_control_t(std::vector<std::string> &restored, std::optional<platf::sink_t> sinks):
        restored {restored},
        sinks {std::move(sinks)} {}

    /** @brief Record a routing request. @param sink Requested endpoint. @return Zero on success. */
    int set_sink(const std::string &sink) override {
      restored.push_back(sink);
      return 0;
    }

    /** @brief Supply no microphone because these tests exercise routing ownership. @return Null microphone. */
    std::unique_ptr<platf::mic_t> microphone(const std::uint8_t *, int, std::uint32_t, std::uint32_t, bool, bool) override {
      return nullptr;
    }

    /** @brief Report fixture endpoints as available. @param sink Endpoint to inspect. @return Whether the endpoint is recognized. */
    bool is_sink_available(const std::string &sink) override {
      return sink == "original" || sink == "configured";
    }

    /** @brief Return the configured discovery result. @return Injected sink information. */
    std::optional<platf::sink_t> sink_info() override {
      return sinks;
    }
  };
}  // namespace

/** @brief Context references retain shared control after its service is destroyed and restore once. */
TEST(AudioOwnerTest, RetainedContextSurvivesOwnerAndRestoresOriginalSink) {
  config::settings_t settings;
  std::vector<std::string> restored;
  audio::audio_ctx_ref_t retained;
  {
    audio::service_t owner {settings, std::make_shared<safe::mail_raw_t>(), [&restored]() {
                              return std::make_unique<fake_audio_control_t>(restored, platf::sink_t {"original", {}});
                            }};
    retained = owner.context();
    auto second = owner.context();
    ASSERT_TRUE(retained);
    EXPECT_EQ(retained.get(), second.get());
    EXPECT_TRUE(audio::is_audio_ctx_sink_available(*retained));
    retained->restore_sink = true;
  }
  EXPECT_TRUE(restored.empty());
  retained.reset();
  EXPECT_EQ(restored, std::vector<std::string> {"original"});
}

/** @brief Failed lazy initialization can be retried after references release their failed attempt. */
TEST(AudioOwnerTest, FailedInitializationAndSinkDiscoveryCanBeRetried) {
  config::settings_t settings;
  settings.audio.sink = "configured";
  std::vector<std::string> restored;
  int attempts {};
  audio::service_t owner {settings, std::make_shared<safe::mail_raw_t>(), [&attempts, &restored]() -> std::unique_ptr<platf::audio_control_t> {
                            ++attempts;
                            if (attempts == 1) {
                              return nullptr;
                            }
                            return std::make_unique<fake_audio_control_t>(restored, attempts == 2 ? std::nullopt : std::optional<platf::sink_t> {platf::sink_t {}});
                          }};
  {
    auto failed = owner.context();
    ASSERT_TRUE(failed);
    EXPECT_FALSE(audio::is_audio_ctx_sink_available(*failed));
  }
  {
    auto failed = owner.context();
    ASSERT_TRUE(failed);
    EXPECT_FALSE(failed->control);
  }
  auto succeeded = owner.context();
  ASSERT_TRUE(succeeded);
  ASSERT_TRUE(succeeded->control);
  EXPECT_EQ(attempts, 3);
  EXPECT_TRUE(audio::is_audio_ctx_sink_available(*succeeded));
  succeeded->restore_sink = true;
  succeeded.reset();
  EXPECT_EQ(restored, std::vector<std::string> {"configured"});
}

/** @brief Independent audio owners preserve separate original sinks and capture contexts. */
TEST(AudioOwnerTest, IndependentOwnersDoNotShareControlOrRestoration) {
  config::settings_t first_settings;
  config::settings_t second_settings;
  std::vector<std::string> first_restored;
  std::vector<std::string> second_restored;
  audio::service_t first {first_settings, std::make_shared<safe::mail_raw_t>(), [&first_restored]() {
                            return std::make_unique<fake_audio_control_t>(first_restored, platf::sink_t {"first", {}});
                          }};
  audio::service_t second {second_settings, std::make_shared<safe::mail_raw_t>(), [&second_restored]() {
                             return std::make_unique<fake_audio_control_t>(second_restored, platf::sink_t {"second", {}});
                           }};
  auto first_context = first.context();
  auto second_context = second.context();
  ASSERT_TRUE(first_context);
  ASSERT_TRUE(second_context);
  EXPECT_NE(first_context.get(), second_context.get());
  first_context->restore_sink = true;
  first_context.reset();
  EXPECT_EQ(first_restored, std::vector<std::string> {"first"});
  EXPECT_TRUE(second_restored.empty());
}

/** @brief Empty and unavailable endpoints cannot be restored or reported as available. */
TEST(AudioOwnerTest, EmptyAndUnavailableSinksKeepRoutingUnchanged) {
  config::settings_t settings;
  std::vector<std::string> restored;
  audio::service_t owner {settings, std::make_shared<safe::mail_raw_t>(), [&restored]() {
                            return std::make_unique<fake_audio_control_t>(restored, platf::sink_t {});
                          }};
  auto empty = owner.context();
  ASSERT_TRUE(empty);
  EXPECT_FALSE(audio::is_audio_ctx_sink_available(*empty));
  empty->restore_sink = true;
  empty.reset();
  EXPECT_TRUE(restored.empty());
  auto unavailable = owner.context();
  ASSERT_TRUE(unavailable);
  unavailable->sink.host = "unavailable";
  EXPECT_FALSE(audio::is_audio_ctx_sink_available(*unavailable));
}

/** @brief Disabled host audio completes shutdown without initializing an audio backend. */
TEST(AudioOwnerTest, DisabledCaptureDoesNotInitializeDevices) {
  config::settings_t settings;
  settings.audio.stream = false;
  auto host_mail = std::make_shared<safe::mail_raw_t>();
  auto session_mail = std::make_shared<safe::mail_raw_t>();
  const auto shutdown_event = session_mail->event<bool>(mail::shutdown);
  shutdown_event->raise(true);
  int attempts {};
  audio::service_t owner {settings, host_mail, [&attempts]() -> std::unique_ptr<platf::audio_control_t> {
                            ++attempts;
                            return nullptr;
                          }};
  owner.capture(session_mail, {}, nullptr);
  EXPECT_EQ(attempts, 0);
}
