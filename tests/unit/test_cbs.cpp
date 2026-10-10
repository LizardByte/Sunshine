/**
 * @file tests/unit/test_cbs.cpp
 * @brief Test H.264 and HEVC parameter-set rewriting and single- or multi-packet SPS VUI detection.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <initializer_list>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

// local includes
#include <src/cbs.h>
#include <src/video.h>

namespace {
  /**
   * @brief Release a packet allocated by FFmpeg.
   * @param packet Packet whose encoded payload is released.
   */
  void release_packet(AVPacket *packet) {
    av_packet_free(&packet);
  }

  /**
   * @brief Verify parameter-set rewriting with real software-encoder output.
   */
  struct CodedBitstreamTest: testing::TestWithParam<AVCodecID> {};
}  // namespace

TEST_P(CodedBitstreamTest, RewrittenParameterSetsRetainValidVuiMetadata) {
  const auto codec_id = GetParam();
  const auto *codec = avcodec_find_encoder_by_name(codec_id == AV_CODEC_ID_H264 ? "libx264" : "libx265");
  ASSERT_NE(codec, nullptr);
  video::avcodec_ctx_t context {avcodec_alloc_context3(codec)};
  ASSERT_NE(context.get(), nullptr);
  context->width = 64;
  context->height = 64;
  context->pix_fmt = AV_PIX_FMT_YUV420P;
  context->time_base = {1, 30};
  context->framerate = {30, 1};
  context->gop_size = 1;
  context->max_b_frames = 0;
  context->refs = 1;
  context->color_range = AVCOL_RANGE_MPEG;
  context->color_primaries = AVCOL_PRI_BT709;
  context->color_trc = AVCOL_TRC_BT709;
  context->colorspace = AVCOL_SPC_BT709;

  AVDictionary *options = nullptr;
  auto free_options = util::fail_guard([&options] {
    av_dict_free(&options);
  });
  av_dict_set(&options, "preset", "ultrafast", 0);
  av_dict_set(&options, "tune", "zerolatency", 0);
  if (codec_id == AV_CODEC_ID_H264) {
    av_dict_set(&options, "x264-params", "repeat-headers=1:threads=1", 0);
  } else {
    av_dict_set(&options, "x265-params", "repeat-headers=1:pools=none:frame-threads=1", 0);
  }
  ASSERT_EQ(avcodec_open2(context.get(), codec, &options), 0);

  video::avcodec_frame_t frame {av_frame_alloc()};
  ASSERT_NE(frame.get(), nullptr);
  frame->width = context->width;
  frame->height = context->height;
  frame->format = context->pix_fmt;
  frame->pts = 0;
  ASSERT_EQ(av_frame_get_buffer(frame.get(), 0), 0);
  for (int plane = 0; plane < 3; ++plane) {
    const auto height = plane == 0 ? frame->height : frame->height / 2;
    std::fill_n(frame->data[plane], frame->linesize[plane] * height, plane == 0 ? 16 : 128);
  }
  ASSERT_EQ(avcodec_send_frame(context.get(), frame.get()), 0);
  util::safe_ptr<AVPacket, &release_packet> packet {av_packet_alloc()};
  ASSERT_NE(packet.get(), nullptr);
  ASSERT_EQ(avcodec_receive_packet(context.get(), packet.get()), 0);
  ASSERT_TRUE(cbs::validate_sps({packet.get()}, codec_id));

  const std::vector<std::uint8_t> original_packet(packet->data, packet->data + packet->size);
  std::vector<std::uint8_t> rewritten(packet->data, packet->data + packet->size);
  const auto replace_unit = [&rewritten](const cbs::nal_t &unit) {
    const auto original = std::ranges::search(rewritten, unit.old);
    if (original.empty()) {
      return false;
    }
    const auto position = rewritten.erase(original.begin(), original.end());
    rewritten.insert(position, unit._new.begin(), unit._new.end());
    return true;
  };
  if (codec_id == AV_CODEC_ID_H264) {
    const auto result = cbs::make_sps_h264(context.get(), packet.get());
    ASSERT_GT(result.sps.old.size(), 0U);
    ASSERT_GT(result.sps._new.size(), 0U);
    ASSERT_TRUE(replace_unit(result.sps));
  } else {
    const auto result = cbs::make_sps_hevc(context.get(), packet.get());
    ASSERT_GT(result.vps.old.size(), 0U);
    ASSERT_GT(result.vps._new.size(), 0U);
    ASSERT_GT(result.sps.old.size(), 0U);
    ASSERT_GT(result.sps._new.size(), 0U);
    ASSERT_TRUE(replace_unit(result.vps));
    ASSERT_TRUE(replace_unit(result.sps));
  }
  EXPECT_TRUE(std::ranges::equal(original_packet, std::span(packet->data, packet->size)));
  util::safe_ptr<AVPacket, &release_packet> rewritten_packet {av_packet_alloc()};
  ASSERT_NE(rewritten_packet.get(), nullptr);
  ASSERT_EQ(av_new_packet(rewritten_packet.get(), static_cast<int>(rewritten.size())), 0);
  std::ranges::copy(rewritten, rewritten_packet->data);
  EXPECT_TRUE(cbs::validate_sps({rewritten_packet.get()}, codec_id));
}

INSTANTIATE_TEST_SUITE_P(SoftwareCodecs, CodedBitstreamTest, testing::Values(AV_CODEC_ID_H264, AV_CODEC_ID_HEVC));

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
