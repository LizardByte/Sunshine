/**
 * @file src/platform/linux/pipewire_utils.h
 * @brief Helpers for PipeWire capture format negotiation and memory-buffer packing.
 */
#pragma once

// standard includes
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

// lib includes
#if defined(SUNSHINE_BUILD_KWIN) || defined(SUNSHINE_BUILD_PORTAL)
  #include <libdrm/drm_fourcc.h>
  #include <spa/buffer/buffer.h>
  #include <spa/param/video/raw.h>
#endif

// The SPA buffer API fixes these flag values. Older headers may not expose
// every flag and builds without the SPA headers do not have them at all, so
// define local fallbacks instead of silently disabling the checks.
#ifndef SPA_CHUNK_FLAG_CORRUPTED
  #define SPA_CHUNK_FLAG_CORRUPTED (1u << 0)  ///< Chunk data is corrupted in some way.
#endif
#ifndef SPA_CHUNK_FLAG_EMPTY
  #define SPA_CHUNK_FLAG_EMPTY (1u << 1)  ///< Chunk data is empty with media specific meaning.
#endif

// local includes
#include "src/platform/common.h"

namespace pipewire {
  /**
   * @brief Drop accounting for capture frames that could not be used.
   *
   * A burst is a run of consecutive unusable frames. The first drop and every
   * logging interval after that are reported, and a burst that lasts longer
   * than the abandon duration ends the stream. The abandon threshold is
   * expressed in time so it does not depend on the compositor refresh rate.
   */
  class drop_burst_t {
  public:
    /**
     * @brief Result of accounting one dropped frame.
     */
    struct update_t {
      bool log {};  ///< Whether the drop should be logged.
      bool abandoned {};  ///< Whether the burst lasted longer than the abandon duration.
    };

    /**
     * @brief Account one unusable frame.
     *
     * @param now Current steady clock time.
     * @return Logging and abandonment decision for this drop.
     */
    [[nodiscard]] update_t on_drop(std::chrono::steady_clock::time_point now) {
      const auto count = drops_.fetch_add(1) + 1;
      auto start = start_ns_.load();
      if (count == 1 || start == 0) {
        start = now.time_since_epoch().count();
        start_ns_.store(start);
      }

      const auto elapsed = std::chrono::steady_clock::duration {now.time_since_epoch().count() - start};
      return {count == 1 || count % LOG_INTERVAL == 0, elapsed >= ABANDON_AFTER};
    }

    /**
     * @brief Start a fresh burst after a usable frame.
     */
    void on_recovered() {
      drops_.store(0);
      start_ns_.store(0);
    }

  private:
    static constexpr int LOG_INTERVAL = 60;  ///< Drops between repeated log messages.
    static constexpr auto ABANDON_AFTER = std::chrono::seconds(2);  ///< Burst duration after which the stream is abandoned.
    std::atomic<int> drops_ {0};  ///< Drops in the current burst.
    std::atomic<std::int64_t> start_ns_ {0};  ///< Steady clock start of the current burst, or zero.
  };

  /**
   * @brief One plane of a capture image: address, row stride and valid size.
   */
  struct plane_view_t {
    const std::uint8_t *data {};  ///< First byte of the plane.
    std::size_t stride {};  ///< Bytes between consecutive rows of the plane.
    std::size_t size {};  ///< Valid bytes in the plane.
  };

  /**
   * @brief Valid region inside a mapped PipeWire buffer.
   */
  struct valid_region_t {
    const std::uint8_t *data {};  ///< First valid byte.
    std::size_t size {};  ///< Number of valid bytes.
  };

  /**
   * @brief Chroma plane of a single contiguous NV12 block.
   */
  struct nv12_chroma_t {
    std::size_t stride {};  ///< Chroma row stride in bytes.
    std::size_t size {};  ///< Chroma plane size in bytes.
  };

  /**
   * @brief Budget of one capture reinit after repeated invalid frames.
   *
   * A valid frame refreshes the budget; the second abandonment aborts the
   * session. The holder must outlive the display object, which the reinit
   * cycle destroys and recreates, so a session that ends with the budget
   * spent leaks it to the next session. Scoping the budget per session would
   * require the reinit cycle to carry a session token.
   */
  class abandoned_stream_budget_t {
  public:
    /**
     * @brief Consume the budget for one capture reinit.
     *
     * @return True only for the first abandonment since the last recovery or abort.
     */
    [[nodiscard]] bool consume_reinit() {
      return !retry_used_.exchange(true);
    }

    /**
     * @brief Refresh the budget after a valid frame proves the stream recovered.
     */
    void on_recovered() {
      reset();
    }

    /**
     * @brief Reset the budget when the second abandonment aborts the session.
     */
    void on_aborted() {
      reset();
    }

