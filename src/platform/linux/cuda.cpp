/**
 * @file src/platform/linux/cuda.cpp
 * @brief Definitions for CUDA encoding.
 */
// standard includes
#include <array>
#include <bitset>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

// lib includes
#include <ffnvcodec/dynlink_loader.h>
#include <NvFBC.h>
#ifdef SUNSHINE_BUILD_VULKAN
  #include <vulkan/vulkan.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/imgutils.h>
}

// local includes
#include "cuda.h"
#include "graphics.h"
#include "src/logging.h"
#include "src/utility.h"
#include "src/video.h"
#include "wayland.h"

/**
 * @def SUNSHINE_STRINGVIEW_HELPER(x)
 * @brief Macro for SUNSHINE STRINGVIEW HELPER.
 */
#define SUNSHINE_STRINGVIEW_HELPER(x) x##sv
/**
 * @def SUNSHINE_STRINGVIEW(x)
 * @brief Macro for SUNSHINE STRINGVIEW.
 */
#define SUNSHINE_STRINGVIEW(x) SUNSHINE_STRINGVIEW_HELPER(x)

/**
 * @def CU_CHECK(x, y)
 * @brief Macro for CU CHECK.
 */
#define CU_CHECK(x, y) \
  if (check((x), SUNSHINE_STRINGVIEW(y ": "))) \
  return -1

/**
 * @def CU_CHECK_IGNORE(x, y)
 * @brief Macro for CU CHECK IGNORE.
 */
#define CU_CHECK_IGNORE(x, y) \
  check((x), SUNSHINE_STRINGVIEW(y ": "))

namespace fs = std::filesystem;

using namespace std::literals;

namespace cuda {
  constexpr auto cudaDevAttrMaxThreadsPerBlock = (CUdevice_attribute) 1;  ///< CUDA dev attr max threads per block.
  constexpr auto cudaDevAttrMaxThreadsPerMultiProcessor = (CUdevice_attribute) 39;  ///< CUDA dev attr max threads per multi processor.

  /**
   * @brief Convert a CUDA result code into Sunshine's capture status.
   *
   * @param sv String view containing the text to inspect.
   * @param name Human-readable name to assign.
   * @param description Human-readable description used in log output.
   */
  void pass_error(const std::string_view &sv, const char *name, const char *description) {
    BOOST_LOG(error) << sv << name << ':' << description;
  }

  /**
   * @brief Release a Core Foundation object when the wrapper is destroyed.
   *
   * @param cf Core Foundation object passed to the scoped releaser.
   */
  void cff(CudaFunctions *cf) {
    cuda_free_functions(&cf);
  }

  /**
   * @brief Handle to a CUDA dynamic-library function table.
   */
  using cdf_t = util::safe_ptr<CudaFunctions, cff>;

  static cdf_t cdf;

  inline static int check(CUresult result, const std::string_view &sv) {
    if (result != CUDA_SUCCESS) {
      const char *name;
      const char *description;

      cdf->cuGetErrorName(result, &name);
      cdf->cuGetErrorString(result, &description);

      BOOST_LOG(error) << sv << name << ':' << description;
      return -1;
    }

    return 0;
  }

  /**
   * @brief Release stream resources.
   *
   * @param stream CUDA stream or PipeWire stream involved in the operation.
   */
  void freeStream(CUstream stream) {
    CU_CHECK_IGNORE(cdf->cuStreamDestroy(stream), "Couldn't destroy cuda stream");
  }

  /**
   * @brief Unregister a CUDA graphics resource if it is still registered.
   *
   * @param resource CUDA graphics resource being mapped or unmapped.
   */
  void unregisterResource(CUgraphicsResource resource) {
    CU_CHECK_IGNORE(cdf->cuGraphicsUnregisterResource(resource), "Couldn't unregister resource");
  }

  /**
   * @brief CUDA graphics resource pointer released with `cuGraphicsUnregisterResource`.
   */
  using registered_resource_t = util::safe_ptr<CUgraphicsResource_st, unregisterResource>;

  /**
   * @brief CUDA image wrapper that owns mapped graphics resources for one frame.
   */
  class img_t: public platf::img_t {
  public:
    tex_t tex;  ///< CUDA texture object used as the conversion source.
  };

  /**
   * @brief Map CUDA graphics resources for use as an image.
   *
   * @return 0 on success; nonzero or negative platform status on failure.
   */
  int init() {
    auto status = cuda_load_functions(&cdf, nullptr);
    if (status) {
      BOOST_LOG(error) << "Couldn't load cuda: "sv << status;

      return -1;
    }

    CU_CHECK(cdf->cuInit(0), "Couldn't initialize cuda");

    return 0;
  }

  /**
   * @brief CUDA encode device that imports captured frames into CUDA memory.
   */
  class cuda_t: public platf::avcodec_encode_device_t {
  public:
    /**
     * @brief Initialize the CUDA device context used for frame conversion.
     *
     * @param in_width In width.
     * @param in_height In height.
     * @return 0 on success; nonzero or negative platform status on failure.
     */
    int init(int in_width, int in_height) {
      if (!cdf) {
        BOOST_LOG(warning) << "cuda not initialized"sv;
        return -1;
      }

      hardware = true;

      width = in_width;
      height = in_height;

      return 0;
    }

    /**
     * @brief Attach frame resources used by the next conversion or encode operation.
     *
     * @param new_frame Video or graphics frame being processed.
     * @param hw_frames_ctx FFmpeg hardware frames context associated with the frame.
     * @return Status from updating frame.
     */
    int set_frame(AVFrame *new_frame, AVBufferRef *hw_frames_ctx) override {
      this->hwframe.reset(new_frame);
      this->frame = new_frame;

      auto hwframe_ctx = (AVHWFramesContext *) hw_frames_ctx->data;

      if (hwframe_ctx->sw_format != AV_PIX_FMT_NV12 && hwframe_ctx->sw_format != AV_PIX_FMT_YUV444P) {
        BOOST_LOG(error) << "cuda::cuda_t doesn't support any format other than AV_PIX_FMT_NV12 and AV_PIX_FMT_YUV444P"sv;
        return -1;
      }

      if (!new_frame->buf[0]) {
        if (av_hwframe_get_buffer(hw_frames_ctx, new_frame, 0)) {
          BOOST_LOG(error) << "Couldn't get hwframe for NVENC"sv;
          return -1;
        }
      }

      is_yuv444 = (hwframe_ctx->sw_format == AV_PIX_FMT_YUV444P);

      auto cuda_ctx = (AVCUDADeviceContext *) hwframe_ctx->device_ctx->hwctx;

      stream = make_stream();
      if (!stream) {
        return -1;
      }

      cuda_ctx->stream = stream.get();

      auto sws_opt = sws_t::make(width, height, new_frame->width, new_frame->height, width * 4);
      if (!sws_opt) {
        return -1;
      }

      sws = std::move(*sws_opt);

      linear_interpolation = width != new_frame->width || height != new_frame->height;

      return 0;
    }

    /**
     * @brief Apply the configured colorspace metadata to the active frame.
     */
    void apply_colorspace() override {
      sws.apply_colorspace(colorspace);

      auto tex = tex_t::make(height, width * 4);
      if (!tex) {
        return;
      }

      // The default green color is ugly.
      // Update the background color
      platf::img_t img;
      img.width = width;
      img.height = height;
      img.pixel_pitch = 4;
      img.row_pitch = img.width * img.pixel_pitch;

      std::vector<std::uint8_t> image_data;
      image_data.resize(img.row_pitch * img.height);

      img.data = image_data.data();

      if (sws.load_ram(img, tex->array)) {
        return;
      }

      if (is_yuv444) {
        sws.convert_yuv444(frame->data[0], frame->data[1], frame->data[2], frame->linesize[0], tex->texture.linear, stream.get(), {frame->width, frame->height, 0, 0});
      } else {
        sws.convert_nv12(frame->data[0], frame->data[1], frame->linesize[0], frame->linesize[1], tex->texture.linear, stream.get(), {frame->width, frame->height, 0, 0});
      }
    }

    /**
     * @brief Select the CUDA texture object for the configured filtering mode.
     *
     * @param tex Texture resource used by the converter.
     * @return CUDA texture object using linear or point sampling.
     */
    cudaTextureObject_t tex_obj(const tex_t &tex) const {
      return linear_interpolation ? tex.texture.linear : tex.texture.point;
    }

