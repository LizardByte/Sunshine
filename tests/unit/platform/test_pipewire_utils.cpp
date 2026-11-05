/**
 * @file tests/unit/platform/test_pipewire_utils.cpp
 * @brief Test src/platform/linux/pipewire_utils.h.
 */
#ifdef __linux__

  #include "../../tests_common.h"

  #include <algorithm>
  #include <array>
  #include <chrono>
  #include <src/platform/linux/pipewire_utils.h>
  #include <utility>
  #include <vector>

/**
 * @brief The software conversion path is selected for software encoders and for
 *        Vulkan without a DMA-BUF capable device.
 */
TEST(PipeWireUtilsTests, SoftwareConversionSelected) {
  using enum platf::mem_type_e;

  EXPECT_TRUE(pipewire::software_conversion_selected(system, false));
  EXPECT_TRUE(pipewire::software_conversion_selected(system, true));

  // Vulkan without a DMA-BUF capable device falls back to the software path.
  EXPECT_TRUE(pipewire::software_conversion_selected(vulkan, false));
  EXPECT_FALSE(pipewire::software_conversion_selected(vulkan, true));

  // Hardware paths convert outside the software path.
  EXPECT_FALSE(pipewire::software_conversion_selected(vaapi, false));
  EXPECT_FALSE(pipewire::software_conversion_selected(vaapi, true));
  EXPECT_FALSE(pipewire::software_conversion_selected(cuda, false));
  EXPECT_FALSE(pipewire::software_conversion_selected(cuda, true));

  EXPECT_FALSE(pipewire::software_conversion_selected(dxgi, false));
  EXPECT_FALSE(pipewire::software_conversion_selected(videotoolbox, false));
  EXPECT_FALSE(pipewire::software_conversion_selected(unknown, false));
}

/**
 * @brief VRAM capture is selected only when the compositor offers DMA-BUF
 *        formats the encoder path can import.
 */
TEST(PipeWireUtilsTests, VramCaptureSelected) {
  using enum platf::mem_type_e;

  // DMA-BUF availability is required for every path.
  EXPECT_FALSE(pipewire::vram_capture_selected(vaapi, false, false));
  EXPECT_FALSE(pipewire::vram_capture_selected(vulkan, false, false));
  EXPECT_FALSE(pipewire::vram_capture_selected(cuda, false, true));

  EXPECT_TRUE(pipewire::vram_capture_selected(vaapi, true, false));
  EXPECT_TRUE(pipewire::vram_capture_selected(vulkan, true, false));
  EXPECT_TRUE(pipewire::vram_capture_selected(cuda, true, true));

  // A hybrid CUDA system cannot import DMA-BUFs from the display GPU.
  EXPECT_FALSE(pipewire::vram_capture_selected(cuda, true, false));
  EXPECT_FALSE(pipewire::vram_capture_selected(system, true, false));
}

/**
 * @brief The SPA chunk offset selects the valid data region and the region
 *        must fit inside the mapped buffer.
 */