  private:
    /**
     * @brief Start a fresh reinit budget.
     */
    void reset() {
      retry_used_.store(false);
    }

    std::atomic<bool> retry_used_ {false};  ///< Whether a reinit was already spent.
  };

  /**
   * @brief Check whether the selected encoder path keeps captured frames in VRAM.
   *
   * DMA-BUF captures are only usable when the compositor offers them and the
   * encoder path can import them: VAAPI always imports, Vulkan falls back to
   * the software conversion path without DMA-BUF, and CUDA only imports when
   * the display GPU is the NVIDIA device driving the encoder.
   *
   * @note Must match the device selection in
   * pipewire_display_t::make_avcodec_encode_device().
   *
   * @param mem_type Encoder memory type selected for the stream.
   * @param has_dmabuf_capture True when DMA-BUF capture formats are available.
   * @param display_is_nvidia True when the display GPU is NVIDIA.
   * @return True when captures are consumed as VRAM frames.
   */
  inline bool vram_capture_selected(platf::mem_type_e mem_type, bool has_dmabuf_capture, bool display_is_nvidia) {
    using enum platf::mem_type_e;

    return has_dmabuf_capture && (mem_type == vaapi || mem_type == vulkan || (mem_type == cuda && display_is_nvidia));
  }

  /**
   * @brief Check whether the selected encoder path converts captures in software.
   *
   * The generic software conversion path understands every declared capture
   * format. Hardware RAM converters (CUDA/VAAPI) only upload packed 8-bit BGR
   * frames, and hardware VRAM converters import RGB DMA-BUF surfaces.
   *
   * @note Must match the device selection in
   * pipewire_display_t::make_avcodec_encode_device(): Vulkan only gets a VRAM
   * device when DMA-BUF capture formats are available.
   *
   * @param mem_type Encoder memory type selected for the stream.
   * @param has_dmabuf_capture True when DMA-BUF capture formats are available.
   * @return True when the software conversion path is selected.
   */
  inline bool software_conversion_selected(platf::mem_type_e mem_type, bool has_dmabuf_capture) {
    using enum platf::mem_type_e;

    switch (mem_type) {
      // Software encoders convert NV12 through the software scaler.
      case system:
        return true;
      // Vulkan falls back to the software conversion path when DMA-BUF is unavailable.
      case vulkan:
        return !vram_capture_selected(vulkan, has_dmabuf_capture, false);
      default:
        return false;
    }
  }

  /**
   * @brief Check whether a memory-buffer capture format can be consumed by the selected encoder path.
   *
   * The software conversion path handles every declared capture format.
   * Hardware RAM converters only accept packed 8-bit BGR frames, so NV12 and
   * the packed 10-bit formats must not be advertised to them.
   *
   * @param pix_fmt Declared capture pixel format.
   * @param mem_type Encoder memory type selected for the stream.
   * @param has_dmabuf_capture True when DMA-BUF capture formats are available.
   * @return True when the memory-buffer format may be advertised.
   */
  inline bool memptr_format_supported(platf::pix_fmt_e pix_fmt, platf::mem_type_e mem_type, bool has_dmabuf_capture) {
    if (software_conversion_selected(mem_type, has_dmabuf_capture)) {
      return true;
    }

    // No software conversion: only explicitly declared packed 8-bit BGR is usable.
    return platf::is_bgr_capture_format(pix_fmt) && pix_fmt != platf::pix_fmt_e::unknown;
  }

  /**
   * @brief Resolve the valid data region of a PipeWire memory buffer.
   *
   * The SPA chunk offset is taken modulo the mapped size and the valid size
   * must fit in the remainder, as required by the spa_chunk documentation.
   * Wrapped regions that pass the end of the mapping are rejected instead of
   * being copied in two segments, so a chunk that relies on wrap-around is
   * dropped rather than misread.
   *
   * @param data Start of the mapped buffer as bytes.
   * @param maxsize Size of the mapping in bytes; zero when unknown.
   * @param offset Chunk offset of the valid data.
   * @param size Chunk size of the valid data.
   * @param flags SPA chunk flags for the region.
   * @return Valid region, or empty when it is unusable.
   */
  [[nodiscard]] inline std::optional<valid_region_t> resolve_memptr_region(const std::uint8_t *data, std::size_t maxsize, std::size_t offset, std::size_t size, std::uint32_t flags = 0) {
    if ((flags & SPA_CHUNK_FLAG_CORRUPTED) != 0) {
      return std::nullopt;
    }

    if (!data) {
      return std::nullopt;
    }

    if (maxsize > 0) {
      const auto start = offset % maxsize;
      if (size > maxsize - start) {
        return std::nullopt;
      }

      return valid_region_t {data + start, size};
    }

    if (offset == 0) {
      // Unknown mapping size with no offset: keep the historical behavior of
      // trusting the chunk size.
      return valid_region_t {data, size};
    }

    // Unknown mapping size: an offset cannot be validated, so fail closed
    // rather than return a pointer that may lie outside the mapping.
    return std::nullopt;
  }

