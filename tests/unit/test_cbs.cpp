/**
 * @file tests/unit/test_cbs.cpp
 * @brief Test SPS VUI detection in single-packet and split first-IDR frames.
 */
#include "../tests_common.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <algorithm>
#include <initializer_list>
#include <src/cbs.h>
#include <tuple>
#include <vector>

namespace {
  // Generated offline from one 16x16 black IDR frame (PTS=1), followed immediately
  // by avcodec_send_frame(nullptr), then receiving until EOF. x264/x265 used
  // ultrafast/zerolatency; x265 disabled Info SEI. CBS removed nonessential NALs
  // and serialized SPS variants with and without VUI, including inferred defaults.
  // These fixtures contain only parameter sets and IDR, with no SEI/build metadata.

  /** @brief Ordered packets, each containing a list of payload bytes. */
  using FirstIdrFixture = std::initializer_list<std::initializer_list<uint8_t>>;

  // clang-format off
  /** @brief H.264 packets with SPS VUI present. */
  constexpr FirstIdrFixture h264_vui = {
    {  // SPS with VUI + PPS.
      0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x0a, 0xda, 0x7a, 0x6a, 0x02, 0x02, 0x02, 0x80, 0x00,
      0x00, 0x03, 0x00, 0x80, 0x00, 0x00, 0x3c, 0x47, 0x89, 0x13, 0x50, 0x00, 0x00, 0x00, 0x01, 0x68,
      0xce, 0x0f, 0xc8,
    },
    {  // IDR slice.
      0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x3a, 0x26, 0x28, 0x00, 0x09, 0x02, 0xe0,
    },
  };

  /** @brief H.264 packets with SPS VUI absent. */
  constexpr FirstIdrFixture h264_no_vui = {
    {  // SPS without VUI + PPS.
      0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x0a, 0xda, 0x79, 0x00, 0x00, 0x00, 0x01, 0x68, 0xce,
      0x0f, 0xc8,
    },
    {  // IDR slice.
      0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x3a, 0x26, 0x28, 0x00, 0x09, 0x02, 0xe0,
    },
  };

  /** @brief HEVC packets with SPS VUI present. */
  constexpr FirstIdrFixture hevc_vui = {
    {  // VPS + SPS with VUI + PPS.
      0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00,
      0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x1e, 0xba, 0x02, 0x40, 0x00, 0x00, 0x00, 0x01,
      0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03,
      0x00, 0x1e, 0xa0, 0x88, 0x45, 0x96, 0xea, 0xaf, 0x0b, 0x9a, 0x80, 0x80, 0x80, 0x82, 0x00, 0x00,
      0x03, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x78, 0x10, 0x00, 0x00, 0x00, 0x01, 0x44, 0x01, 0xc0,
      0x71, 0x81, 0x12,
    },
    {  // IDR slice.
      0x00, 0x00, 0x01, 0x28, 0x01, 0xad, 0xe0, 0xd1, 0x17, 0xff, 0x25, 0xbb, 0x80,
    },
  };

  /** @brief HEVC packets with SPS VUI absent. */
  constexpr FirstIdrFixture hevc_no_vui = {
    {  // VPS + SPS without VUI + PPS.
      0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00,
      0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x1e, 0xba, 0x02, 0x40, 0x00, 0x00, 0x00, 0x01,
      0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03,
      0x00, 0x1e, 0xa0, 0x88, 0x45, 0x96, 0xea, 0xaf, 0x0b, 0x20, 0x00, 0x00, 0x00, 0x01, 0x44, 0x01,
      0xc0, 0x71, 0x81, 0x12,
    },
    {  // IDR slice.
      0x00, 0x00, 0x01, 0x28, 0x01, 0xad, 0xe0, 0xd1, 0x17, 0xff, 0x25, 0xbb, 0x80,
    },
  };

  // clang-format on

  /**
   * @brief Select ordered packet payloads with the requested SPS VUI flag.
   * @param codec_id H.264 or HEVC codec identifier.
   * @param has_vui Whether SPS VUI should be present.
   * @return Packet payloads in receive order.
   */
  const FirstIdrFixture &first_idr_fixture(AVCodecID codec_id, bool has_vui) {
    if (codec_id == AV_CODEC_ID_H264) {
      return has_vui ? h264_vui : h264_no_vui;
    }
    return has_vui ? hevc_vui : hevc_no_vui;
  }

  /** @brief Codec identifier, expected VUI presence and whether to split the frame. */
  using SpsTestParam = std::tuple<AVCodecID, bool, bool>;

  /** @brief Check real CBS parsing of single-packet and two-packet first frames. */
  class ValidateSpsTest: public testing::TestWithParam<SpsTestParam> {};

  /**
   * @brief Name each codec, VUI and packet-layout combination.
   * @param info Parameterized test information.
   * @return Descriptive gtest parameter name.
   */
  std::string sps_test_name(const testing::TestParamInfo<SpsTestParam> &info) {
    const auto &[codec_id, has_vui, split] = info.param;
    return std::string(codec_id == AV_CODEC_ID_H264 ? "H264" : "Hevc") +
           (has_vui ? "WithVui" : "WithoutVui") + (split ? "TwoPackets" : "SinglePacket");
  }
}  // namespace

TEST_P(ValidateSpsTest, DetectsVuiInActiveSps) {
  const auto &[codec_id, has_vui, split] = GetParam();
  const auto &fixture = first_idr_fixture(codec_id, has_vui);
  std::vector<std::vector<uint8_t>> payloads(fixture.begin(), fixture.end());
  if (!split) {
    std::vector<uint8_t> combined;
    for (const auto &payload : payloads) {
      combined.insert(combined.end(), payload.begin(), payload.end());
    }
    payloads = {std::move(combined)};
  }

  std::vector<AVPacket *> packets;
  auto free_packets = util::fail_guard([&]() {
    for (auto *packet : packets) {
      av_packet_free(&packet);
    }
  });
  for (size_t i = 0; i < payloads.size(); ++i) {
    auto *packet = av_packet_alloc();
    packets.push_back(packet);
    ASSERT_NE(packet, nullptr);
    ASSERT_EQ(av_new_packet(packet, static_cast<int>(payloads[i].size())), 0);
    std::copy(payloads[i].begin(), payloads[i].end(), packet->data);
    bool contains_idr = i + 1 == payloads.size();
    packet->flags = contains_idr ? AV_PKT_FLAG_KEY : 0;
    packet->pts = packet->dts = contains_idr ? 1 : 0;
  }

  EXPECT_EQ(cbs::validate_sps(packets, codec_id), has_vui);
}

INSTANTIATE_TEST_SUITE_P(
  FirstIdr,
  ValidateSpsTest,
  testing::Combine(testing::Values(AV_CODEC_ID_H264, AV_CODEC_ID_HEVC), testing::Bool(), testing::Bool()),
  sps_test_name
);