TEST(PipeWireUtilsTests, ResolveMemptrRegion) {
  const std::array<std::uint8_t, 32> data {};

  // Zero offset starts at the mapping base.
  auto region = pipewire::resolve_memptr_region(data.data(), data.size(), 0, 16);
  ASSERT_TRUE(region.has_value());
  EXPECT_EQ(region->data, data.data());
  EXPECT_EQ(region->size, 16u);

  // A non-zero offset skips the bytes before the valid region.
  region = pipewire::resolve_memptr_region(data.data(), data.size(), 8, 16);
  ASSERT_TRUE(region.has_value());
  EXPECT_EQ(region->data, data.data() + 8);
  EXPECT_EQ(region->size, 16u);

  // The offset is taken modulo the mapping size.
  region = pipewire::resolve_memptr_region(data.data(), data.size(), 40, 8);
  ASSERT_TRUE(region.has_value());
  EXPECT_EQ(region->data, data.data() + 8);
  EXPECT_EQ(region->size, 8u);

  // A region ending exactly at the mapping end is valid.
  region = pipewire::resolve_memptr_region(data.data(), data.size(), 16, 16);
  ASSERT_TRUE(region.has_value());
  EXPECT_EQ(region->data, data.data() + 16);
  EXPECT_EQ(region->size, 16u);

  // A region that runs past the mapping is rejected.
  EXPECT_FALSE(pipewire::resolve_memptr_region(data.data(), data.size(), 24, 16).has_value());
  EXPECT_FALSE(pipewire::resolve_memptr_region(nullptr, data.size(), 0, 16).has_value());

  // Without a known mapping size the offset cannot be validated.
  region = pipewire::resolve_memptr_region(data.data(), 0, 0, 16);
  ASSERT_TRUE(region.has_value());
  EXPECT_EQ(region->data, data.data());
  EXPECT_EQ(region->size, 16u);
  EXPECT_FALSE(pipewire::resolve_memptr_region(data.data(), 0, 4, 16).has_value());

  // A chunk the producer flagged as corrupted is rejected instead of decoded.
  EXPECT_FALSE(pipewire::resolve_memptr_region(data.data(), data.size(), 0, 16, SPA_CHUNK_FLAG_CORRUPTED).has_value());
}

/**
 * @brief A contiguous NV12 block is only trusted when its size matches the
 *        luma and chroma planes exactly.
 */
TEST(PipeWireUtilsTests, ResolveContiguousNv12Chroma) {
  constexpr int height = 4;
  constexpr std::size_t luma_stride = 8;
  constexpr std::size_t chroma_stride = pipewire::contiguous_chroma_stride(luma_stride);
  constexpr std::size_t chroma_size = chroma_stride * ((height + 1) / 2);

  // Tight layout without a reported stride.
  auto resolved = pipewire::resolve_contiguous_nv12_chroma(luma_stride * height + chroma_size, luma_stride, height);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->stride, chroma_stride);
  EXPECT_EQ(resolved->size, chroma_size);

  // Padded luma stride with the matching chroma plane.
  constexpr std::size_t padded_stride = luma_stride + 32;
  constexpr std::size_t padded_chroma_size = pipewire::contiguous_chroma_stride(padded_stride) * ((height + 1) / 2);
  resolved = pipewire::resolve_contiguous_nv12_chroma(padded_stride * height + padded_chroma_size, padded_stride, height);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->stride, pipewire::contiguous_chroma_stride(padded_stride));
  EXPECT_EQ(resolved->size, padded_chroma_size);

  // An aligned height exposes padding between the planes and is rejected.
  EXPECT_FALSE(pipewire::resolve_contiguous_nv12_chroma(luma_stride * 8 + chroma_size, luma_stride, height).has_value());

  // Extra trailing bytes are ambiguous and are rejected.
  EXPECT_FALSE(pipewire::resolve_contiguous_nv12_chroma(luma_stride * height + chroma_size + 4, luma_stride, height).has_value());

  // A luma-only block and degenerate heights are rejected.
  EXPECT_FALSE(pipewire::resolve_contiguous_nv12_chroma(luma_stride * height, luma_stride, height).has_value());
  EXPECT_FALSE(pipewire::resolve_contiguous_nv12_chroma(luma_stride * height + chroma_size, luma_stride, 0).has_value());
}

/**
 * @brief Packed 32-bit RGB layouts need a full row per stride and enough
 *        valid bytes for the final row.
 */