  /**
   * @brief Check that a packed capture region holds a complete 32-bit RGB frame.
   *
   * A packed frame requires at least one full row (width * 4 bytes) per
   * stride, a whole number of pixels per row, and enough valid bytes for the
   * final row. A shorter or sub-pixel stride would overlap rows even when the
   * size check passes.
   *
   * @param width Frame width in pixels.
   * @param height Frame height in pixels.
   * @param stride Row stride in bytes.
   * @param size Valid size of the data region in bytes.
   * @return True when the packed layout is usable.
   */
  inline bool packed_memptr_layout_supported(int width, int height, std::size_t stride, std::size_t size) {
    if (width <= 0 || height <= 0) {
      return false;
    }

    const auto row_bytes = static_cast<std::size_t>(width) * 4;
    if (stride < row_bytes || stride % 4 != 0) {
      return false;
    }

    return size >= (static_cast<std::size_t>(height) - 1) * stride + row_bytes;
  }

  /**
   * @brief Resolve the row stride reported for a memory buffer plane.
   *
   * A negative stride describes a bottom-up image, which this capture path
   * cannot convert, so it is rejected instead of being treated as a packed
   * layout. A zero stride means the producer did not report one; the caller's
   * fallback is used in that case.
   *
   * @param chunk_stride Stride reported by the SPA chunk.
   * @param fallback Stride to use when none was reported.
   * @return Row stride in bytes, or empty when the stride is negative.
   */
  inline std::optional<std::size_t> resolve_row_stride(std::int32_t chunk_stride, std::size_t fallback) {
    if (chunk_stride < 0) {
      return std::nullopt;
    }

    return chunk_stride > 0 ? static_cast<std::size_t>(chunk_stride) : fallback;
  }

  /**
   * @brief Resolve the chroma row stride inside a single contiguous NV12 block.
   *
   * In a contiguous block the chroma plane directly follows the luma plane.
   * Chroma rows hold an even number of bytes (sample pairs), so an odd luma
   * stride, including a tight odd width, is rounded up by one byte.
   *
   * @param luma_stride Luma row stride in bytes.
   * @return Chroma row stride in bytes.
   */
  constexpr std::size_t contiguous_chroma_stride(std::size_t luma_stride) {
    return luma_stride + (luma_stride & 1);
  }

  /**
   * @brief Resolve the chroma plane of a single contiguous NV12 block.
   *
   * The block is only trusted when the valid size matches the luma plane plus
   * the chroma plane exactly, at the assumed strides. Producers that align the
   * luma plane height (for example 1080 to 1088) would otherwise expose
   * padding as chroma, and padded luma rows without a reported stride cannot
   * be distinguished from a tight layout.
   *
   * @param luma_size Valid size of the contiguous block in bytes.
   * @param luma_stride Luma row stride in bytes.
   * @param height Frame height in pixels.
   * @return Chroma plane, or empty when the block does not match the contiguous NV12 layout.
   */
  [[nodiscard]] inline std::optional<nv12_chroma_t> resolve_contiguous_nv12_chroma(std::size_t luma_size, std::size_t luma_stride, int height) {
    if (height <= 0) {
      return std::nullopt;
    }

    const auto luma_plane_size = luma_stride * static_cast<std::size_t>(height);
    if (luma_size < luma_plane_size) {
      return std::nullopt;
    }

    const auto rows = (static_cast<std::size_t>(height) + 1) / 2;
    const auto stride = contiguous_chroma_stride(luma_stride);
    const auto plane_size = stride * rows;
    if (luma_size - luma_plane_size != plane_size) {
      return std::nullopt;
    }

    return nv12_chroma_t {stride, plane_size};
  }

