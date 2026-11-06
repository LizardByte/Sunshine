/**
 * @file src/platform/linux/cuda_gl_textures.h
 * @brief Declarations for the GL textures that GL->CUDA conversion renders into.
 */
#pragma once

// standard includes
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ranges>
#include <utility>

// local includes
#include "cuda.h"
#include "graphics.h"

struct AVPixFmtDescriptor;

namespace cuda {
  /**
   * @brief What a frame's copies out of the planes act on.
   */
  struct gl_cuda_frame_copy_t {
    const ::CudaFunctions &cu;  ///< CUDA function table.
    gl_cuda_shared_planes_t &shared;  ///< Shared planes.
    CUstream stream;  ///< CUDA stream to queue the copies on.
    const AVFrame &frame;  ///< Target frame.
    const AVPixFmtDescriptor &fmt_desc;  ///< Layout of the target frame.
    std::array<CUgraphicsResource, 3> resources;  ///< Graphics resource of each plane, when mapping the textures.
    int plane_count;  ///< Number of planes to copy.
  };

  /**
   * @brief Build the steps of handing a converted frame from GL to CUDA.
   *
   * @param copy What the copies act on.
   * @param render Renders the frame into the plane textures with GL.
   * @return The steps.
   */
  gl_cuda_handoff_ops_t make_gl_cuda_handoff_ops(const gl_cuda_frame_copy_t &copy, std::function<void()> render);

  /**
   * @brief Register a plane's plain GL texture with CUDA, for mapping it every frame.
   *
   * @param cu CUDA function table.
   * @param resource Receives the graphics resource.
   * @param texture The texture.
   * @return 0 on success or -1 on failure.
   */
  int register_plane_texture(const ::CudaFunctions &cu, CUgraphicsResource *resource, std::uint32_t texture);

  /**
   * @brief GL textures that the color conversion renders into, in the layout of the stream.
   */
  struct gl_cuda_textures_t {
    egl::nv12_t nv12;  ///< EGL/OpenGL resources used for NV12 output frames.
    egl::yuv444_t yuv444;  ///< EGL/OpenGL resources used for YUV444 output frames.
  };

  /**
   * @brief Registers one plane's plain GL texture with CUDA, for mapping it every frame.
   *
   * The arguments are the plane index and the texture. Returns 0 on success or -1 on failure.
   */
  using register_texture_fn = std::function<int(std::size_t plane, std::uint32_t texture)>;

  /**
   * @brief Create the textures, sharing their memory with CUDA when possible.
   *
   * Otherwise they are plain GL textures, registered with CUDA for mapping them every frame.
   *
   * @param textures Receives the textures.
   * @param shared Shared planes, which end up active only when they back the textures.
   * @param width Frame width.
   * @param height Frame height.
   * @param format Frame format.
   * @param is_yuv444 Whether the frame has three planes rather than two.
   * @param register_texture Registers a plain texture with CUDA.
   * @return 0 on success or -1 on failure.
   */
  inline int create_gl_cuda_textures(gl_cuda_textures_t &textures, gl_cuda_shared_planes_t &shared, int width, int height, AVPixelFormat format, bool is_yuv444, const register_texture_fn &register_texture) {
    const auto create = [&](const egl::target_storage_fn &storage) {
      // On failure, the target's textures are deleted here, before their memory is released.
      if (is_yuv444) {
        auto target = egl::create_yuv444_target(width, height, format, storage);
        if (!target) {
          return -1;
        }
        textures.yuv444 = std::move(*target);
      } else {
        auto target = egl::create_nv12_target(width, height, format, storage);
        if (!target) {
          return -1;
        }
        textures.nv12 = std::move(*target);
      }

      return 0;
    };

    return create_gl_cuda_target({
      .init_shared = [&shared] {
        return shared.init();
      },
      .create_shared_target = [&] {
        return create([&shared](std::size_t plane, GLenum internal_format, int plane_width, int plane_height) {
          return shared.allocate(plane, internal_format, plane_width, plane_height);
        });
      },
      .reset_shared = [&shared] {
        shared.reset();
      },
      .create_mapped_target = [&create] {
        return create({});
      },
      .register_mapped_target = [&] {
        const auto &tex = is_yuv444 ? textures.yuv444->tex : textures.nv12->tex;
        const auto plane_count = static_cast<std::size_t>(is_yuv444 ? 3 : 2);
        const bool registered = std::ranges::all_of(std::views::iota(std::size_t {0}, plane_count), [&](std::size_t plane) {
          return register_texture(plane, tex[plane]) == 0;
        });
        return registered ? 0 : -1;
      },
    });
  }
}  // namespace cuda
