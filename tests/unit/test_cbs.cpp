/**
 * @file tests/unit/test_cbs.cpp
 * @brief Test H.264 and HEVC parameter-set rewriting and single- or multi-packet SPS VUI detection.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <initializer_list>
#include <span>
#include <string_view>
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
  ASSERT_TRUE(cbs::validate_sps(std::array {packet.get()}, codec_id));

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
  EXPECT_TRUE(cbs::validate_sps(std::array {rewritten_packet.get()}, codec_id));
}

INSTANTIATE_TEST_SUITE_P(SoftwareCodecs, CodedBitstreamTest, testing::Values(AV_CODEC_ID_H264, AV_CODEC_ID_HEVC));

namespace {
  using namespace std::literals;

  /**
   * @brief Annex B packets in FFmpeg receive order, retaining embedded NUL bytes.
   *
   * String views exclude the string-literal terminator. Parameter sets configure
   * decoding; the IDR packet supplies the picture. The SPS VUI flag is varied to
   * test whether the parser detects Video Usability Information metadata.
   * Char arrays are not used in case clang-format puts each char on a separate line.
   */
  using FirstIdrFixture = std::initializer_list<std::string_view>;

  /**
   * @brief H.264 packets with SPS Video Usability Information (VUI) present.
   */
  constexpr FirstIdrFixture h264_vui {
    // Packet 1: decoder parameter sets.
    // Sequence parameter set (SPS); starts with an Annex B delimiter.
    "\x00\x00\x00\x01\x67\x42\xc0\x0a\xda\x7a\x6a\x02\x02\x02\x80\x00"
    "\x00\x03\x00\x80\x00\x00\x3c\x47\x89\x13\x50"
    // Picture parameter set (PPS); starts with an Annex B delimiter.
    "\x00\x00\x00\x01\x68\xce\x0f\xc8"sv,
    // Packet 2: instantaneous decoder refresh (IDR) picture.
    // IDR slice; starts with an Annex B delimiter.
    "\x00\x00\x01\x65\x88\x84\x3a\x26\x28\x00\x09\x02\xe0"sv,
  };

  /**
   * @brief H.264 packets with SPS Video Usability Information (VUI) absent.
   */
  constexpr FirstIdrFixture h264_no_vui {
    // Packet 1: decoder parameter sets.
    // Sequence parameter set (SPS); starts with an Annex B delimiter.
    "\x00\x00\x00\x01\x67\x42\xc0\x0a\xda\x79"
    // Picture parameter set (PPS); starts with an Annex B delimiter.
    "\x00\x00\x00\x01\x68\xce\x0f\xc8"sv,
    // Packet 2: instantaneous decoder refresh (IDR) picture.
    // IDR slice; starts with an Annex B delimiter.
    "\x00\x00\x01\x65\x88\x84\x3a\x26\x28\x00\x09\x02\xe0"sv,
  };

  /**
   * @brief HEVC packets with SPS Video Usability Information (VUI) present.
   */
  constexpr FirstIdrFixture hevc_vui {
    // Packet 1: decoder parameter sets.
    // Video parameter set (VPS); starts with an Annex B delimiter.
    "\x00\x00\x00\x01\x40\x01\x0c\x01\xff\xff\x01\x60\x00\x00\x03\x00"
    "\x90\x00\x00\x03\x00\x00\x03\x00\x1e\xba\x02\x40"
    // Sequence parameter set (SPS); starts with an Annex B delimiter.
    "\x00\x00\x00\x01\x42\x01\x01\x01\x60\x00\x00\x03\x00\x90\x00\x00"
    "\x03\x00\x00\x03\x00\x1e\xa0\x88\x45\x96\xea\xaf\x0b\x9a\x80\x80"
    "\x80\x82\x00\x00\x03\x00\x02\x00\x00\x03\x00\x78\x10"
    // Picture parameter set (PPS); starts with an Annex B delimiter.
    "\x00\x00\x00\x01\x44\x01\xc0\x71\x81\x12"sv,
    // Packet 2: instantaneous decoder refresh (IDR) picture.
    // IDR slice; starts with an Annex B delimiter.
    "\x00\x00\x01\x28\x01\xad\xe0\xd1\x17\xff\x25\xbb\x80"sv,
  };

  /**
   * @brief HEVC packets with SPS Video Usability Information (VUI) absent.
   */
  constexpr FirstIdrFixture hevc_no_vui {
    // Packet 1: decoder parameter sets.
    // Video parameter set (VPS); starts with an Annex B delimiter.
    "\x00\x00\x00\x01\x40\x01\x0c\x01\xff\xff\x01\x60\x00\x00\x03\x00"
    "\x90\x00\x00\x03\x00\x00\x03\x00\x1e\xba\x02\x40"
    // Sequence parameter set (SPS); starts with an Annex B delimiter.
    "\x00\x00\x00\x01\x42\x01\x01\x01\x60\x00\x00\x03\x00\x90\x00\x00"
    "\x03\x00\x00\x03\x00\x1e\xa0\x88\x45\x96\xea\xaf\x0b\x20"
    // Picture parameter set (PPS); starts with an Annex B delimiter.
    "\x00\x00\x00\x01\x44\x01\xc0\x71\x81\x12"sv,
    // Packet 2: instantaneous decoder refresh (IDR) picture.
    // IDR slice; starts with an Annex B delimiter.
    "\x00\x00\x01\x28\x01\xad\xe0\xd1\x17\xff\x25\xbb\x80"sv,
  };

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
   * @param test_info Parameterized test information.
   * @return Descriptive gtest parameter name.
   */
  std::string sps_test_name(const testing::TestParamInfo<SpsTestParam> &test_info) {
    const auto &[codec_id, has_vui, split] = test_info.param;
    return std::string(codec_id == AV_CODEC_ID_H264 ? "H264" : "Hevc") +
           (has_vui ? "WithVui" : "WithoutVui") + (split ? "TwoPackets" : "SinglePacket");
  }
}  // namespace

TEST_P(ValidateSpsTest, DetectsVuiInActiveSps) {
  const auto &[codec_id, has_vui, split] = GetParam();
  const auto &fixture = first_idr_fixture(codec_id, has_vui);
  std::vector<std::vector<uint8_t>> payloads;
  for (const auto packet_bytes : fixture) {
    payloads.emplace_back(packet_bytes.begin(), packet_bytes.end());
  }
  if (!split) {
    std::vector<uint8_t> combined;
    for (const auto &payload : payloads) {
      combined.insert(combined.end(), payload.begin(), payload.end());
    }
    payloads = {std::move(combined)};
  }

  std::vector<AVPacket *> packets;
  auto free_packets = util::fail_guard([&packets]() {
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
    packet->pts = contains_idr ? 1 : 0;
    packet->dts = packet->pts;
  }

  EXPECT_EQ(cbs::validate_sps(packets, codec_id), has_vui);
}

INSTANTIATE_TEST_SUITE_P(
  FirstIdr,
  ValidateSpsTest,
  testing::Combine(testing::Values(AV_CODEC_ID_H264, AV_CODEC_ID_HEVC), testing::Bool(), testing::Bool()),
  sps_test_name
);