TEST(PipeWireUtilsTests, PackedMemptrLayoutSupported) {
  constexpr int width = 8;
  constexpr int height = 4;
  constexpr std::size_t row_bytes = static_cast<std::size_t>(width) * 4;

  // Exactly the frame, no padding.
  EXPECT_TRUE(pipewire::packed_memptr_layout_supported(width, height, row_bytes, row_bytes * height));

  // Padded stride with the final row fully present.
  EXPECT_TRUE(pipewire::packed_memptr_layout_supported(width, height, row_bytes + 32, (row_bytes + 32) * height));

  // A stride shorter than one row would overlap rows.
  EXPECT_FALSE(pipewire::packed_memptr_layout_supported(width, height, row_bytes - 1, row_bytes * height));
  EXPECT_FALSE(pipewire::packed_memptr_layout_supported(width, height, 0, row_bytes * height));

  // A stride that is not a whole number of pixels cannot be published.
  EXPECT_FALSE(pipewire::packed_memptr_layout_supported(width, height, row_bytes + 2, (row_bytes + 2) * height));

  // The final row must be fully contained in the valid region.
  EXPECT_FALSE(pipewire::packed_memptr_layout_supported(width, height, row_bytes, row_bytes * height - 1));

  // Invalid dimensions are rejected.
  EXPECT_FALSE(pipewire::packed_memptr_layout_supported(0, height, row_bytes, row_bytes * height));
  EXPECT_FALSE(pipewire::packed_memptr_layout_supported(width, 0, row_bytes, row_bytes * height));
}

/**
 * @brief Memory-buffer formats are only advertised when the selected encoder
 *        path can convert them: the software path handles every declared
 *        format, while hardware RAM converters accept packed 8-bit BGR only.
 */
TEST(PipeWireUtilsTests, MemptrFormatSupported) {
  using enum platf::mem_type_e;

  // Software conversion paths handle every declared capture format.
  EXPECT_TRUE(pipewire::memptr_format_supported(platf::pix_fmt_e::nv12, system, false));
  EXPECT_TRUE(pipewire::memptr_format_supported(platf::pix_fmt_e::xbgr2101010, system, false));
  EXPECT_TRUE(pipewire::memptr_format_supported(platf::pix_fmt_e::xbgr2101010, vulkan, false));
  EXPECT_TRUE(pipewire::memptr_format_supported(platf::pix_fmt_e::bgr0, system, false));

  // Hardware RAM paths only accept packed 8-bit BGR.
  EXPECT_TRUE(pipewire::memptr_format_supported(platf::pix_fmt_e::bgr0, cuda, false));
  EXPECT_TRUE(pipewire::memptr_format_supported(platf::pix_fmt_e::bgra, vaapi, false));
  EXPECT_FALSE(pipewire::memptr_format_supported(platf::pix_fmt_e::nv12, cuda, false));
  EXPECT_FALSE(pipewire::memptr_format_supported(platf::pix_fmt_e::xbgr2101010, cuda, false));
  EXPECT_FALSE(pipewire::memptr_format_supported(platf::pix_fmt_e::bgra1010102, vaapi, false));
  EXPECT_FALSE(pipewire::memptr_format_supported(platf::pix_fmt_e::nv12, vaapi, false));
  EXPECT_FALSE(pipewire::memptr_format_supported(platf::pix_fmt_e::rgba, cuda, false));

  // Hardware VRAM paths (DMA-BUF capable) also fall back to 8-bit BGR only.
  EXPECT_TRUE(pipewire::memptr_format_supported(platf::pix_fmt_e::bgr0, vaapi, true));
  EXPECT_FALSE(pipewire::memptr_format_supported(platf::pix_fmt_e::rgba1010102, vulkan, true));
  EXPECT_FALSE(pipewire::memptr_format_supported(platf::pix_fmt_e::nv12, cuda, true));

  // Unmapped formats are only usable by the software heuristic fallback.
  EXPECT_TRUE(pipewire::memptr_format_supported(platf::pix_fmt_e::unknown, system, false));
  EXPECT_FALSE(pipewire::memptr_format_supported(platf::pix_fmt_e::unknown, cuda, false));
}

/**
 * @brief NV12 with separate luma and chroma planes and padded strides is
 *        repacked into a contiguous buffer the converter can consume.
 */
