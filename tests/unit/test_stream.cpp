/**
 * @file tests/unit/test_stream.cpp
 * @brief Test src/stream.*
 */

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace stream {
  std::vector<uint8_t> concat_and_insert(uint64_t insert_size, uint64_t slice_size, const std::string_view &data1, const std::string_view &data2);

  std::string_view metrics_csv_header();
  std::string format_metrics_csv_row(
    const std::string_view &payload,
    int64_t timestamp_ms,
    uint32_t session_id,
    int bitrate_kbps,
    int idr_count
  );
  std::string make_metrics_csv_filename(uint32_t session_id, int64_t timestamp_ms);
}

#include "../tests_common.h"

TEST(ConcatAndInsertTests, ConcatNoInsertionTest) {
  char b1[] = {'a', 'b'};
  char b2[] = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(0, 2, std::string_view {b1, sizeof(b1)}, std::string_view {b2, sizeof(b2)});
  auto expected = std::vector<uint8_t> {'a', 'b', 'c', 'd', 'e'};
  ASSERT_EQ(res, expected);
}

TEST(ConcatAndInsertTests, ConcatLargeStrideTest) {
  char b1[] = {'a', 'b'};
  char b2[] = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(1, sizeof(b1) + sizeof(b2) + 1, std::string_view {b1, sizeof(b1)}, std::string_view {b2, sizeof(b2)});
  auto expected = std::vector<uint8_t> {0, 'a', 'b', 'c', 'd', 'e'};
  ASSERT_EQ(res, expected);
}

TEST(ConcatAndInsertTests, ConcatSmallStrideTest) {
  char b1[] = {'a', 'b'};
  char b2[] = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(1, 1, std::string_view {b1, sizeof(b1)}, std::string_view {b2, sizeof(b2)});
  auto expected = std::vector<uint8_t> {0, 'a', 0, 'b', 0, 'c', 0, 'd', 0, 'e'};
  ASSERT_EQ(res, expected);
}

namespace {
  // Builds a 21-byte SS_FRAME_FEC_STATUS payload in the wire (big-endian) format.
  std::array<uint8_t, 21> build_fec_status_payload(
    uint32_t frame_index,
    uint16_t highest_received,
    uint16_t next_contiguous,
    uint16_t missing,
    uint16_t total_data,
    uint16_t total_parity,
    uint16_t received_data,
    uint16_t received_parity,
    uint8_t fec_percentage,
    uint8_t multi_fec_index,
    uint8_t multi_fec_count
  ) {
    auto put_u16_be = [](std::array<uint8_t, 21> &out, size_t off, uint16_t v) {
      out[off] = static_cast<uint8_t>((v >> 8) & 0xFF);
      out[off + 1] = static_cast<uint8_t>(v & 0xFF);
    };

    std::array<uint8_t, 21> payload {};
    payload[0] = static_cast<uint8_t>((frame_index >> 24) & 0xFF);
    payload[1] = static_cast<uint8_t>((frame_index >> 16) & 0xFF);
    payload[2] = static_cast<uint8_t>((frame_index >> 8) & 0xFF);
    payload[3] = static_cast<uint8_t>(frame_index & 0xFF);
    put_u16_be(payload, 4, highest_received);
    put_u16_be(payload, 6, next_contiguous);
    put_u16_be(payload, 8, missing);
    put_u16_be(payload, 10, total_data);
    put_u16_be(payload, 12, total_parity);
    put_u16_be(payload, 14, received_data);
    put_u16_be(payload, 16, received_parity);
    payload[18] = fec_percentage;
    payload[19] = multi_fec_index;
    payload[20] = multi_fec_count;
    return payload;
  }
}  // namespace

TEST(MetricsCsvTests, HeaderHasAllColumns) {
  auto header = stream::metrics_csv_header();
  EXPECT_NE(header.find("timestamp_ms"), std::string_view::npos);
  EXPECT_NE(header.find("session_id"), std::string_view::npos);
  EXPECT_NE(header.find("bitrate_kbps"), std::string_view::npos);
  EXPECT_NE(header.find("frame_index"), std::string_view::npos);
  EXPECT_NE(header.find("missing_packets"), std::string_view::npos);
  EXPECT_NE(header.find("total_data_packets"), std::string_view::npos);
  EXPECT_NE(header.find("received_data_packets"), std::string_view::npos);
  EXPECT_NE(header.find("total_parity_packets"), std::string_view::npos);
  EXPECT_NE(header.find("received_parity_packets"), std::string_view::npos);
  EXPECT_NE(header.find("fec_percentage"), std::string_view::npos);
  EXPECT_NE(header.find("idr_request_count"), std::string_view::npos);
  EXPECT_EQ(header.find('\n'), std::string_view::npos);  // header has no trailing newline
}

TEST(MetricsCsvTests, RowDecodesBigEndianFields) {
  auto payload = build_fec_status_payload(
    /*frame_index=*/0x12345678u,  // 305419896
    /*highest_received=*/100,
    /*next_contiguous=*/95,
    /*missing=*/5,
    /*total_data=*/50,
    /*total_parity=*/10,
    /*received_data=*/45,
    /*received_parity=*/9,
    /*fec_percentage=*/20,
    /*multi_fec_index=*/0,
    /*multi_fec_count=*/1
  );

  auto row = stream::format_metrics_csv_row(
    std::string_view {reinterpret_cast<const char *>(payload.data()), payload.size()},
    /*timestamp_ms=*/1000,
    /*session_id=*/42,
    /*bitrate_kbps=*/20000,
    /*idr_count=*/3
  );

  EXPECT_EQ(row, "1000,42,20000,305419896,5,50,45,10,9,20,3");
}

TEST(MetricsCsvTests, RowReturnsEmptyOnShortPayload) {
  std::array<uint8_t, 10> short_payload {};
  auto row = stream::format_metrics_csv_row(
    std::string_view {reinterpret_cast<const char *>(short_payload.data()), short_payload.size()},
    1000,
    42,
    20000,
    0
  );
  EXPECT_TRUE(row.empty());
}

TEST(MetricsCsvTests, RowEmbedsSessionMetadata) {
  auto payload = build_fec_status_payload(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

  auto row = stream::format_metrics_csv_row(
    std::string_view {reinterpret_cast<const char *>(payload.data()), payload.size()},
    /*timestamp_ms=*/1713045600000LL,
    /*session_id=*/7,
    /*bitrate_kbps=*/55388,
    /*idr_count=*/12
  );

  // Leading fields come from session metadata, not from the FEC payload.
  EXPECT_EQ(row.substr(0, row.find(',')), "1713045600000");
  EXPECT_NE(row.find(",7,"), std::string::npos);
  EXPECT_NE(row.find(",55388,"), std::string::npos);
  // idr_count is the trailing field
  EXPECT_EQ(row.substr(row.rfind(',') + 1), "12");
}

TEST(MetricsCsvTests, FilenameFormat) {
  EXPECT_EQ(
    stream::make_metrics_csv_filename(42, 1713045600000LL),
    "sunshine_metrics_42_1713045600000.csv"
  );
}