    stream_t stream;  ///< CUDA stream used for asynchronous conversion work.
    frame_t hwframe;  ///< FFmpeg hardware frame backed by CUDA resources.

    int height;  ///< Frame or display height in pixels.
    int width;  ///< Frame or display width in pixels.

    // When height and width don't change, it's not necessary to use linear interpolation
    bool linear_interpolation;  ///< Whether the CUDA converter uses linear interpolation.

    bool is_yuv444;  ///< Whether the CUDA converter outputs YUV 4:4:4.

    sws_t sws;  ///< Software scaler used for CUDA frame conversion fallback paths.
  };

  /**
   * @brief CUDA encode device path that converts frames through system memory.
   */
  class cuda_ram_t: public cuda_t {
  public:
    /**
     * @brief Convert a captured frame through CUDA into system-memory encoder input.
     *
     * @param img Image or frame object to read from or populate.
     * @return Conversion status.
     */
    int convert(platf::img_t &img) override {
      if (is_yuv444) {
        return sws.load_ram(img, tex.array) || sws.convert_yuv444(frame->data[0], frame->data[1], frame->data[2], frame->linesize[0], tex_obj(tex), stream.get());
      }
      return sws.load_ram(img, tex.array) || sws.convert_nv12(frame->data[0], frame->data[1], frame->linesize[0], frame->linesize[1], tex_obj(tex), stream.get());
    }

    /**
     * @brief Attach frame resources used by the next conversion or encode operation.
     *
     * @param new_frame Video or graphics frame being processed.
     * @param hw_frames_ctx FFmpeg hardware frames context associated with the frame.
     * @return Status from updating frame.
     */
    int set_frame(AVFrame *new_frame, AVBufferRef *hw_frames_ctx) override {
      if (cuda_t::set_frame(new_frame, hw_frames_ctx)) {
        return -1;
      }

      auto tex_opt = tex_t::make(height, width * 4);
      if (!tex_opt) {
        return -1;
      }

      tex = std::move(*tex_opt);

      return 0;
    }

    tex_t tex;  ///< CUDA texture object used as the conversion source.
  };

  /**
   * @brief CUDA encode device path that keeps converted frames in GPU memory.
   */
  class cuda_vram_t: public cuda_t {
  public:
    /**
     * @brief Convert a captured frame through CUDA into GPU encoder input.
     *
     * @param img Image or frame object to read from or populate.
     * @return Conversion status.
     */
    int convert(platf::img_t &img) override {
      if (is_yuv444) {
        return sws.convert_yuv444(frame->data[0], frame->data[1], frame->data[2], frame->linesize[0], tex_obj(((img_t *) &img)->tex), stream.get());
      }
      return sws.convert_nv12(frame->data[0], frame->data[1], frame->linesize[0], frame->linesize[1], tex_obj(((img_t *) &img)->tex), stream.get());
    }
  };

  namespace {
    bool cuda_is_initialized() {
      return static_cast<bool>(cdf);
    }

    CUresult cuda_get_device(CUdevice *device, int index) {
      return cdf->cuDeviceGet(device, index);
    }

    CUresult cuda_get_pci_bus_id(char *pci_bus_id, int len, CUdevice device) {
      return cdf->cuDeviceGetPCIBusId(pci_bus_id, len, device);
    }

    const cuda_pci_accessors_t cuda_pci_accessors {
      .is_initialized = cuda_is_initialized,
      .initialize = init,
      .get_device = cuda_get_device,
      .get_pci_bus_id = cuda_get_pci_bus_id,
    };
  }  // namespace

  /**
   * @brief Query the PCI bus id of a CUDA device.
   * @param index CUDA device index to query.
   * @return Lowercase PCI bus id (e.g. "00000000:0f:00.0"), or no value when
   *         CUDA is unavailable or the query fails.
   */
  std::optional<std::string> device_pci_bus_id(int index) {
    return device_pci_bus_id(index, cuda_pci_accessors);
  }