  /**
   * @brief Repack a two-plane NV12 memory capture into a contiguous buffer.
   *
   * PipeWire may deliver NV12 as separate luma and chroma `spa_data` blocks
   * with independent strides, or as a single contiguous block. The conversion
   * path expects the chroma plane immediately after the luma plane, addressed
   * as `buffer + row_pitch * height`. Rows are compacted to `width` bytes, so
   * the caller must use the returned row pitch.
   *
   * @param buffer Destination buffer; resized when it is too small.
   * @param width Frame width in pixels.
   * @param height Frame height in pixels.
   * @param luma Source luma plane view.
   * @param chroma Source chroma plane view.
   * @return Destination row pitch, or empty when the layout is unusable.
   */
  [[nodiscard]] inline std::optional<std::size_t> pack_nv12_memptr_frame(std::vector<std::uint8_t> &buffer, int width, int height, const plane_view_t &luma, const plane_view_t &chroma) {
    if (width <= 0 || height <= 0) {
      return std::nullopt;
    }

    if (!luma.data || !chroma.data) {
      return std::nullopt;
    }

    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    // The chroma row holds ceil(width / 2) sample pairs, i.e. an even number
    // of bytes. Odd sizes therefore keep one padding byte per chroma row.
    const auto dst_pitch = w + (w & 1);
    const auto chroma_rows = (h + 1) / 2;

    if (luma.stride < w || chroma.stride < dst_pitch) {
      return std::nullopt;
    }

    // Validate both planes before writing anything so a rejected frame leaves
    // the destination buffer untouched.
    if (luma.size < (h - 1) * luma.stride + w) {
      return std::nullopt;
    }
    if (chroma.size < (chroma_rows - 1) * chroma.stride + dst_pitch) {
      return std::nullopt;
    }

    if (const auto needed = dst_pitch * h + dst_pitch * chroma_rows; buffer.size() < needed) {
      buffer.resize(needed);
    }

    auto *dst_luma = buffer.data();
    if (w == dst_pitch && luma.stride == w) {
      // Source rows are already tightly packed: copy the plane in one call.
      std::memcpy(dst_luma, luma.data, w * h);
    } else {
      for (std::size_t row = 0; row < h; ++row) {
        std::memcpy(dst_luma + row * dst_pitch, luma.data + row * luma.stride, w);
      }
    }

    auto *dst_chroma = dst_luma + dst_pitch * h;
    if (chroma.stride == dst_pitch) {
      std::memcpy(dst_chroma, chroma.data, dst_pitch * chroma_rows);
    } else {
      for (std::size_t row = 0; row < chroma_rows; ++row) {
        std::memcpy(dst_chroma + row * dst_pitch, chroma.data + row * chroma.stride, dst_pitch);
      }
    }

    return dst_pitch;
  }

#if defined(SUNSHINE_BUILD_KWIN) || defined(SUNSHINE_BUILD_PORTAL)
  /**
   * @brief Map a PipeWire SPA video format to Sunshine's pixel format enum.
   *
   * @param spa_format PipeWire SPA video format identifier.
   * @return Sunshine pixel format; unknown for unmapped formats.
   */
  inline platf::pix_fmt_e map_spa_pix_fmt(int32_t spa_format) {
    using enum platf::pix_fmt_e;

    switch (spa_format) {
      case SPA_VIDEO_FORMAT_NV12:
        return nv12;
      case SPA_VIDEO_FORMAT_BGRx:
        return bgr0;
      case SPA_VIDEO_FORMAT_BGRA:
        return bgra;
      case SPA_VIDEO_FORMAT_xBGR_210LE:
        return xbgr2101010;
      case SPA_VIDEO_FORMAT_ARGB_210LE:
        return argb2101010;
      case SPA_VIDEO_FORMAT_ABGR_210LE:
        return abgr2101010;
      case SPA_VIDEO_FORMAT_RGBA_102LE:
        return rgba1010102;
      case SPA_VIDEO_FORMAT_BGRA_102LE:
        return bgra1010102;
      default:
        return unknown;
    }
  }

  /**
   * @brief A DRM fourcc and its matching PipeWire SPA video format.
   */
  struct format_map_t {
    std::uint64_t fourcc;  ///< DRM fourcc pixel format.
    int32_t pw_format;  ///< Matching PipeWire SPA video format.
  };

  /**
   * @brief Capture formats offered to PipeWire, as DRM fourcc / SPA pairs.
   *
   * The order is the negotiation preference: 10-bit formats first so HDR
   * sessions keep their bit depth, then packed 8-bit RGB, and finally the
   * chroma-subsampled NV12 as the most compatible fallback.
   */
  inline constexpr auto format_map = std::to_array(
    {
      format_map_t {DRM_FORMAT_XBGR2101010, SPA_VIDEO_FORMAT_xBGR_210LE},
      format_map_t {DRM_FORMAT_BGRA1010102, SPA_VIDEO_FORMAT_BGRA_102LE},
      format_map_t {DRM_FORMAT_RGBA1010102, SPA_VIDEO_FORMAT_RGBA_102LE},
      format_map_t {DRM_FORMAT_ABGR2101010, SPA_VIDEO_FORMAT_ABGR_210LE},
      format_map_t {DRM_FORMAT_ARGB2101010, SPA_VIDEO_FORMAT_ARGB_210LE},
      format_map_t {DRM_FORMAT_ARGB8888, SPA_VIDEO_FORMAT_BGRA},
      format_map_t {DRM_FORMAT_XRGB8888, SPA_VIDEO_FORMAT_BGRx},
      format_map_t {DRM_FORMAT_NV12, SPA_VIDEO_FORMAT_NV12},
    }
  );

  static_assert(format_map.back().fourcc == DRM_FORMAT_NV12);
#endif
}  // namespace pipewire
