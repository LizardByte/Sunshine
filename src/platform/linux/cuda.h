/**
 * @file src/platform/linux/cuda.h
 * @brief Definitions for CUDA implementation.
 */
#pragma once

// standard includes
#include <cstdint>

#if defined(SUNSHINE_BUILD_CUDA)
  // standard includes
  #include <cmath>
  #include <cstdint>
  #include <memory>
  #include <optional>
  #include <string>
  #include <vector>

  // lib includes
  #include <ffnvcodec/dynlink_cuda.h>

  // local includes
  #include "src/video_colorspace.h"
#endif

namespace cuda::nvfbc {
  /**
   * @brief Decide whether NvFBC should offer the whole virtual desktop as a display.
   *
   * NvFBC enumerates zero outputs when the X screen drives no display head.
   * Capture can still use `NVFBC_TRACKING_SCREEN` for the virtual desktop.
   *
   * @param output_count Number of outputs NvFBC enumerated.
   * @param capture_possible Whether NvFBC reported that capture is possible.
   * @param screen_width Virtual desktop width reported by NvFBC.
   * @param screen_height Virtual desktop height reported by NvFBC.
   * @return `true` when the virtual desktop should be offered as a display.
   */
  constexpr bool should_offer_virtual_desktop(std::uint32_t output_count, bool capture_possible, std::uint32_t screen_width, std::uint32_t screen_height) {
    return output_count == 0 && capture_possible && screen_width > 0 && screen_height > 0;
  }
}  // namespace cuda::nvfbc

#if defined(SUNSHINE_BUILD_CUDA)
namespace platf {
  struct avcodec_encode_device_t;
  struct img_t;
}  // namespace platf

namespace cuda {

  namespace nvfbc {
    std::vector<std::string> display_names();
  }

  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, bool vram);

  /**
   * @brief Create a GL->CUDA encoding device for consuming captured dmabufs.
   * @param in_width Width of captured frames.
   * @param in_height Height of captured frames.
   * @param offset_x Offset of content in captured frame.
   * @param offset_y Offset of content in captured frame.
   * @return FFmpeg encoding device context.
   */
  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_gl_encode_device(int width, int height, int offset_x, int offset_y);

  /**
   * @brief Functions used to query the PCI bus id of a CUDA device.
   */
  struct cuda_pci_accessors_t {
    bool (*is_initialized)();  ///< Report whether the CUDA function table is loaded.
    int (*initialize)();  ///< Load the CUDA function table, returning 0 on success.
    tcuDeviceGet *get_device;  ///< Resolve a CUDA device index, returning CUDA_SUCCESS on success.
    tcuDeviceGetPCIBusId *get_pci_bus_id;  ///< Write the device bus id, returning CUDA_SUCCESS on success.
  };

  /**
   * @brief Query the PCI bus id of a CUDA device.
   * @param index CUDA device index to query.
   * @return Lowercase PCI bus id (e.g. "00000000:0f:00.0"), or no value when
   *         CUDA is unavailable or the query fails.
   */
  std::optional<std::string> device_pci_bus_id(int index);

  /**
   * @brief Query the PCI bus id of a CUDA device with injected driver operations.
   * @param index CUDA device index to query.
   * @param accessors Driver operations used to initialize CUDA and query the device.
   * @return Lowercase PCI bus id (e.g. "00000000:0f:00.0"), or no value when
   *         CUDA is unavailable or the query fails.
   */
  std::optional<std::string> device_pci_bus_id(int index, const cuda_pci_accessors_t &accessors);

  int init();
}  // namespace cuda

/**
 * @brief Opaque CUDA array handle shared by host and CUDA compiler declarations.
 */
using cudaArray_t = struct cudaArray *;

  #if !defined(__CUDACC__)
/**
 * @brief Opaque CUDA stream handle for host compilation.
 */
using cudaStream_t = struct CUstream_st *;
/**
 * @brief CUDA texture object identifier for host compilation.
 */
using cudaTextureObject_t = unsigned long long;
  #else /* defined(__CUDACC__) */
typedef __location__(device_builtin) struct CUstream_st *cudaStream_t;
typedef __location__(device_builtin) unsigned long long cudaTextureObject_t;
  #endif /* !defined(__CUDACC__) */

namespace cuda {