  /**
   * @brief Query the PCI bus id of a CUDA device with injected driver operations.
   * @param index CUDA device index to query.
   * @param accessors Driver operations used to initialize CUDA and query the device.
   * @return Lowercase PCI bus id (e.g. "00000000:0f:00.0"), or no value when
   *         CUDA is unavailable or the query fails.
   */
  std::optional<std::string> device_pci_bus_id(int index, const cuda_pci_accessors_t &accessors) {
    if (!accessors.is_initialized() && accessors.initialize() != 0) {
      return std::nullopt;
    }

    CUdevice device;
    const auto device_status = accessors.get_device(&device, index);
    if (device_status != CUDA_SUCCESS) {
      BOOST_LOG(error) << "Couldn't get CUDA device: "sv << device_status;
      return std::nullopt;
    }

    // Room for both the legacy "DDDDDDDD:BB:DD.F" rendering and the short
    // "DDDD:BB:DD.F" form current drivers return, plus NUL.
    std::array<char, 32> pci_bus_id {};
    const auto bus_id_status = accessors.get_pci_bus_id(pci_bus_id.data(), pci_bus_id.size(), device);
    if (bus_id_status != CUDA_SUCCESS) {
      BOOST_LOG(error) << "Couldn't get CUDA device PCI bus ID: "sv << bus_id_status;
      return std::nullopt;
    }

    // Linux renders PCI addresses lowercase while CUDA uses uppercase.
    std::string bus_id {pci_bus_id.data()};
    std::transform(bus_id.begin(), bus_id.end(), bus_id.begin(), [](char c) {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return bus_id;
  }

  /**
   * @brief Opens the DRM device associated with the CUDA device index.
   * @param index CUDA device index to open.
   * @return File descriptor or -1 on failure.
   */
  file_t open_drm_fd_for_cuda_device(int index) {
    CUdevice device;
    CU_CHECK(cdf->cuDeviceGet(&device, index), "Couldn't get CUDA device");

    // There's no way to directly go from CUDA to a DRM device, so we'll
    // use sysfs to look up the DRM device name from the PCI ID.
    std::array<char, 13> pci_bus_id;
    CU_CHECK(cdf->cuDeviceGetPCIBusId(pci_bus_id.data(), pci_bus_id.size(), device), "Couldn't get CUDA device PCI bus ID");
    BOOST_LOG(debug) << "Found CUDA device with PCI bus ID: "sv << pci_bus_id.data();

    // Linux uses lowercase hexadecimal while CUDA uses uppercase
    std::ranges::transform(pci_bus_id, pci_bus_id.begin(), [](char c) {
      return std::tolower(c);
    });

    // Look for the DRM nodes in sysfs. The render node is preferred: GBM only needs
    // rendering, and headless containers are often given renderD* without card*.
    try {
      char sysfs_path[PATH_MAX];
      std::snprintf(sysfs_path, sizeof(sysfs_path), "/sys/bus/pci/devices/%s/drm", pci_bus_id.data());
      fs::path sysfs_dir {sysfs_path};
      fs::path dri_path {"/dev/dri"sv};

      for (auto &entry : fs::directory_iterator {sysfs_dir}) {
        auto file = entry.path().filename();
        auto filestring = file.generic_string();
        if (std::string_view {filestring}.substr(0, 7) != "renderD"sv) {
          continue;
        }

        BOOST_LOG(debug) << "Found DRM render node: "sv << filestring;

        auto device_path = dri_path / file;
        int fd = open(device_path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
          return fd;
        }

        std::array<char, 1024> message;
        BOOST_LOG(warning) << "Couldn't open render node "sv << device_path.string() << ": "sv
                           << strerror_r(errno, message.data(), message.size()) << ", falling back to the primary node"sv;
      }

      for (auto &entry : fs::directory_iterator {sysfs_dir}) {
        auto file = entry.path().filename();
        auto filestring = file.generic_string();
        if (std::string_view {filestring}.substr(0, 4) != "card"sv) {
          continue;
        }

        BOOST_LOG(debug) << "Found DRM primary node: "sv << filestring;

        auto device_path = dri_path / file;
        int fd = platf::kms::privileged_open_drm_card_fd(device_path.c_str());
        if (fd >= 0) {
          return fd;
        }
      }
    } catch (const std::filesystem::filesystem_error &err) {
      BOOST_LOG(error) << "Failed to read sysfs: "sv << err.what();
    }

    BOOST_LOG(error) << "Unable to find DRM device with PCI bus ID: "sv << pci_bus_id.data();
    return -1;
  }

  /**
   * @brief CUDA frame resources registered for interop conversion.
   */
  struct cu_resources {
    registered_resource_t y_res;  ///< Y res.
    registered_resource_t u_res;  ///< U res.
    registered_resource_t v_res;  ///< V res.
    registered_resource_t uv_res;  ///< Uv res.
  };

  /**
   * @brief Output plane textures shared by GL and CUDA through external memory.
   *
   * Vulkan allocates exportable memory for each plane. GL and CUDA both import it,
   * so CUDA reads what GL renders without mapping the textures.
   *
   * Each handoff waits on the CPU: a GL fence before the copies, and a CUDA event
   * before GL renders into the planes again. Handing off on the GPU instead, by
   * mapping or with external semaphores, makes GL and CUDA wait on each other inside
   * the GPU, which stalls other GPU work such as a game.
   *
   * Without Vulkan, init() always fails and the caller maps the textures every frame.
   */
  class gl_cuda_shared_planes_t {
  public:
    gl_cuda_shared_planes_t() = default;
    gl_cuda_shared_planes_t(const gl_cuda_shared_planes_t &) = delete;
    gl_cuda_shared_planes_t &operator=(const gl_cuda_shared_planes_t &) = delete;

    ~gl_cuda_shared_planes_t() {
      reset();
    }

    /**
     * @brief Create a Vulkan device on the current CUDA device to allocate the shared memory.
     *
     * @return 0 on success or -1 when GL, CUDA or Vulkan can't share memory here.
     */
    int init() {
#ifndef SUNSHINE_BUILD_VULKAN
      return -1;
#else
      if (!gl::ctx.EXT_memory_object_fd) {
        BOOST_LOG(info) << "GL lacks GL_EXT_memory_object_fd"sv;
        return -1;
      }

      if (!cdf->cuDeviceGetUuid || !cdf->cuImportExternalMemory) {
        BOOST_LOG(info) << "CUDA driver lacks external memory interop"sv;
        return -1;
      }

      CUdevice cu_device;
      CU_CHECK(cdf->cuCtxGetDevice(&cu_device), "Couldn't get CUDA device");
      CUuuid uuid;
      CU_CHECK(cdf->cuDeviceGetUuid(&uuid, cu_device), "Couldn't get CUDA device UUID");

      VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
      app.apiVersion = VK_API_VERSION_1_1;

      VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
      instance_info.pApplicationInfo = &app;
      if (vkCreateInstance(&instance_info, nullptr, &instance) != VK_SUCCESS) {
        BOOST_LOG(info) << "Couldn't create Vulkan instance for GL/CUDA sharing"sv;
        return -1;
      }

      physical_device = find_physical_device(uuid);
      if (physical_device == VK_NULL_HANDLE) {
        BOOST_LOG(info) << "No Vulkan device matches the CUDA device"sv;
        return -1;
      }

      const float priority = 1.0f;
      VkDeviceQueueCreateInfo queue_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
      queue_info.queueFamilyIndex = 0;
      queue_info.queueCount = 1;
      queue_info.pQueuePriorities = &priority;

      const char *extension = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
      VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
      device_info.queueCreateInfoCount = 1;
      device_info.pQueueCreateInfos = &queue_info;
      device_info.enabledExtensionCount = 1;
      device_info.ppEnabledExtensionNames = &extension;
      if (vkCreateDevice(physical_device, &device_info, nullptr, &device) != VK_SUCCESS) {
        BOOST_LOG(info) << "Couldn't create Vulkan device with external memory export"sv;
        return -1;
      }

      get_memory_fd = (PFN_vkGetMemoryFdKHR) vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR");
      if (!get_memory_fd) {
        return -1;
      }

      CU_CHECK(cdf->cuEventCreate(&copy_done, CU_EVENT_DISABLE_TIMING), "Couldn't create CUDA copy event");

      active = true;
      return 0;
#endif
    }

    /**
     * @brief Back the texture bound to `GL_TEXTURE_2D` with exportable memory and import it into CUDA.
     *
     * Matches egl::target_storage_fn.
     *
     * @param plane Plane index.
     * @param internal_format Sized GL internal format of the plane.
     * @param width Plane width.
     * @param height Plane height.
     * @return 0 on success or -1 on failure.
     */
    int allocate([[maybe_unused]] std::size_t plane, [[maybe_unused]] GLenum internal_format, [[maybe_unused]] int width, [[maybe_unused]] int height) {
#ifndef SUNSHINE_BUILD_VULKAN
      return -1;
#else
      struct format_t {
        GLenum gl;
        VkFormat vk;
        CUarray_format cu;
        unsigned int channels;
      };

      constexpr std::array<format_t, 4> formats {{
        {GL_R8, VK_FORMAT_R8_UNORM, CU_AD_FORMAT_UNSIGNED_INT8, 1},
        {GL_RG8, VK_FORMAT_R8G8_UNORM, CU_AD_FORMAT_UNSIGNED_INT8, 2},
        {GL_R16, VK_FORMAT_R16_UNORM, CU_AD_FORMAT_UNSIGNED_INT16, 1},
        {GL_RG16, VK_FORMAT_R16G16_UNORM, CU_AD_FORMAT_UNSIGNED_INT16, 2},
      }};
      const auto format = std::ranges::find(formats, internal_format, &format_t::gl);
      if (format == formats.end()) {
        BOOST_LOG(error) << "Unsupported shared plane format: "sv << util::hex(internal_format).to_string_view();
        return -1;
      }

      auto &p = planes[plane];

      VkExternalMemoryImageCreateInfo external_info = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
      external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
      VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      image_info.pNext = &external_info;
      image_info.imageType = VK_IMAGE_TYPE_2D;
      image_info.format = format->vk;
      image_info.extent = {(std::uint32_t) width, (std::uint32_t) height, 1};
      image_info.mipLevels = 1;
      image_info.arrayLayers = 1;
      image_info.samples = VK_SAMPLE_COUNT_1_BIT;
      image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
      image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      if (vkCreateImage(device, &image_info, nullptr, &p.image) != VK_SUCCESS) {
        BOOST_LOG(error) << "Couldn't create Vulkan image for plane "sv << plane;
        return -1;
      }

      VkMemoryRequirements requirements;
      vkGetImageMemoryRequirements(device, p.image, &requirements);

      VkPhysicalDeviceMemoryProperties memory_props;
      vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_props);
      std::uint32_t type_index = 0;
      while (type_index < memory_props.memoryTypeCount &&
             !((requirements.memoryTypeBits & (1u << type_index)) && (memory_props.memoryTypes[type_index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))) {
        ++type_index;
      }

      VkMemoryDedicatedAllocateInfo dedicated_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
      dedicated_info.image = p.image;
      VkExportMemoryAllocateInfo export_info = {.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
      export_info.pNext = &dedicated_info;
      export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
      VkMemoryAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      alloc_info.pNext = &export_info;
      alloc_info.allocationSize = requirements.size;
      alloc_info.memoryTypeIndex = type_index;
      if (type_index == memory_props.memoryTypeCount || vkAllocateMemory(device, &alloc_info, nullptr, &p.memory) != VK_SUCCESS || vkBindImageMemory(device, p.image, p.memory, 0) != VK_SUCCESS) {
        BOOST_LOG(error) << "Couldn't allocate exportable memory for plane "sv << plane;
        return -1;
      }

      // GL and CUDA each import their own fd, and take ownership of it on success.
      VkMemoryGetFdInfoKHR fd_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
      fd_info.memory = p.memory;
      fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
      int gl_fd = -1;
      CUDA_EXTERNAL_MEMORY_HANDLE_DESC memory_desc {};
      memory_desc.handle.fd = -1;
      if (get_memory_fd(device, &fd_info, &gl_fd) != VK_SUCCESS || get_memory_fd(device, &fd_info, &memory_desc.handle.fd) != VK_SUCCESS) {
        BOOST_LOG(error) << "Couldn't export memory for plane "sv << plane;
        close(gl_fd);
        return -1;
      }

      const GLint dedicated = GL_TRUE;
      gl::ctx.CreateMemoryObjectsEXT(1, &p.gl_memory);
      gl::ctx.MemoryObjectParameterivEXT(p.gl_memory, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
      gl::ctx.ImportMemoryFdEXT(p.gl_memory, requirements.size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, gl_fd);
      if (auto err = gl::ctx.GetError(); err != GL_NO_ERROR) {
        BOOST_LOG(error) << "Couldn't import plane memory into GL: "sv << util::hex(err).to_string_view();
        close(gl_fd);
        close(memory_desc.handle.fd);
        return -1;
      }
      gl::ctx.TexStorageMem2DEXT(GL_TEXTURE_2D, 1, internal_format, width, height, p.gl_memory, 0);
      if (auto err = gl::ctx.GetError(); err != GL_NO_ERROR) {
        BOOST_LOG(error) << "Couldn't create GL texture on plane memory: "sv << util::hex(err).to_string_view();
        close(memory_desc.handle.fd);
        return -1;
      }

      memory_desc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
      memory_desc.size = requirements.size;
      memory_desc.flags = cu_external_memory_dedicated;
      if (check(cdf->cuImportExternalMemory(&p.cu_memory, &memory_desc), "Couldn't import plane memory into CUDA: "sv)) {
        close(memory_desc.handle.fd);
        return -1;
      }

      CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC array_desc {};
      array_desc.arrayDesc.Width = width;
      array_desc.arrayDesc.Height = height;
      array_desc.arrayDesc.Format = format->cu;
      array_desc.arrayDesc.NumChannels = format->channels;
      array_desc.arrayDesc.Flags = cu_array3d_color_attachment;
      array_desc.numLevels = 1;
      CU_CHECK(cdf->cuExternalMemoryGetMappedMipmappedArray(&p.cu_mipmap, p.cu_memory, &array_desc), "Couldn't map plane memory as a CUDA array");
      CU_CHECK(cdf->cuMipmappedArrayGetLevel(&p.cu_array, p.cu_mipmap, 0), "Couldn't get plane array");

      return 0;
#endif
    }

    /**
     * @brief Block until CUDA has copied the previous frame out of the planes.
     *
     * Call before GL renders into the planes.
     *
     * @return 0 on success or -1 on failure.
     */
    int wait_for_cuda() {
      if (!copies_pending) {
        return 0;
      }

      copies_pending = false;
      CU_CHECK(cdf->cuEventSynchronize(copy_done), "Couldn't wait for previous CUDA copy");
      return 0;
    }

    /**
     * @brief Block until GL has finished rendering into the planes.
     *
     * Call before queuing the copies out of the planes.
     *
     * @return 0 on success or -1 on failure.
     */
    int wait_for_gl() {
      auto fence = gl::ctx.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
      if (!fence) {
        BOOST_LOG(error) << "Couldn't create GL fence: "sv << gl::ctx.GetError();
        return -1;
      }

      // Generous timeout: this only guards against a hung GPU.
      auto status = gl::ctx.ClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, std::chrono::nanoseconds(1s).count());
      gl::ctx.DeleteSync(fence);
      if (status == GL_TIMEOUT_EXPIRED || status == GL_WAIT_FAILED) {
        BOOST_LOG(error) << "Waiting for GL fence failed: "sv << util::hex(status).to_string_view();
        return -1;
      }

      return 0;
    }

    /**
     * @brief Note that this frame's copies out of the planes are queued, so wait_for_cuda() waits for them.
     *
     * @param stream CUDA stream the copies were queued on.
     * @return 0 on success or -1 on failure.
     */
    int record_copies(CUstream stream) {
      CU_CHECK(cdf->cuEventRecord(copy_done, stream), "Couldn't record CUDA copy event");
      copies_pending = true;
      return 0;
    }

    /**
     * @brief CUDA array backing a plane.
     *
     * @param plane Plane index.
     * @return The plane's CUDA array.
     */
    CUarray array(std::size_t plane) const {
      return planes[plane].cu_array;
    }

    /**
     * @brief Whether the planes are shared and the handoff methods apply.
     *
     * @return True after a successful init() until reset().
     */
    bool is_active() const {
      return active;
    }

    /**
     * @brief Release everything init() and allocate() created. The plane textures must already be deleted.
     */
    void reset() {
      for (auto &p : planes) {
        if (p.cu_mipmap) {
          CU_CHECK_IGNORE(cdf->cuMipmappedArrayDestroy(p.cu_mipmap), "Couldn't destroy CUDA mipmapped array");
        }
        if (p.cu_memory) {
          CU_CHECK_IGNORE(cdf->cuDestroyExternalMemory(p.cu_memory), "Couldn't destroy CUDA external memory");
        }
        if (p.gl_memory) {
          gl::ctx.DeleteMemoryObjectsEXT(1, &p.gl_memory);
        }
#ifdef SUNSHINE_BUILD_VULKAN
        if (device != VK_NULL_HANDLE) {
          vkDestroyImage(device, p.image, nullptr);
          vkFreeMemory(device, p.memory, nullptr);
        }
#endif
        p = {};
      }

      if (copy_done) {
        CU_CHECK_IGNORE(cdf->cuEventDestroy(copy_done), "Couldn't destroy CUDA event");
        copy_done = nullptr;
      }

#ifdef SUNSHINE_BUILD_VULKAN
      vkDestroyDevice(device, nullptr);
      vkDestroyInstance(instance, nullptr);
      instance = VK_NULL_HANDLE;
      physical_device = VK_NULL_HANDLE;
      device = VK_NULL_HANDLE;
#endif
      copies_pending = false;
      active = false;
    }

  private:
    /**
     * @brief Memory backing one plane, as seen by Vulkan, GL and CUDA.
     */
    struct plane_t {
#ifdef SUNSHINE_BUILD_VULKAN
      VkImage image = VK_NULL_HANDLE;  ///< Vulkan image that owns the allocation's layout.
      VkDeviceMemory memory = VK_NULL_HANDLE;  ///< Exportable Vulkan allocation.
#endif
      GLuint gl_memory = 0;  ///< GL memory object imported from the allocation.
      CUexternalMemory cu_memory = nullptr;  ///< CUDA external memory imported from the allocation.
      CUmipmappedArray cu_mipmap = nullptr;  ///< CUDA view of the allocation.
      CUarray cu_array = nullptr;  ///< Level 0 of `cu_mipmap`, the source of the copies.
    };

#ifdef SUNSHINE_BUILD_VULKAN
    static constexpr unsigned int cu_external_memory_dedicated = 0x1;  ///< `CUDA_EXTERNAL_MEMORY_DEDICATED`, missing from ffnvcodec.
    static constexpr unsigned int cu_array3d_color_attachment = 0x20;  ///< `CUDA_ARRAY3D_COLOR_ATTACHMENT`, missing from ffnvcodec.

    /**
     * @brief Find the Vulkan physical device with a given UUID.
     *
     * @param uuid UUID of the CUDA device.
     * @return The matching device, or `VK_NULL_HANDLE`.
     */
    VkPhysicalDevice find_physical_device(const CUuuid &uuid) const {
      std::uint32_t count = 0;
      vkEnumeratePhysicalDevices(instance, &count, nullptr);
      std::vector<VkPhysicalDevice> devices(count);
      vkEnumeratePhysicalDevices(instance, &count, devices.data());

      for (auto dev : devices) {
        VkPhysicalDeviceIDProperties id = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 props = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        props.pNext = &id;
        vkGetPhysicalDeviceProperties2(dev, &props);

        if (std::memcmp(id.deviceUUID, uuid.bytes, VK_UUID_SIZE) == 0) {
          return dev;
        }
      }

      return VK_NULL_HANDLE;
    }

    VkInstance instance = VK_NULL_HANDLE;  ///< Vulkan instance owning `device`.
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;  ///< Vulkan view of the CUDA device.
    VkDevice device = VK_NULL_HANDLE;  ///< Vulkan device that allocates the shared memory.
    PFN_vkGetMemoryFdKHR get_memory_fd = nullptr;  ///< `vkGetMemoryFdKHR`.
#endif
    std::array<plane_t, 3> planes {};  ///< Shared memory of each plane.
    CUevent copy_done = nullptr;  ///< Recorded after each frame's copies out of the planes.
    bool copies_pending = false;  ///< Whether `copy_done` was recorded since wait_for_cuda() last waited on it.
    bool active = false;  ///< Whether init() succeeded.
  };

  /**
   * @brief OpenGL/CUDA interop resources used for GPU-side frame conversion.
   */
  class gl_cuda_vram_t: public platf::avcodec_encode_device_t {
  public:
    ~gl_cuda_vram_t() override {
      // Let queued copies finish before the shared planes they read are released.
      if (stream && cdf) {
        CU_CHECK_IGNORE(cdf->cuStreamSynchronize(stream.get()), "Couldn't synchronize CUDA stream");
      }
    }

    /**
     * @brief Initialize the GL->CUDA encoding device.
     * @param in_width Width of captured frames.
     * @param in_height Height of captured frames.
     * @param in_offset_x Offset of content in captured frame.
     * @param in_offset_y Offset of content in captured frame.
     * @return 0 on success or -1 on failure.
     */
    int init(int in_width, int in_height, int in_offset_x, int in_offset_y) {
      // This must be non-zero to tell the video core that it's a hardware encoding device.
      hardware = true;

      // TODO: Support more than one CUDA device
      file = std::move(open_drm_fd_for_cuda_device(0));
      if (file.el < 0) {
        std::array<char, 1024> string;
        BOOST_LOG(error) << "Couldn't open DRM FD for CUDA device: "sv << strerror_r(errno, string.data(), string.size());
        return -1;
      }

      gbm.reset(gbm::create_device(file.el));
      if (!gbm) {
        BOOST_LOG(error) << "Couldn't create GBM device: ["sv << util::hex(eglGetError()).to_string_view() << ']';
        return -1;
      }

      display = egl::make_display(gbm.get());
      if (!display) {
        return -1;
      }

      auto ctx_opt = egl::make_ctx(display.get());
      if (!ctx_opt) {
        return -1;
      }

      ctx = std::move(*ctx_opt);

      width = in_width;
      height = in_height;

      sequence = 0;

      this->offset_x = in_offset_x;
      this->offset_y = in_offset_y;

      return 0;
    }

    /**
     * @brief Initialize color conversion into target CUDA frame.
     * @param new_frame Destination CUDA frame to write into.
     * @param hw_frames_ctx_buf FFmpeg hardware frame context.
     * @return 0 on success or -1 on failure.
     */
    int set_frame(AVFrame *new_frame, AVBufferRef *hw_frames_ctx_buf) override {
      this->hwframe.reset(new_frame);
      this->frame = new_frame;

      auto hw_frames_ctx = (AVHWFramesContext *) hw_frames_ctx_buf->data;

      if (hw_frames_ctx->sw_format != AV_PIX_FMT_NV12 && hw_frames_ctx->sw_format != AV_PIX_FMT_YUV444P && hw_frames_ctx->sw_format != AV_PIX_FMT_P010LE && hw_frames_ctx->sw_format != AV_PIX_FMT_YUV444P16LE) {
        BOOST_LOG(error) << "cuda::gl_cuda_vram_t doesn't support any format other than AV_PIX_FMT_NV12, AV_PIX_FMT_P010LE, AV_PIX_FMT_YUV444P and AV_PIX_FMT_YUV444P16LE"sv;
        return -1;
      }

      if (!new_frame->buf[0]) {
        if (av_hwframe_get_buffer(hw_frames_ctx_buf, new_frame, 0)) {
          BOOST_LOG(error) << "Couldn't get hwframe for NVENC_GL"sv;
          return -1;
        }
      }

      sw_format = hw_frames_ctx->sw_format;
      is_yuv444 = (sw_format == AV_PIX_FMT_YUV444P || sw_format == AV_PIX_FMT_YUV444P16LE);

      auto sws_opt = egl::sws_t::make(width, height, new_frame->width, new_frame->height, sw_format, is_yuv444);
      if (!sws_opt) {
        return -1;
      }

      this->sws = std::move(*sws_opt);

      auto cuda_ctx = (AVCUDADeviceContext *) hw_frames_ctx->device_ctx->hwctx;

      stream = make_stream();
      if (!stream) {
        return -1;
      }

      cuda_ctx->stream = stream.get();

      // Share the output planes through external memory when possible, so CUDA can
      // read them without a synchronizing map/unmap every frame.
      if (shared.init() == 0) {
        auto storage = [this](std::size_t plane, GLenum format, int plane_width, int plane_height) {
          return shared.allocate(plane, format, plane_width, plane_height);
        };
        if (create_target(storage) == 0) {
          BOOST_LOG(info) << "Sharing GL->CUDA planes through external memory"sv;
          return 0;
        }
        BOOST_LOG(warning) << "Couldn't share GL->CUDA planes, falling back to mapping them every frame"sv;
      }
      shared.reset();

      if (create_target({})) {
        return -1;
      }

      if (is_yuv444) {
        CU_CHECK(cdf->cuGraphicsGLRegisterImage(&cu_res.y_res, yuv444->tex[0], GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY), "Couldn't register Y texture");
        CU_CHECK(cdf->cuGraphicsGLRegisterImage(&cu_res.u_res, yuv444->tex[1], GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY), "Couldn't register U texture");
        CU_CHECK(cdf->cuGraphicsGLRegisterImage(&cu_res.v_res, yuv444->tex[2], GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY), "Couldn't register V texture");
      } else {
        CU_CHECK(cdf->cuGraphicsGLRegisterImage(&cu_res.y_res, nv12->tex[0], GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY), "Couldn't register Y plane texture");
        CU_CHECK(cdf->cuGraphicsGLRegisterImage(&cu_res.uv_res, nv12->tex[1], GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY), "Couldn't register UV plane texture");
      }

      return 0;
    }

    /**
     * @brief Create the GL textures that the color conversion renders into.
     *
     * @param storage Allocator for the plane textures' storage, or empty for plain GL textures.
     * @return 0 on success or -1 on failure.
     */
    int create_target(const egl::target_storage_fn &storage) {
      // Drop any previous attempt's textures before their storage is released.
      yuv444 = egl::yuv444_t {};
      nv12 = egl::nv12_t {};

      if (is_yuv444) {
        auto yuv444_opt = egl::create_yuv444_target(frame->width, frame->height, sw_format, storage);
        if (!yuv444_opt) {
          return -1;
        }
        this->yuv444 = std::move(*yuv444_opt);
      } else {
        auto nv12_opt = egl::create_nv12_target(frame->width, frame->height, sw_format, storage);
        if (!nv12_opt) {
          return -1;
        }
        this->nv12 = std::move(*nv12_opt);
      }

      return 0;
    }

    /**
     * @brief Convert the captured image into the target CUDA frame.
     * @param img Captured screen image.
     * @return 0 on success or -1 on failure.
     */
    int convert(platf::img_t &img) override {
      auto &descriptor = (egl::img_descriptor_t &) img;

      if (descriptor.sequence == 0) {
        // For dummy images, use a blank RGB texture instead of importing a DMA-BUF
        rgb = egl::create_blank(img);
      } else if (descriptor.sequence > sequence) {
        sequence = descriptor.sequence;

        rgb = egl::rgb_t {};

        auto rgb_opt = egl::import_source(display.get(), descriptor.sd);

        if (!rgb_opt) {
          return -1;
        }

        rgb = std::move(*rgb_opt);
      }

      auto fmt_desc = av_pix_fmt_desc_get(sw_format);
      const int plane_count = is_yuv444 ? 3 : 2;

      sws.load_vram(descriptor, offset_x, offset_y, rgb->tex[0], is_yuv444);

      // Perform the color conversion and scaling in GL, once CUDA is done reading the previous frame
      if (shared.is_active() && shared.wait_for_cuda()) {
        return -1;
      }
      if (is_yuv444) {
        sws.convert_yuv444(yuv444->buf);
      } else {
        sws.convert_nv12(nv12->buf);
      }

      std::array<CUarray, 3> arrays {};
      std::array<CUgraphicsResource, 3> resources {};
      if (shared.is_active()) {
        if (shared.wait_for_gl()) {
          return -1;
        }
        for (int i = 0; i < plane_count; i++) {
          arrays[i] = shared.array(i);
        }
      } else {
        // Map the GL textures to read for CUDA
        if (is_yuv444) {
          resources = {{cu_res.y_res.get(), cu_res.u_res.get(), cu_res.v_res.get()}};
        } else {
          resources = {{cu_res.y_res.get(), cu_res.uv_res.get(), nullptr}};
        }
        CU_CHECK(cdf->cuGraphicsMapResources(plane_count, resources.data(), stream.get()), "Couldn't map GL textures in CUDA");
        for (int i = 0; i < plane_count; i++) {
          CU_CHECK(cdf->cuGraphicsSubResourceGetMappedArray(&arrays[i], resources[i], 0, 0), "Couldn't get mapped plane array");
        }
      }

      // Copy from the GL textures to the target CUDA frame
      for (int i = 0; i < plane_count; i++) {
        CUDA_MEMCPY2D cpy = {};
        cpy.srcMemoryType = CU_MEMORYTYPE_ARRAY;
        cpy.srcArray = arrays[i];

        cpy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
        cpy.dstDevice = (CUdeviceptr) frame->data[i];
        cpy.dstPitch = frame->linesize[i];
        cpy.WidthInBytes = (frame->width * fmt_desc->comp[i].step) >> (i ? fmt_desc->log2_chroma_w : 0);
        cpy.Height = frame->height >> (i ? fmt_desc->log2_chroma_h : 0);

        CU_CHECK_IGNORE(cdf->cuMemcpy2DAsync(&cpy, stream.get()), "Couldn't copy texture to CUDA frame");
      }

      if (shared.is_active()) {
        return shared.record_copies(stream.get());
      }

      // Unmap the textures to allow modification from GL again
      CU_CHECK(cdf->cuGraphicsUnmapResources(plane_count, resources.data(), stream.get()), "Couldn't unmap GL textures from CUDA");

      return 0;
    }

    /**
     * @brief Configures shader parameters for the specified colorspace.
     */
    void apply_colorspace() override {
      sws.apply_colorspace(colorspace, is_yuv444);
    }

    file_t file;  ///< File descriptor for the imported DMA-BUF.
    gbm::gbm_t gbm;  ///< GBM device used for buffer allocation..
    egl::display_t display;  ///< EGL display used to import captured frames.
    egl::ctx_t ctx;  ///< EGL context used to import captured frames.

    // This must be destroyed before display_t
    stream_t stream;  ///< CUDA stream used for asynchronous conversion work.
    frame_t hwframe;  ///< FFmpeg hardware frame backed by CUDA resources.

    // Holds the plane textures' memory, so it must outlive nv12 and yuv444
    gl_cuda_shared_planes_t shared;  ///< Output planes shared with CUDA, when external memory is available.

    egl::sws_t sws;  ///< Software scaler used for CUDA frame conversion fallback paths.
    egl::nv12_t nv12;  ///< EGL/OpenGL resources used for NV12 output frames.
    egl::yuv444_t yuv444;  ///< EGL/OpenGL resources used for YUV444 output frames.
    AVPixelFormat sw_format;  ///< FFmpeg software pixel format produced by conversion.

    int height;  ///< Frame or display height in pixels.
    int width;  ///< Frame or display width in pixels.

    std::uint64_t sequence;  ///< Capture sequence number associated with the frame.
    egl::rgb_t rgb;  ///< Imported RGB source image used before CUDA conversion.

    cu_resources cu_res;  ///< CUDA graphics resources registered for the current frame.

    int offset_x;  ///< Horizontal offset in physical pixels.
    int offset_y;  ///< Vertical offset in physical pixels.

    bool is_yuv444;  ///< Whether the CUDA converter outputs YUV 4:4:4.
  };

  /**
   * @brief Create AVCodec encode device.
   *
   * @param width Frame or display width in pixels.
   * @param height Frame or display height in pixels.
   * @param vram Whether the image should use GPU memory instead of system memory.
   * @return Constructed AVCodec encode device object.
   */
  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(int width, int height, bool vram) {
    if (init()) {
      return nullptr;
    }

    std::unique_ptr<cuda_t> cuda;

    if (vram) {
      cuda = std::make_unique<cuda_vram_t>();
    } else {
      cuda = std::make_unique<cuda_ram_t>();
    }

    if (cuda->init(width, height)) {
      return nullptr;
    }

    return cuda;
  }

  /**
   * @brief Create a GL->CUDA encoding device for consuming captured dmabufs.
   * @param width Width of captured frames.
   * @param height Height of captured frames.
   * @param offset_x Offset of content in captured frame.
   * @param offset_y Offset of content in captured frame.
   * @return FFmpeg encoding device context.
   */
  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_gl_encode_device(int width, int height, int offset_x, int offset_y) {
    if (init()) {
      return nullptr;
    }

    auto cuda = std::make_unique<gl_cuda_vram_t>();

    if (cuda->init(width, height, offset_x, offset_y)) {
      return nullptr;
    }

    return cuda;
  }

  namespace nvfbc {
    static PNVFBCCREATEINSTANCE createInstance {};
    static NVFBC_API_FUNCTION_LIST func {.dwVersion = NVFBC_VERSION};

    /**
     * @brief Convert a C++ boolean to the corresponding NvFBC flag.
     * @param b Boolean state to convert.
     * @return NVFBC_TRUE when b is true, otherwise NVFBC_FALSE.
     */
    static constexpr NVFBC_BOOL nv_bool(bool b) {
      return b ? NVFBC_TRUE : NVFBC_FALSE;
    }

    static dyn::library_t *library_handle {nullptr};  ///< Loaded NvFBC library retained while its function table is in use.

    /**
     * @brief Load NvFBC and create the CUDA capture helper.
     *
     * @return 0 on success; nonzero or negative platform status on failure.
     */
    int init() {
      static bool funcs_loaded = false;

      if (funcs_loaded) {
        return 0;
      }

      if (!library_handle) {
        library_handle = dyn::handle({"libnvidia-fbc.so.1", "libnvidia-fbc.so"});
        if (!library_handle) {
          return -1;
        }
      }

      std::vector<std::tuple<dyn::apiproc *, const char *>> funcs {
        {(dyn::apiproc *) &createInstance, "NvFBCCreateInstance"},
      };

      if (dyn::load(library_handle, funcs)) {
        dlclose(library_handle);
        library_handle = nullptr;

        return -1;
      }

      auto status = cuda::nvfbc::createInstance(&cuda::nvfbc::func);
      if (status) {
        BOOST_LOG(error) << "Unable to create NvFBC instance"sv;

        dlclose(library_handle);
        library_handle = nullptr;
        return -1;
      }

      funcs_loaded = true;
      return 0;
    }

    /**
     * @brief NvFBC CUDA context selected for a capture session.
     */
    class ctx_t {
    public:
      /**
       * @brief Explicitly create an NvFBC session context for a native capture handle.
       *
       * @param session_handle Native library or object handle used by the operation.
       */
      explicit ctx_t(NVFBC_SESSION_HANDLE session_handle) {
        NVFBC_BIND_CONTEXT_PARAMS params {NVFBC_BIND_CONTEXT_PARAMS_VER};

        if (func.nvFBCBindContext(session_handle, &params)) {
          BOOST_LOG(error) << "Couldn't bind NvFBC context to current thread: " << func.nvFBCGetLastErrorStr(session_handle);
        }

        this->handle = session_handle;
      }

      ~ctx_t() {
        NVFBC_RELEASE_CONTEXT_PARAMS params {NVFBC_RELEASE_CONTEXT_PARAMS_VER};
        if (func.nvFBCReleaseContext(handle, &params)) {
          BOOST_LOG(error) << "Couldn't release NvFBC context from current thread: " << func.nvFBCGetLastErrorStr(handle);
        }
      }

      NVFBC_SESSION_HANDLE handle;  ///< NVIDIA FBC capture session handle.
    };

    /**
     * @brief NvFBC dynamic-library handle and function table.
     */
    class handle_t {
      enum flag_e {
        SESSION_HANDLE,
        SESSION_CAPTURE,
        MAX_FLAGS,
      };

    public:
      handle_t() = default;

      /**
       * @brief Move an NvFBC API handle and its resolved function table.
       *
       * @param other Source object whose state is copied or moved into this object.
       */
      handle_t(handle_t &&other):
          handle_flags {other.handle_flags},
          handle {other.handle} {
        other.handle_flags.reset();
      }

      /**
       * @brief Assign state from another instance while preserving ownership semantics.
       *
       * @param other Source object whose state is copied or moved into this object.
       * @return Reference or value produced by the operator.
       */
      handle_t &operator=(handle_t &&other) {
        std::swap(handle_flags, other.handle_flags);
        std::swap(handle, other.handle);

        return *this;
      }

      /**
       * @brief Allocate the underlying object and wrap it in the owning handle.
       *
       * @return Created backend object, or null when creation fails.
       */
      static std::optional<handle_t> make() {
        NVFBC_CREATE_HANDLE_PARAMS params {.dwVersion = NVFBC_CREATE_HANDLE_PARAMS_VER};

        // Set privateData to allow NvFBC on consumer NVIDIA GPUs.
        // Based on https://github.com/keylase/nvidia-patch/blob/3193b4b1cea91527bf09ea9b8db5aade6a3f3c0a/win/nvfbcwrp/nvfbcwrp_main.cpp#L23-L25 .
        const std::array<unsigned int, 4> MAGIC_PRIVATE_DATA = {0xAEF57AC5, 0x401D1A39, 0x1B856BBE, 0x9ED0CEBA};
        params.privateData = MAGIC_PRIVATE_DATA.data();
        params.privateDataSize = sizeof(MAGIC_PRIVATE_DATA);

        handle_t handle;
        auto status = func.nvFBCCreateHandle(&handle.handle, &params);
        if (status) {
          BOOST_LOG(error) << "Failed to create session: "sv << handle.last_error();

          return std::nullopt;
        }

        handle.handle_flags[SESSION_HANDLE] = true;

        return handle;
      }

      /**
       * @brief Read the last error string from the active NvFBC session.
       *
       * @return Human-readable NvFBC error string.
       */
      const char *last_error() {
        return func.nvFBCGetLastErrorStr(handle);
      }

      /**
       * @brief Return or update the current status value.
       *
       * @return Status status.
       */
      std::optional<NVFBC_GET_STATUS_PARAMS> status() {
        NVFBC_GET_STATUS_PARAMS params {.dwVersion = NVFBC_GET_STATUS_PARAMS_VER};

        auto status = func.nvFBCGetStatus(handle, &params);
        if (status) {
          BOOST_LOG(error) << "Failed to get NvFBC status: "sv << last_error();

          return std::nullopt;
        }

        return params;
      }

      /**
       * @brief Run the capture loop for this backend.
       *
       * @param capture_params Capture params.
       * @return Capture status reported to the streaming pipeline.
       */
      int capture(NVFBC_CREATE_CAPTURE_SESSION_PARAMS &capture_params) {
        if (func.nvFBCCreateCaptureSession(handle, &capture_params)) {
          BOOST_LOG(error) << "Failed to start capture session: "sv << last_error();
          return -1;
        }

        handle_flags[SESSION_CAPTURE] = true;

        NVFBC_TOCUDA_SETUP_PARAMS setup_params {
          NVFBC_TOCUDA_SETUP_PARAMS_VER,
          NVFBC_BUFFER_FORMAT_BGRA,
        };

        if (func.nvFBCToCudaSetUp(handle, &setup_params)) {
          BOOST_LOG(error) << "Failed to setup cuda interop with nvFBC: "sv << last_error();
          return -1;
        }
        return 0;
      }

      /**
       * @brief Release the NvFBC capture session and wait for capture work to stop.
       *
       * @return Stop status.
       */
      int stop() {
        if (!handle_flags[SESSION_CAPTURE]) {
          return 0;
        }

        NVFBC_DESTROY_CAPTURE_SESSION_PARAMS params {NVFBC_DESTROY_CAPTURE_SESSION_PARAMS_VER};

        if (func.nvFBCDestroyCaptureSession(handle, &params)) {
          BOOST_LOG(error) << "Couldn't destroy capture session: "sv << last_error();

          return -1;
        }

        handle_flags[SESSION_CAPTURE] = false;

        return 0;
      }

      /**
       * @brief Reset the object to its initial empty state.
       *
       * @return Reset status.
       */
      int reset() {
        if (!handle_flags[SESSION_HANDLE]) {
          return 0;
        }

        stop();

        NVFBC_DESTROY_HANDLE_PARAMS params {NVFBC_DESTROY_HANDLE_PARAMS_VER};

        ctx_t ctx {handle};
        if (func.nvFBCDestroyHandle(handle, &params)) {
          BOOST_LOG(error) << "Couldn't destroy session handle: "sv << func.nvFBCGetLastErrorStr(handle);
        }

        handle_flags[SESSION_HANDLE] = false;

        return 0;
      }

      ~handle_t() {
        reset();
      }

      std::bitset<MAX_FLAGS> handle_flags;  ///< Handle flags.

      NVFBC_SESSION_HANDLE handle;  ///< NVIDIA FBC capture session handle.
    };

    /**
     * @brief NvFBC display capture backend that produces CUDA frames.
     */
    class display_t: public platf::display_t {
    public:
      /**
       * @brief Initialize NvFBC capture for the selected display.
       *
       * @param display_name Display name.
       * @param config Configuration values to apply.
       * @return 0 on success; nonzero or negative platform status on failure.
       */
      int init(const std::string_view &display_name, const ::video::config_t &config) {
        auto handle = handle_t::make();
        if (!handle) {
          return -1;
        }

        ctx_t ctx {handle->handle};

        auto status_params = handle->status();
        if (!status_params) {
          return -1;
        }

        int streamedMonitor = -1;
        if (!display_name.empty()) {
          if (status_params->bXRandRAvailable) {
            auto monitor_nr = util::from_view(display_name);

            if (monitor_nr < 0 || monitor_nr >= status_params->dwOutputNum) {
              BOOST_LOG(warning) << "Can't stream monitor ["sv << monitor_nr << "], valid output indices are [0, "sv << status_params->dwOutputNum << "), defaulting to virtual desktop"sv;
            } else {
              streamedMonitor = monitor_nr;
            }
          } else {
            BOOST_LOG(warning) << "XrandR not available, streaming entire virtual desktop"sv;
          }
        }

        delay = ::video::capture_frame_interval(config);

        capture_params = NVFBC_CREATE_CAPTURE_SESSION_PARAMS {.dwVersion = NVFBC_CREATE_CAPTURE_SESSION_PARAMS_VER};

        capture_params.eCaptureType = NVFBC_CAPTURE_SHARED_CUDA;
        capture_params.bDisableAutoModesetRecovery = nv_bool(true);

        capture_params.dwSamplingRateMs = 1000 /* ms */ / config.framerate;

        if (streamedMonitor != -1) {
          auto &output = status_params->outputs[streamedMonitor];

          width = output.trackedBox.w;
          height = output.trackedBox.h;
          offset_x = output.trackedBox.x;
          offset_y = output.trackedBox.y;

          capture_params.eTrackingType = NVFBC_TRACKING_OUTPUT;
          capture_params.dwOutputId = output.dwId;
        } else {
          capture_params.eTrackingType = NVFBC_TRACKING_SCREEN;

          width = status_params->screenSize.w;
          height = status_params->screenSize.h;
        }

        env_width = status_params->screenSize.w;
        env_height = status_params->screenSize.h;

        this->handle = std::move(*handle);
        return 0;
      }

      platf::capture_e capture(const push_captured_image_cb_t &push_captured_image_cb, const pull_free_image_cb_t &pull_free_image_cb, bool *cursor) override {
        auto next_frame = std::chrono::steady_clock::now();

        {
          // We must create at least one texture on this thread before calling NvFBCToCudaSetUp()
          // Otherwise it fails with "Unable to register an OpenGL buffer to a CUDA resource (result: 201)" message
          std::shared_ptr<platf::img_t> img_dummy;
          pull_free_image_cb(img_dummy);
        }

        // Force display_t::capture to initialize handle_t::capture
        cursor_visible = !*cursor;

        ctx_t ctx {handle.handle};
        auto fg = util::fail_guard([&]() {
          handle.reset();
        });

        sleep_overshoot_logger.reset();

        while (true) {
          platf::handle_pacing(next_frame, delay, sleep_overshoot_logger);

          std::shared_ptr<platf::img_t> img_out;
          auto status = snapshot(pull_free_image_cb, img_out, 150ms, *cursor);
          switch (status) {
            case platf::capture_e::reinit:
            case platf::capture_e::capture_error:
            case platf::capture_e::interrupted:
              return status;
            case platf::capture_e::timeout:
              if (!push_captured_image_cb(std::move(img_out), false)) {
                return platf::capture_e::ok;
              }
              break;
            case platf::capture_e::ok:
              if (!push_captured_image_cb(std::move(img_out), true)) {
                return platf::capture_e::ok;
              }
              break;
            default:
              BOOST_LOG(error) << "Unrecognized capture status ["sv << std::to_underlying(status) << ']';
              return status;
          }
        }

        return platf::capture_e::ok;
      }

      // Reinitialize the capture session.
      /**
       * @brief Reinitialize NvFBC capture after a recoverable failure.
       *
       * @param cursor Cursor image or visibility state to composite.
       * @return Reinit status.
       */
      platf::capture_e reinit(bool cursor) {
        if (handle.stop()) {
          return platf::capture_e::capture_error;
        }

        cursor_visible = cursor;
        if (cursor) {
          capture_params.bPushModel = nv_bool(false);
          capture_params.bWithCursor = nv_bool(true);
          capture_params.bAllowDirectCapture = nv_bool(false);
        } else {
          capture_params.bPushModel = nv_bool(true);
          capture_params.bWithCursor = nv_bool(false);
          capture_params.bAllowDirectCapture = nv_bool(true);
        }

        if (handle.capture(capture_params)) {
          return platf::capture_e::capture_error;
        }

        // If trying to capture directly, test if it actually does.
        if (capture_params.bAllowDirectCapture) {
          CUdeviceptr device_ptr;
          NVFBC_FRAME_GRAB_INFO grab_info;

          NVFBC_TOCUDA_GRAB_FRAME_PARAMS grab {
            NVFBC_TOCUDA_GRAB_FRAME_PARAMS_VER,
            NVFBC_TOCUDA_GRAB_FLAGS_NOWAIT,
            &device_ptr,
            &grab_info,
            0,
          };

          // Direct Capture may fail the first few times, even if it's possible
          for (int x = 0; x < 3; ++x) {
            if (auto status = func.nvFBCToCudaGrabFrame(handle.handle, &grab)) {
              if (status == NVFBC_ERR_MUST_RECREATE) {
                return platf::capture_e::reinit;
              }

              BOOST_LOG(error) << "Couldn't capture nvFramebuffer: "sv << handle.last_error();

              return platf::capture_e::capture_error;
            }

            if (grab_info.bDirectCapture) {
              break;
            }

            BOOST_LOG(debug) << "Direct capture failed attempt ["sv << x << ']';
          }

          if (!grab_info.bDirectCapture) {
            BOOST_LOG(debug) << "Direct capture failed, trying the extra copy method"sv;
            // Direct capture failed
            capture_params.bPushModel = nv_bool(false);
            capture_params.bWithCursor = nv_bool(false);
            capture_params.bAllowDirectCapture = nv_bool(false);

            if (handle.stop() || handle.capture(capture_params)) {
              return platf::capture_e::capture_error;
            }
          }
        }

        return platf::capture_e::ok;
      }

      /**
       * @brief Capture a display frame into the provided image object.
       *
       * @param pull_free_image_cb Callback that provides an available image buffer.
       * @param img_out Captured CUDA image returned to the streaming pipeline.
       * @param timeout Maximum time to wait for the operation.
       * @param cursor Cursor image or visibility state to composite.
       * @return Capture status reported to the streaming pipeline.
       */
      platf::capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor) {
        if (cursor != cursor_visible) {
          auto status = reinit(cursor);
          if (status != platf::capture_e::ok) {
            return status;
          }
        }

        CUdeviceptr device_ptr;
        NVFBC_FRAME_GRAB_INFO grab_info;

        NVFBC_TOCUDA_GRAB_FRAME_PARAMS grab {
          NVFBC_TOCUDA_GRAB_FRAME_PARAMS_VER,
          NVFBC_TOCUDA_GRAB_FLAGS_NOWAIT,
          &device_ptr,
          &grab_info,
          (std::uint32_t) timeout.count(),
        };

        if (auto status = func.nvFBCToCudaGrabFrame(handle.handle, &grab)) {
          if (status == NVFBC_ERR_MUST_RECREATE) {
            return platf::capture_e::reinit;
          }

          BOOST_LOG(error) << "Couldn't capture nvFramebuffer: "sv << handle.last_error();
          return platf::capture_e::capture_error;
        }

        if (!pull_free_image_cb(img_out)) {
          return platf::capture_e::interrupted;
        }
        auto img = (img_t *) img_out.get();

        if (img->tex.copy((std::uint8_t *) device_ptr, img->height, img->row_pitch)) {
          return platf::capture_e::capture_error;
        }

        return platf::capture_e::ok;
      }

      /**
       * @brief Create AVCodec encode device.
       *
       * @param pix_fmt Sunshine pixel format to convert or allocate for.
       * @return Constructed AVCodec encode device object.
       */
      std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(platf::pix_fmt_e pix_fmt) override {
        return ::cuda::make_avcodec_encode_device(width, height, true);
      }

      /**
       * @brief Allocate an image buffer compatible with this display backend.
       *
       * @return Allocated img object, or null when unavailable.
       */
      std::shared_ptr<platf::img_t> alloc_img() override {
        auto img = std::make_shared<cuda::img_t>();

        img->data = nullptr;
        img->width = width;
        img->height = height;
        img->pixel_pitch = 4;
        img->row_pitch = img->width * img->pixel_pitch;

        auto tex_opt = tex_t::make(height, width * img->pixel_pitch);
        if (!tex_opt) {
          return nullptr;
        }

        img->tex = std::move(*tex_opt);

        return img;
      };

      /**
       * @brief Populate a fallback image when real capture data is unavailable.
       *
       * @return Capture status reported to the streaming pipeline.
       */
      int dummy_img(platf::img_t *) override {
        return 0;
      }

      std::chrono::nanoseconds delay;  ///< Delay before the timer task becomes eligible to run.

      bool cursor_visible;  ///< Whether the cursor should be included in the capture.
      handle_t handle;  ///< NvFBC capture handle owning the active capture session.

      NVFBC_CREATE_CAPTURE_SESSION_PARAMS capture_params;  ///< NvFBC capture-session parameters used for frame grabs.
    };
  }  // namespace nvfbc
}  // namespace cuda

namespace platf {
  /**
   * @brief Create an NvFBC CUDA display capture backend.
   *
   * @param hwdevice_type Hardware device type requested for capture or encode.
   * @param display_name Display name.
   * @param config Configuration values to apply.
   * @return Display backend, or nullptr when NvFBC/CUDA initialization fails.
   */
  std::shared_ptr<display_t> nvfbc_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
    if (hwdevice_type != mem_type_e::cuda) {
      BOOST_LOG(error) << "Could not initialize nvfbc display with the given hw device type"sv;
      return nullptr;
    }

    auto display = std::make_shared<cuda::nvfbc::display_t>();

    if (display->init(display_name, config)) {
      return nullptr;
    }

    return display;
  }

  /**
   * @brief Enumerate display names exposed by NvFBC.
   *
   * @return NvFBC display names, or an empty list when enumeration fails.
   */
  std::vector<std::string> nvfbc_display_names() {
    if (cuda::init() || cuda::nvfbc::init()) {
      return {};
    }

    std::vector<std::string> display_names;

    auto handle = cuda::nvfbc::handle_t::make();
    if (!handle) {
      return {};
    }

    auto status_params = handle->status();
    if (!status_params) {
      return {};
    }

    if (!status_params->bIsCapturePossible) {
      BOOST_LOG(error) << "NVidia driver doesn't support NvFBC screencasting"sv;
    }

    BOOST_LOG(info) << "Found ["sv << status_params->dwOutputNum << "] outputs"sv;
    BOOST_LOG(info) << "Virtual Desktop: "sv << status_params->screenSize.w << 'x' << status_params->screenSize.h;
    BOOST_LOG(info) << "XrandR: "sv << (status_params->bXRandRAvailable ? "available"sv : "unavailable"sv);

    for (auto x = 0; x < status_params->dwOutputNum; ++x) {
      auto &output = status_params->outputs[x];
      BOOST_LOG(info) << "-- Output --"sv;
      BOOST_LOG(debug) << "  ID: "sv << output.dwId;
      BOOST_LOG(debug) << "  Name: "sv << output.name;
      BOOST_LOG(info) << "  Resolution: "sv << output.trackedBox.w << 'x' << output.trackedBox.h;
      BOOST_LOG(info) << "  Offset: "sv << output.trackedBox.x << 'x' << output.trackedBox.y;
      display_names.emplace_back(std::to_string(x));
    }

    if (cuda::nvfbc::should_offer_virtual_desktop(status_params->dwOutputNum, status_params->bIsCapturePossible, status_params->screenSize.w, status_params->screenSize.h)) {
      BOOST_LOG(info) << "No NvFBC outputs, offering the virtual desktop"sv;
      display_names.emplace_back("0");
    }

    return display_names;
  }
}  // namespace platf
