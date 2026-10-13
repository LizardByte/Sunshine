/**
 * @file tests/unit/test_cbs.cpp
 * @brief Regression tests for typed H.264 and HEVC parameter-set serialization.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <memory>
#include <span>
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
  ASSERT_TRUE(cbs::validate_sps(packet.get(), codec_id));

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
  EXPECT_TRUE(cbs::validate_sps(rewritten_packet.get(), codec_id));
}

INSTANTIATE_TEST_SUITE_P(SoftwareCodecs, CodedBitstreamTest, testing::Values(AV_CODEC_ID_H264, AV_CODEC_ID_HEVC));