  class freeCudaPtr_t {
  public:
    void operator()(void *ptr);
  };

  class freeCudaStream_t {
  public:
    void operator()(cudaStream_t ptr);
  };

  using ptr_t = std::unique_ptr<void, freeCudaPtr_t>;
  using stream_t = std::unique_ptr<CUstream_st, freeCudaStream_t>;

  stream_t make_stream(int flags = 0);

  struct viewport_t {
    int width;
    int height;
    int offsetX;
    int offsetY;
  };

  /**
   * @brief Fit a captured image into an output frame, keeping its aspect ratio.
   *
   * The offsets are rounded down to even values. NV12 stores one interleaved U,V pair for every
   * two pixels, and the conversion kernel writes each pair at the pixel's x plus `offsetX`, so an
   * odd offset would place every pair one byte off and swap U and V (red and blue).
   *
   * @param in_width Width of the captured image in pixels.
   * @param in_height Height of the captured image in pixels.
   * @param out_width Width of the output frame in pixels.
   * @param out_height Height of the output frame in pixels.
   * @param scale Set to the number of captured pixels per output pixel.
   * @return The area of the output frame that the captured image covers.
   */
  inline viewport_t make_viewport(int in_width, int in_height, int out_width, int out_height, float &scale) {
    // Ensure aspect ratio is maintained
    const auto scalar = std::fminf(out_width / (float) in_width, out_height / (float) in_height);
    const auto out_width_f = in_width * scalar;
    const auto out_height_f = in_height * scalar;

    // result is always positive
    const auto offsetX_f = (out_width - out_width_f) / 2;
    const auto offsetY_f = (out_height - out_height_f) / 2;

    viewport_t viewport;
    viewport.width = out_width_f;
    viewport.height = out_height_f;
    viewport.offsetX = static_cast<int>(offsetX_f) & ~1;
    viewport.offsetY = static_cast<int>(offsetY_f) & ~1;

    scale = 1.0f / scalar;
    return viewport;
  }

  class tex_t {
  public:
    static std::optional<tex_t> make(int height, int pitch);

    tex_t();
    tex_t(tex_t &&) noexcept;

    tex_t &operator=(tex_t &&other) noexcept;

    ~tex_t();

    int copy(std::uint8_t *src, int height, int pitch);

    cudaArray_t array;

    /**
     * @brief CUDA texture objects for point and linear sampling of the same image.
     */
    struct texture_t {
      cudaTextureObject_t point;
      cudaTextureObject_t linear;
    };

    texture_t texture;  ///< Point and linear sampling textures for the CUDA array.
  };

  class sws_t {
  public:
    sws_t() = default;
    sws_t(int in_width, int in_height, int out_width, int out_height, int pitch, int threadsPerBlock, ptr_t &&color_matrix);

    /**
     * in_width, in_height -- The width and height of the captured image in pixels
     * out_width, out_height -- the width and height of the NV12 image in pixels
     *
     * pitch -- The size of a single row of pixels in bytes
     */
    static std::optional<sws_t> make(int in_width, int in_height, int out_width, int out_height, int pitch);

    // Converts loaded image into a CUDevicePtr
    int convert_nv12(std::uint8_t *Y, std::uint8_t *UV, std::uint32_t pitchY, std::uint32_t pitchUV, cudaTextureObject_t texture, stream_t::pointer stream);
    int convert_nv12(std::uint8_t *Y, std::uint8_t *UV, std::uint32_t pitchY, std::uint32_t pitchUV, cudaTextureObject_t texture, stream_t::pointer stream, const viewport_t &viewport);
    int convert_yuv444(std::uint8_t *Y, std::uint8_t *U, std::uint8_t *V, std::uint32_t pitch, cudaTextureObject_t texture, stream_t::pointer stream);
    int convert_yuv444(std::uint8_t *Y, std::uint8_t *U, std::uint8_t *V, std::uint32_t pitch, cudaTextureObject_t texture, stream_t::pointer stream, const viewport_t &viewport);

    void apply_colorspace(const video::sunshine_colorspace_t &colorspace);

    int load_ram(platf::img_t &img, cudaArray_t array);

    ptr_t color_matrix;

    int threadsPerBlock;

    viewport_t viewport;

    float scale;
  };
}  // namespace cuda

#endif