TEST(PipeWireUtilsTests, PackNv12SeparatePlanes) {
  constexpr int width = 4;
  constexpr int height = 4;
  constexpr std::size_t luma_stride = 8;
  constexpr std::size_t chroma_stride = 8;

  std::vector<std::uint8_t> luma(luma_stride * height, 0xEE);
  for (int row = 0; row < height; ++row) {
    for (int col = 0; col < width; ++col) {
      luma[row * luma_stride + col] = static_cast<std::uint8_t>(row + 1);
    }
  }

  std::vector<std::uint8_t> chroma(chroma_stride * (height / 2), 0xEE);
  for (int row = 0; row < height / 2; ++row) {
    for (int col = 0; col < width; ++col) {
      chroma[row * chroma_stride + col] = static_cast<std::uint8_t>(0x80 + row);
    }
  }

  // Pre-fill with a sentinel to prove every byte of the packed frame is written.
  std::vector<std::uint8_t> buffer(64, 0xAB);

  const auto row_pitch = pipewire::pack_nv12_memptr_frame(
    buffer,
    width,
    height,
    {luma.data(), luma_stride, luma.size()},
    {chroma.data(), chroma_stride, chroma.size()}
  );

  const std::size_t expected_size = static_cast<std::size_t>(width) * height * 3 / 2;
  ASSERT_TRUE(row_pitch.has_value());
  ASSERT_EQ(*row_pitch, static_cast<std::size_t>(width));
  EXPECT_GE(buffer.size(), expected_size);

  for (int row = 0; row < height; ++row) {
    for (int col = 0; col < width; ++col) {
      ASSERT_EQ(buffer[static_cast<std::size_t>(row) * width + col], row + 1) << "luma row " << row << " col " << col;
    }
  }

  const std::size_t chroma_offset = static_cast<std::size_t>(width) * height;
  for (int row = 0; row < height / 2; ++row) {
    for (int col = 0; col < width; ++col) {
      ASSERT_EQ(buffer[chroma_offset + static_cast<std::size_t>(row) * width + col], 0x80 + row) << "chroma row " << row << " col " << col;
    }
  }
}

/**
 * @brief A single contiguous NV12 block is compacted the same way as separate
 *        planes, and a buffer that is already large enough is reused.
 */
TEST(PipeWireUtilsTests, PackNv12ContiguousBlock) {
  constexpr int width = 4;
  constexpr int height = 4;

  std::vector<std::uint8_t> source(static_cast<std::size_t>(width) * height * 3 / 2);
  for (std::size_t i = 0; i < source.size(); ++i) {
    source[i] = static_cast<std::uint8_t>(i);
  }

  // A destination buffer that is too small is grown to the packed frame size.
  std::vector<std::uint8_t> small_buffer(8, 0xAB);

  const auto tight_luma = pipewire::plane_view_t {source.data(), width, static_cast<std::size_t>(width) * height};
  const auto tight_chroma = pipewire::plane_view_t {source.data() + static_cast<std::size_t>(width) * height, width, source.size() - static_cast<std::size_t>(width) * height};

  auto row_pitch = pipewire::pack_nv12_memptr_frame(small_buffer, width, height, tight_luma, tight_chroma);
  ASSERT_TRUE(row_pitch.has_value());
  ASSERT_EQ(*row_pitch, static_cast<std::size_t>(width));
  ASSERT_EQ(small_buffer.size(), source.size());
  EXPECT_EQ(small_buffer, source);

  // A buffer that is already large enough is reused without reallocation.
  std::vector<std::uint8_t> buffer(source.size(), 0xAB);
  const auto *const data_before = buffer.data();
  const auto capacity_before = buffer.capacity();

  row_pitch = pipewire::pack_nv12_memptr_frame(buffer, width, height, tight_luma, tight_chroma);
  ASSERT_TRUE(row_pitch.has_value());
  ASSERT_EQ(*row_pitch, static_cast<std::size_t>(width));
  EXPECT_EQ(buffer.data(), data_before);
  EXPECT_EQ(buffer.capacity(), capacity_before);
  EXPECT_EQ(buffer, source);
}

/**
 * @brief Odd NV12 dimensions are valid: the chroma plane uses ceil() rows and
 *        columns, and its padding byte is preserved in the packed layout.
 */
TEST(PipeWireUtilsTests, PackNv12OddDimensions) {
  constexpr int width = 5;
  constexpr int height = 3;
  constexpr std::size_t dst_pitch = 6;
  constexpr std::size_t chroma_rows = 2;

  std::vector<std::uint8_t> luma(static_cast<std::size_t>(width) * height);
  for (std::size_t i = 0; i < luma.size(); ++i) {
    luma[i] = static_cast<std::uint8_t>(0x10 + i);
  }

  std::vector<std::uint8_t> chroma(dst_pitch * chroma_rows);
  for (std::size_t i = 0; i < chroma.size(); ++i) {
    chroma[i] = static_cast<std::uint8_t>(0x80 + i);
  }

  std::vector<std::uint8_t> buffer;

  const pipewire::plane_view_t luma_view {luma.data(), width, luma.size()};
  const pipewire::plane_view_t chroma_view {chroma.data(), dst_pitch, chroma.size()};

  const auto row_pitch = pipewire::pack_nv12_memptr_frame(buffer, width, height, luma_view, chroma_view);
  ASSERT_TRUE(row_pitch.has_value());
  ASSERT_EQ(*row_pitch, dst_pitch);
  ASSERT_EQ(buffer.size(), dst_pitch * height + dst_pitch * chroma_rows);

  for (std::size_t row = 0; row < height; ++row) {
    for (std::size_t col = 0; col < width; ++col) {
      ASSERT_EQ(buffer[row * dst_pitch + col], 0x10 + row * width + col) << "luma row " << row << " col " << col;
    }
  }

  const std::size_t chroma_offset = dst_pitch * height;
  for (std::size_t i = 0; i < chroma.size(); ++i) {
    ASSERT_EQ(buffer[chroma_offset + i], 0x80 + i);
  }
}

/**
 * @brief A reported row stride is used as-is; a missing one falls back to the
 *        caller's default and a negative (bottom-up) one is rejected.
 */
TEST(PipeWireUtilsTests, ResolveRowStride) {
  EXPECT_EQ(pipewire::resolve_row_stride(64, 32).value_or(0), 64u);
  EXPECT_EQ(pipewire::resolve_row_stride(0, 32).value_or(0), 32u);
  EXPECT_FALSE(pipewire::resolve_row_stride(-64, 32).has_value());
}

/**
 * @brief Inside a contiguous NV12 block the chroma rows always use an even
 *        number of bytes, so an odd luma stride gains a padding byte.
 */
TEST(PipeWireUtilsTests, ContiguousChromaStride) {
  EXPECT_EQ(pipewire::contiguous_chroma_stride(4), 4u);
  EXPECT_EQ(pipewire::contiguous_chroma_stride(8), 8u);
  EXPECT_EQ(pipewire::contiguous_chroma_stride(10), 10u);

  // Odd strides, including a tight odd width, are even-padded.
  EXPECT_EQ(pipewire::contiguous_chroma_stride(5), 6u);
  EXPECT_EQ(pipewire::contiguous_chroma_stride(7), 8u);
}

/**
 * @brief An odd-width contiguous NV12 block without a reported stride is
 *        packed using the even-padded chroma row length.
 */
TEST(PipeWireUtilsTests, PackNv12OddContiguousWithoutStride) {
  constexpr int width = 5;
  constexpr int height = 3;
  constexpr std::size_t luma_stride = width;  // fallback used when no stride is reported
  constexpr std::size_t chroma_stride = pipewire::contiguous_chroma_stride(luma_stride);
  static_assert(chroma_stride == static_cast<std::size_t>(width) + 1);

  const std::size_t luma_size = luma_stride * height;
  const std::size_t chroma_size = chroma_stride * ((height + 1) / 2);

  std::vector<std::uint8_t> source(luma_size + chroma_size);
  for (std::size_t i = 0; i < source.size(); ++i) {
    source[i] = static_cast<std::uint8_t>(i);
  }

  std::vector<std::uint8_t> buffer;

  const pipewire::plane_view_t luma_view {source.data(), luma_stride, luma_size};
  const pipewire::plane_view_t chroma_view {source.data() + luma_size, chroma_stride, chroma_size};

  const auto row_pitch = pipewire::pack_nv12_memptr_frame(buffer, width, height, luma_view, chroma_view);
  ASSERT_TRUE(row_pitch.has_value());
  ASSERT_EQ(*row_pitch, chroma_stride);
  ASSERT_EQ(buffer.size(), (chroma_stride * height) + (chroma_stride * ((height + 1) / 2)));

  for (std::size_t row = 0; row < static_cast<std::size_t>(height); ++row) {
    for (int col = 0; col < width; ++col) {
      ASSERT_EQ(buffer[row * chroma_stride + col], source[row * luma_stride + col]) << "luma row " << row << " col " << col;
    }
  }

  for (std::size_t row = 0; row < (static_cast<std::size_t>(height) + 1) / 2; ++row) {
    for (std::size_t col = 0; col < chroma_stride; ++col) {
      ASSERT_EQ(buffer[chroma_stride * height + row * chroma_stride + col], source[luma_size + row * chroma_stride + col]) << "chroma row " << row << " col " << col;
    }
  }
}

/**
 * @brief Invalid NV12 layouts are rejected without touching the destination
 *        buffer, so the capture never publishes a partially packed frame.
 */
TEST(PipeWireUtilsTests, PackNv12RejectsInvalidLayout) {
  constexpr int width = 4;
  constexpr int height = 4;
  // Sized to the largest plane the invalid-layout cases simulate, so the test
  // stays safe even if the validation order changes.
  const std::array<std::uint8_t, 64> planes {};

  std::vector<std::uint8_t> buffer(24, 0xAB);
  const pipewire::plane_view_t valid_plane {planes.data(), width, 16};
  const pipewire::plane_view_t short_plane {planes.data(), width, 8};
  const pipewire::plane_view_t short_stride {planes.data(), 2, 16};
  const pipewire::plane_view_t short_chroma {planes.data(), width, 5};
  const pipewire::plane_view_t null_data {nullptr, width, 16};

  // Invalid dimensions.
  EXPECT_FALSE(pipewire::pack_nv12_memptr_frame(buffer, 0, height, valid_plane, short_plane).has_value());
  EXPECT_FALSE(pipewire::pack_nv12_memptr_frame(buffer, width, 0, valid_plane, short_plane).has_value());

  // Null plane pointers.
  EXPECT_FALSE(pipewire::pack_nv12_memptr_frame(buffer, width, height, null_data, short_plane).has_value());
  EXPECT_FALSE(pipewire::pack_nv12_memptr_frame(buffer, width, height, valid_plane, null_data).has_value());

  // Strides smaller than one row.
  EXPECT_FALSE(pipewire::pack_nv12_memptr_frame(buffer, width, height, short_stride, short_plane).has_value());
  EXPECT_FALSE(pipewire::pack_nv12_memptr_frame(buffer, width, height, valid_plane, short_stride).has_value());

  // Truncated planes: the last row must be fully present.
  EXPECT_FALSE(pipewire::pack_nv12_memptr_frame(buffer, width, height, {planes.data(), width, 13}, short_plane).has_value());
  EXPECT_FALSE(pipewire::pack_nv12_memptr_frame(buffer, width, height, valid_plane, short_chroma).has_value());

  // Nothing may have been written to the destination.
  EXPECT_EQ(buffer, std::vector<std::uint8_t>(24, 0xAB));
}

/**
 * @brief A spent reinit budget aborts the next abandonment; a valid frame
 *        refreshes it and an abort starts a fresh budget.
 */
TEST(PipeWireUtilsTests, AbandonedStreamBudget) {
  pipewire::abandoned_stream_budget_t budget;

  // The first abandonment may reinitialize capture.
  EXPECT_TRUE(budget.consume_reinit());

  // The next abandonment aborts the session instead.
  EXPECT_FALSE(budget.consume_reinit());

  // A valid frame proves the stream recovered and refreshes the budget.
  budget.on_recovered();
  EXPECT_TRUE(budget.consume_reinit());

  // The second abandonment aborts and starts a fresh budget.
  EXPECT_FALSE(budget.consume_reinit());
  budget.on_aborted();
  EXPECT_TRUE(budget.consume_reinit());
}

/**
 * @brief A drop burst is abandoned by elapsed time instead of a frame count,
 *        and a recovered stream starts a fresh burst.
 */
TEST(PipeWireUtilsTests, DropBurstPolicy) {
  pipewire::drop_burst_t burst;
  const auto start = std::chrono::steady_clock::time_point {std::chrono::seconds(10)};

  auto update = burst.on_drop(start);
  EXPECT_TRUE(update.log);
  EXPECT_FALSE(update.abandoned);

  // A long run of drops within the abandon duration keeps the stream alive,
  // so the threshold does not depend on the compositor refresh rate.
  for (int i = 0; i < 500; i++) {
    update = burst.on_drop(start + std::chrono::milliseconds(i));
    EXPECT_FALSE(update.abandoned);
  }

  // A burst that lasts at least the abandon duration ends the stream.
  update = burst.on_drop(start + std::chrono::seconds(2));
  EXPECT_TRUE(update.abandoned);

  // A usable frame starts a fresh burst.
  burst.on_recovered();
  update = burst.on_drop(start + std::chrono::seconds(3));
  EXPECT_FALSE(update.abandoned);
}

  #if defined(SUNSHINE_BUILD_KWIN) || defined(SUNSHINE_BUILD_PORTAL)
/**
 * @brief Every advertised DRM fourcc maps, through the SPA table, to the
 *        expected pixel format. The expected pairs are listed independently,
 *        so this guards the corrected 1:1 HDR mapping instead of repeating it.
 */
TEST(PipeWireUtilsTests, SpaFormatMapIsConsistent) {
  constexpr std::array<std::pair<std::uint64_t, platf::pix_fmt_e>, 8> expected = {{
    {DRM_FORMAT_NV12, platf::pix_fmt_e::nv12},
    {DRM_FORMAT_XBGR2101010, platf::pix_fmt_e::xbgr2101010},
    {DRM_FORMAT_BGRA1010102, platf::pix_fmt_e::bgra1010102},
    {DRM_FORMAT_RGBA1010102, platf::pix_fmt_e::rgba1010102},
    {DRM_FORMAT_ABGR2101010, platf::pix_fmt_e::abgr2101010},
    {DRM_FORMAT_ARGB2101010, platf::pix_fmt_e::argb2101010},
    {DRM_FORMAT_ARGB8888, platf::pix_fmt_e::bgra},
    {DRM_FORMAT_XRGB8888, platf::pix_fmt_e::bgr0},
  }};

  ASSERT_EQ(pipewire::format_map.size(), expected.size());
  for (const auto &[fourcc, pix_fmt] : expected) {
    const auto entry = std::ranges::find(pipewire::format_map, fourcc, &pipewire::format_map_t::fourcc);
    ASSERT_NE(entry, pipewire::format_map.end()) << "missing DRM format " << fourcc;
    EXPECT_EQ(pipewire::map_spa_pix_fmt(entry->pw_format), pix_fmt) << "DRM format " << fourcc;
  }
}
  #else
/**
 * @brief The SPA format mapping only exists when PipeWire capture is built.
 */
TEST(PipeWireUtilsTests, SpaFormatMapUnavailable) {
  GTEST_SKIP() << "PipeWire capture is not built in this configuration";
}
  #endif

#endif
