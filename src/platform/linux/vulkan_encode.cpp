/**
 * @file src/platform/linux/vulkan_encode.cpp
 * @brief Vulkan-native encoder: DMA-BUF → Vulkan compute (RGB→NV12) → Vulkan Video encode.
 *        No EGL/GL dependency — all GPU work stays in a single Vulkan queue.
 */
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <vector>
#include <drm_fourcc.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
}

#include <vulkan/vulkan.h>

#include "vulkan_encode.h"
#include "graphics.h"
#include "src/config.h"
#include "src/logging.h"
#include "src/video_colorspace.h"
#include "shaders/rgb2nv12.spv.h"

using namespace std::literals;

namespace vk {

  // Match a DRI render node path to a Vulkan device index via VK_EXT_physical_device_drm.
  // Returns the index as a string (e.g. "1"), or empty string if no match.
  static std::string find_vulkan_index_for_render_node(const char *render_path) {
    struct stat node_stat;
    if (stat(render_path, &node_stat) < 0) return {};

    auto target_major = major(node_stat.st_rdev);
    auto target_minor = minor(node_stat.st_rdev);

    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    VkInstance inst = VK_NULL_HANDLE;
    if (vkCreateInstance(&ci, nullptr, &inst) != VK_SUCCESS) return {};

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(inst, &count, nullptr);
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(inst, &count, devs.data());

    std::string result;
    for (uint32_t i = 0; i < count; i++) {
      VkPhysicalDeviceDrmPropertiesEXT drm = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT};
      VkPhysicalDeviceProperties2 props2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
      props2.pNext = &drm;
      vkGetPhysicalDeviceProperties2(devs[i], &props2);
      if (drm.hasRender && drm.renderMajor == (int64_t)target_major && drm.renderMinor == (int64_t)target_minor) {
        result = std::to_string(i);
        break;
      }
    }
    vkDestroyInstance(inst, nullptr);
    return result;
  }

  static int create_vulkan_hwdevice(AVBufferRef **hw_device_buf) {
    // Resolve render device path to Vulkan device index
    auto render_path = config::video.adapter_name.empty() ? "/dev/dri/renderD128" : config::video.adapter_name;
    if (render_path[0] == '/') {
      auto idx = find_vulkan_index_for_render_node(render_path.c_str());
      if (!idx.empty()) {
        if (av_hwdevice_ctx_create(hw_device_buf, AV_HWDEVICE_TYPE_VULKAN, idx.c_str(), nullptr, 0) >= 0)
          return 0;
      }
    } else {
      // Non-path: treat as device name substring or numeric index
      if (av_hwdevice_ctx_create(hw_device_buf, AV_HWDEVICE_TYPE_VULKAN, render_path.c_str(), nullptr, 0) >= 0)
        return 0;
    }
    // Final fallback: let FFmpeg pick default
    if (av_hwdevice_ctx_create(hw_device_buf, AV_HWDEVICE_TYPE_VULKAN, nullptr, nullptr, 0) >= 0)
      return 0;
    return -1;
  }

  struct PushConstants {
    float color_vec_y[4];
    float color_vec_u[4];
    float color_vec_v[4];
    float range_y[2];
    float range_uv[2];
    int32_t src_offset[2];
    int32_t src_size[2];
    int32_t dst_size[2];
    int32_t cursor_pos[2];
    int32_t cursor_size[2];
  };

  // Helper to check VkResult
  #define VK_CHECK(expr) do { VkResult _r = (expr); if (_r != VK_SUCCESS) { \
    BOOST_LOG(error) << #expr << " failed: " << _r; return -1; } } while(0)
  #define VK_CHECK_BOOL(expr) do { VkResult _r = (expr); if (_r != VK_SUCCESS) { \
    BOOST_LOG(error) << #expr << " failed: " << _r; return false; } } while(0)

  class vk_vram_t: public platf::avcodec_encode_device_t {
  public:
    ~vk_vram_t() {
      cleanup_pipeline();
    }

    int init(int in_width, int in_height, int in_offset_x = 0, int in_offset_y = 0) {
      width = in_width;
      height = in_height;
      offset_x = in_offset_x;
      offset_y = in_offset_y;
      this->data = (void *) init_hw_device;
      return 0;
    }

    int set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx_buf) override {
      this->hwframe.reset(frame);
      this->frame = frame;
      this->hw_frames_ctx = hw_frames_ctx_buf;

      auto *frames_ctx = (AVHWFramesContext *) hw_frames_ctx_buf->data;
      auto *dev_ctx = (AVHWDeviceContext *) frames_ctx->device_ref->data;
      vk_dev_ctx = (AVVulkanDeviceContext *) dev_ctx->hwctx;
      dev = vk_dev_ctx->act_dev;
      phys_dev = vk_dev_ctx->phys_dev;

      {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(phys_dev, &p);
        BOOST_LOG(info) << "Vulkan encode using GPU: " << p.deviceName;
      }

      // Find a compute-capable queue family from FFmpeg's context
      compute_qf = -1;
      for (int i = 0; i < vk_dev_ctx->nb_qf; i++) {
        if (vk_dev_ctx->qf[i].flags & VK_QUEUE_COMPUTE_BIT) {
          compute_qf = vk_dev_ctx->qf[i].idx;
          break;
        }
      }
      if (compute_qf < 0) {
        BOOST_LOG(error) << "No compute queue family in Vulkan device"sv;
        return -1;
      }

      vkGetDeviceQueue(dev, compute_qf, 0, &compute_queue);

      // Load extension functions
      vkGetMemoryFdPropertiesKHR_fn = (PFN_vkGetMemoryFdPropertiesKHR)
        vkGetDeviceProcAddr(dev, "vkGetMemoryFdPropertiesKHR");

      if (!create_compute_pipeline()) return -1;
      if (!create_command_resources()) return -1;

      return 0;
    }

    void apply_colorspace() override {
      auto *colors = video::color_vectors_from_colorspace(colorspace, true);
      if (colors) {
        memcpy(push.color_vec_y, colors->color_vec_y, sizeof(push.color_vec_y));
        memcpy(push.color_vec_u, colors->color_vec_u, sizeof(push.color_vec_u));
        memcpy(push.color_vec_v, colors->color_vec_v, sizeof(push.color_vec_v));
        memcpy(push.range_y, colors->range_y, sizeof(push.range_y));
        memcpy(push.range_uv, colors->range_uv, sizeof(push.range_uv));
      }
    }

    void init_hwframes(AVHWFramesContext *frames) override {
      frames->initial_pool_size = 4;
      auto *vk_frames = (AVVulkanFramesContext *)frames->hwctx;
      vk_frames->tiling = VK_IMAGE_TILING_OPTIMAL;
      vk_frames->usage = (VkImageUsageFlagBits)(
        VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    int convert(platf::img_t &img) override {
      auto &descriptor = (egl::img_descriptor_t &) img;

      // Get encoder target frame
      if (!frame->buf[0]) {
        if (av_hwframe_get_buffer(hw_frames_ctx, frame, 0) < 0) {
          BOOST_LOG(error) << "Failed to get Vulkan frame"sv;
          return -1;
        }
      }

      // Import new DMA-BUF as VkImage when capture sequence changes
      if (descriptor.sequence == 0) {
        // Dummy frame — clear the target
        return 0;
      }

      if (descriptor.sequence > sequence) {
        sequence = descriptor.sequence;
        if (!import_dmabuf(descriptor.sd)) {
          BOOST_LOG(error) << "Failed to import DMA-BUF"sv;
          return -1;
        }
        descriptors_dirty = true;
      }

      if (src.image == VK_NULL_HANDLE) return -1;

      // Setup Y/UV image views for the encoder target (once)
      if (!target_views_created) {
        if (!create_target_views()) return -1;
        target_views_created = true;
        descriptors_dirty = true;
      }

      // Update descriptor set only when source or target changed
      if (descriptors_dirty) {
        update_descriptors();
        descriptors_dirty = false;
      }

      if (descriptor.data && descriptor.serial != cursor_serial) {
        cursor_serial = descriptor.serial;
        if (!create_cursor_image(descriptor.src_w, descriptor.src_h, descriptor.data))
          return -1;
        update_descriptors();
        descriptors_dirty = false;
      }

      // Fill push constants
      push.src_offset[0] = offset_x;
      push.src_offset[1] = offset_y;
      push.src_size[0] = width;
      push.src_size[1] = height;
      push.dst_size[0] = frame->width;
      push.dst_size[1] = frame->height;

      if (descriptor.data) {
        float scale_x = (float)frame->width / width;
        float scale_y = (float)frame->height / height;
        push.cursor_pos[0] = (int32_t)((descriptor.x - offset_x) * scale_x);
        push.cursor_pos[1] = (int32_t)((descriptor.y - offset_y) * scale_y);
        push.cursor_size[0] = (int32_t)(descriptor.width * scale_x);
        push.cursor_size[1] = (int32_t)(descriptor.height * scale_y);
      } else {
        push.cursor_size[0] = 0;
      }

      // Record and submit compute dispatch
      return dispatch_compute();
    }

  private:
    bool create_compute_pipeline() {
      // Shader module
      VkShaderModuleCreateInfo shader_ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      shader_ci.codeSize = rgb2nv12_comp_spv_size;
      shader_ci.pCode = rgb2nv12_comp_spv;
      VK_CHECK_BOOL(vkCreateShaderModule(dev, &shader_ci, nullptr, &shader_module));

      // Descriptor set layout: binding 0=sampler, 1=Y storage, 2=UV storage, 3=cursor sampler
      VkDescriptorSetLayoutBinding bindings[4] = {};
      bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
      bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
      bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
      bindings[3] = {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

      VkDescriptorSetLayoutCreateInfo ds_layout_ci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      ds_layout_ci.bindingCount = 4;
      ds_layout_ci.pBindings = bindings;
      VK_CHECK_BOOL(vkCreateDescriptorSetLayout(dev, &ds_layout_ci, nullptr, &ds_layout));

      // Push constant range
      VkPushConstantRange pc_range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants)};

      VkPipelineLayoutCreateInfo pl_ci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
      pl_ci.setLayoutCount = 1;
      pl_ci.pSetLayouts = &ds_layout;
      pl_ci.pushConstantRangeCount = 1;
      pl_ci.pPushConstantRanges = &pc_range;
      VK_CHECK_BOOL(vkCreatePipelineLayout(dev, &pl_ci, nullptr, &pipeline_layout));

      // Compute pipeline
      VkComputePipelineCreateInfo comp_ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
      comp_ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
      comp_ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
      comp_ci.stage.module = shader_module;
      comp_ci.stage.pName = "main";
      comp_ci.layout = pipeline_layout;
      VK_CHECK_BOOL(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &comp_ci, nullptr, &pipeline));

      // Descriptor pool
      VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
      };
      VkDescriptorPoolCreateInfo pool_ci = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
      pool_ci.maxSets = 1;
      pool_ci.poolSizeCount = 2;
      pool_ci.pPoolSizes = pool_sizes;
      VK_CHECK_BOOL(vkCreateDescriptorPool(dev, &pool_ci, nullptr, &desc_pool));

      VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      alloc_info.descriptorPool = desc_pool;
      alloc_info.descriptorSetCount = 1;
      alloc_info.pSetLayouts = &ds_layout;
      VK_CHECK_BOOL(vkAllocateDescriptorSets(dev, &alloc_info, &desc_set));

      // Sampler for source image
      VkSamplerCreateInfo sampler_ci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
      sampler_ci.magFilter = VK_FILTER_LINEAR;
      sampler_ci.minFilter = VK_FILTER_LINEAR;
      sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      VK_CHECK_BOOL(vkCreateSampler(dev, &sampler_ci, nullptr, &sampler));

      if (!create_cursor_image(1, 1, nullptr)) return false;

      return true;
    }

    bool create_command_resources() {
      VkCommandPoolCreateInfo pool_ci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
      pool_ci.queueFamilyIndex = compute_qf;
      pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
      VK_CHECK_BOOL(vkCreateCommandPool(dev, &pool_ci, nullptr, &cmd_pool));

      VkCommandBufferAllocateInfo alloc_ci = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
      alloc_ci.commandPool = cmd_pool;
      alloc_ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      alloc_ci.commandBufferCount = CMD_RING_SIZE;
      VK_CHECK_BOOL(vkAllocateCommandBuffers(dev, &alloc_ci, cmd_ring));

      return true;
    }

    static VkFormat drm_fourcc_to_vk_format(uint32_t fourcc) {
      switch (fourcc) {
        case DRM_FORMAT_XRGB8888:
        case DRM_FORMAT_ARGB8888: return VK_FORMAT_B8G8R8A8_UNORM;
        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_ABGR8888: return VK_FORMAT_R8G8B8A8_UNORM;
        case DRM_FORMAT_XRGB2101010:
        case DRM_FORMAT_ARGB2101010: return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        case DRM_FORMAT_XBGR2101010:
        case DRM_FORMAT_ABGR2101010: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        default:
          BOOST_LOG(warning) << "Unknown DRM fourcc 0x" << std::hex << fourcc << std::dec << ", assuming B8G8R8A8";
          return VK_FORMAT_B8G8R8A8_UNORM;
      }
    }

    bool import_dmabuf(const egl::surface_descriptor_t &sd) {
      destroy_src_image();

      int fd = dup(sd.fds[0]);
      if (fd < 0) return false;

      // Query memory requirements for this DMA-BUF
      VkMemoryFdPropertiesKHR fd_props = {VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
      if (vkGetMemoryFdPropertiesKHR_fn) {
        vkGetMemoryFdPropertiesKHR_fn(dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fd_props);
      }

      // Create VkImage for the DMA-BUF
      VkExternalMemoryImageCreateInfo ext_ci = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
      ext_ci.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

      VkSubresourceLayout drm_layouts[4] = {};
      VkImageDrmFormatModifierExplicitCreateInfoEXT drm_ci = {
        VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};
      VkImageTiling tiling;

      if (sd.modifier != DRM_FORMAT_MOD_INVALID) {
        int plane_count = 0;
        for (int i = 0; i < 4 && sd.fds[i] >= 0; ++i) {
          drm_layouts[i].offset = sd.offsets[i];
          drm_layouts[i].rowPitch = sd.pitches[i];
          plane_count++;
        }
        drm_ci.drmFormatModifier = sd.modifier;
        drm_ci.drmFormatModifierPlaneCount = plane_count;
        drm_ci.pPlaneLayouts = drm_layouts;
        ext_ci.pNext = &drm_ci;
        tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
      } else {
        tiling = VK_IMAGE_TILING_LINEAR;
      }

      auto vk_format = drm_fourcc_to_vk_format(sd.fourcc);

      VkImageCreateInfo img_ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      img_ci.pNext = &ext_ci;
      img_ci.imageType = VK_IMAGE_TYPE_2D;
      img_ci.format = vk_format;
      img_ci.extent = {(uint32_t)sd.width, (uint32_t)sd.height, 1};
      img_ci.mipLevels = 1;
      img_ci.arrayLayers = 1;
      img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
      img_ci.tiling = tiling;
      img_ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
      img_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

      auto res = vkCreateImage(dev, &img_ci, nullptr, &src.image);
      if (res != VK_SUCCESS) {
        close(fd);
        BOOST_LOG(error) << "vkCreateImage for DMA-BUF failed: " << res
                         << " (modifier=0x" << std::hex << sd.modifier << std::dec
                         << ", pitch=" << sd.pitches[0] << ", offset=" << sd.offsets[0] << ")";
        return false;
      }

      // Bind imported DMA-BUF memory
      VkMemoryRequirements mem_req;
      vkGetImageMemoryRequirements(dev, src.image, &mem_req);

      VkImportMemoryFdInfoKHR import_fd = {VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
      import_fd.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      import_fd.fd = fd;  // Vulkan takes ownership

      VkMemoryAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      alloc_info.pNext = &import_fd;
      alloc_info.allocationSize = mem_req.size;
      alloc_info.memoryTypeIndex = find_memory_type(
        fd_props.memoryTypeBits ? fd_props.memoryTypeBits : mem_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

      VkDeviceMemory src_mem = VK_NULL_HANDLE;
      res = vkAllocateMemory(dev, &alloc_info, nullptr, &src_mem);
      if (res != VK_SUCCESS) {
        BOOST_LOG(error) << "vkAllocateMemory for DMA-BUF failed: " << res;
        vkDestroyImage(dev, src.image, nullptr);
        src.image = VK_NULL_HANDLE;
        return false;
      }

      vkBindImageMemory(dev, src.image, src_mem, 0);

      // Create image view (Vulkan sampling always returns RGBA order regardless of memory layout)
      VkImageViewCreateInfo view_ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view_ci.image = src.image;
      view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view_ci.format = vk_format;
      view_ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      VK_CHECK_BOOL(vkCreateImageView(dev, &view_ci, nullptr, &src.view));

      src.mem = src_mem;
      return true;
    }

    bool create_cursor_image(int w, int h, const uint8_t *pixels) {
      destroy_cursor_image();

      VkImageCreateInfo img_ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      img_ci.imageType = VK_IMAGE_TYPE_2D;
      img_ci.format = VK_FORMAT_B8G8R8A8_UNORM;
      img_ci.extent = {(uint32_t)w, (uint32_t)h, 1};
      img_ci.mipLevels = 1;
      img_ci.arrayLayers = 1;
      img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
      img_ci.tiling = VK_IMAGE_TILING_LINEAR;
      img_ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
      img_ci.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
      VK_CHECK_BOOL(vkCreateImage(dev, &img_ci, nullptr, &cursor.image));

      VkMemoryRequirements mem_req;
      vkGetImageMemoryRequirements(dev, cursor.image, &mem_req);
      VkMemoryAllocateInfo alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      alloc.allocationSize = mem_req.size;
      alloc.memoryTypeIndex = find_memory_type(mem_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      VK_CHECK_BOOL(vkAllocateMemory(dev, &alloc, nullptr, &cursor.mem));
      VK_CHECK_BOOL(vkBindImageMemory(dev, cursor.image, cursor.mem, 0));

      if (pixels) {
        void *mapped;
        VK_CHECK_BOOL(vkMapMemory(dev, cursor.mem, 0, VK_WHOLE_SIZE, 0, &mapped));
        VkImageSubresource subres = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
        VkSubresourceLayout layout;
        vkGetImageSubresourceLayout(dev, cursor.image, &subres, &layout);
        for (int y = 0; y < h; y++)
          memcpy((uint8_t *)mapped + layout.offset + y * layout.rowPitch, pixels + y * w * 4, w * 4);
        vkUnmapMemory(dev, cursor.mem);
      }

      VkImageViewCreateInfo view_ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view_ci.image = cursor.image;
      view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view_ci.format = VK_FORMAT_B8G8R8A8_UNORM;
      view_ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      VK_CHECK_BOOL(vkCreateImageView(dev, &view_ci, nullptr, &cursor.view));

      cursor.needs_transition = true;
      descriptors_dirty = true;
      return true;
    }

    void destroy_cursor_image() {
      if (cursor.view) { vkDestroyImageView(dev, cursor.view, nullptr); cursor.view = VK_NULL_HANDLE; }
      if (cursor.image) { vkDestroyImage(dev, cursor.image, nullptr); cursor.image = VK_NULL_HANDLE; }
      if (cursor.mem) { vkFreeMemory(dev, cursor.mem, nullptr); cursor.mem = VK_NULL_HANDLE; }
    }

    bool create_target_views() {
      AVVkFrame *vk_frame = (AVVkFrame *) frame->data[0];
      if (!vk_frame) return false;

      // Detect multiplane vs multi-image layout
      int num_imgs = 0;
      for (int i = 0; i < AV_NUM_DATA_POINTERS && vk_frame->img[i]; i++) num_imgs++;

      if (num_imgs == 1) {
        // Single multiplane image — create plane views
        VkImageViewCreateInfo view_ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_ci.image = vk_frame->img[0];
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;

        // Y plane
        view_ci.format = VK_FORMAT_R8_UNORM;
        view_ci.subresourceRange = {VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 1, 0, 1};
        VK_CHECK_BOOL(vkCreateImageView(dev, &view_ci, nullptr, &y_view));

        // UV plane
        view_ci.format = VK_FORMAT_R8G8_UNORM;
        view_ci.subresourceRange = {VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 1, 0, 1};
        VK_CHECK_BOOL(vkCreateImageView(dev, &view_ci, nullptr, &uv_view));
      } else {
        // Separate images per plane
        VkImageViewCreateInfo view_ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        view_ci.image = vk_frame->img[0];
        view_ci.format = VK_FORMAT_R8_UNORM;
        VK_CHECK_BOOL(vkCreateImageView(dev, &view_ci, nullptr, &y_view));

        view_ci.image = vk_frame->img[1];
        view_ci.format = VK_FORMAT_R8G8_UNORM;
        VK_CHECK_BOOL(vkCreateImageView(dev, &view_ci, nullptr, &uv_view));
      }
      return true;
    }

    void update_descriptors() {
      VkDescriptorImageInfo src_info = {sampler, src.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      VkDescriptorImageInfo y_info = {VK_NULL_HANDLE, y_view, VK_IMAGE_LAYOUT_GENERAL};
      VkDescriptorImageInfo uv_info = {VK_NULL_HANDLE, uv_view, VK_IMAGE_LAYOUT_GENERAL};
      VkDescriptorImageInfo cursor_info = {sampler, cursor.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

      VkWriteDescriptorSet writes[4] = {};
      writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, desc_set, 0, 0, 1,
                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &src_info, nullptr, nullptr};
      writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, desc_set, 1, 0, 1,
                   VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &y_info, nullptr, nullptr};
      writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, desc_set, 2, 0, 1,
                   VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &uv_info, nullptr, nullptr};
      writes[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, desc_set, 3, 0, 1,
                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &cursor_info, nullptr, nullptr};
      vkUpdateDescriptorSets(dev, 4, writes, 0, nullptr);
    }

    int dispatch_compute() {
      AVVkFrame *vk_frame = (AVVkFrame *) frame->data[0];
      int num_imgs = 0;
      for (int i = 0; i < AV_NUM_DATA_POINTERS && vk_frame->img[i]; i++) num_imgs++;

      // Rotate to next command buffer. With CMD_RING_SIZE slots, the buffer
      // we're about to reuse was submitted CMD_RING_SIZE frames ago.
      // At 60fps that's ~50ms for a <1ms compute dispatch — always complete.
      // No fences, no semaphore waits, no CPU blocking.
      auto cmd_buf = cmd_ring[cmd_ring_idx];
      cmd_ring_idx = (cmd_ring_idx + 1) % CMD_RING_SIZE;

      VkCommandBufferBeginInfo begin_ci = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      VK_CHECK(vkBeginCommandBuffer(cmd_buf, &begin_ci));

      // Transition source image to SHADER_READ_ONLY
      VkImageMemoryBarrier src_barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      src_barrier.srcAccessMask = 0;
      src_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      src_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      src_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      src_barrier.image = src.image;
      src_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      src_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
      src_barrier.dstQueueFamilyIndex = compute_qf;

      vkCmdPipelineBarrier(cmd_buf,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &src_barrier);

      // Transition cursor image if needed
      if (cursor.needs_transition) {
        VkImageMemoryBarrier cursor_barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        cursor_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        cursor_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        cursor_barrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
        cursor_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cursor_barrier.image = cursor.image;
        cursor_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        cursor_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        cursor_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vkCmdPipelineBarrier(cmd_buf,
          VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          0, 0, nullptr, 0, nullptr, 1, &cursor_barrier);
        cursor.needs_transition = false;
      }

      // Transition target planes to GENERAL for storage writes
      VkImageMemoryBarrier dst_barriers[2] = {};
      int num_dst_barriers = (num_imgs == 1) ? 1 : 2;
      for (int i = 0; i < num_dst_barriers; i++) {
        dst_barriers[i] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        dst_barriers[i].srcAccessMask = target_initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
        dst_barriers[i].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        dst_barriers[i].oldLayout = target_initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
        dst_barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        dst_barriers[i].image = vk_frame->img[num_imgs == 1 ? 0 : i];
        dst_barriers[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        dst_barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dst_barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      }

      vkCmdPipelineBarrier(cmd_buf,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, num_dst_barriers, dst_barriers);

      // Bind pipeline and dispatch
      vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
      vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline_layout, 0, 1, &desc_set, 0, nullptr);
      vkCmdPushConstants(cmd_buf, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(PushConstants), &push);

      uint32_t gx = (frame->width + 15) / 16;
      uint32_t gy = (frame->height + 15) / 16;
      vkCmdDispatch(cmd_buf, gx, gy, 1);

      VK_CHECK(vkEndCommandBuffer(cmd_buf));

      // Submit with timeline semaphore signaling for FFmpeg
      VkTimelineSemaphoreSubmitInfo timeline_info = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
      VkSemaphore wait_sems[AV_NUM_DATA_POINTERS], signal_sems[AV_NUM_DATA_POINTERS];
      uint64_t wait_vals[AV_NUM_DATA_POINTERS], signal_vals[AV_NUM_DATA_POINTERS];
      VkPipelineStageFlags wait_stages[AV_NUM_DATA_POINTERS];
      int sem_count = 0;

      for (int i = 0; i < AV_NUM_DATA_POINTERS && vk_frame->sem[i]; i++) {
        wait_sems[sem_count] = vk_frame->sem[i];
        wait_vals[sem_count] = vk_frame->sem_value[i];
        wait_stages[sem_count] = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

        signal_sems[sem_count] = vk_frame->sem[i];
        signal_vals[sem_count] = vk_frame->sem_value[i] + 1;
        vk_frame->sem_value[i]++;
        sem_count++;
      }

      timeline_info.waitSemaphoreValueCount = sem_count;
      timeline_info.pWaitSemaphoreValues = wait_vals;
      timeline_info.signalSemaphoreValueCount = sem_count;
      timeline_info.pSignalSemaphoreValues = signal_vals;

      VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
      submit.pNext = &timeline_info;
      submit.waitSemaphoreCount = sem_count;
      submit.pWaitSemaphores = wait_sems;
      submit.pWaitDstStageMask = wait_stages;
      submit.commandBufferCount = 1;
      submit.pCommandBuffers = &cmd_buf;
      submit.signalSemaphoreCount = sem_count;
      submit.pSignalSemaphores = signal_sems;

      // Lock the queue (FFmpeg requires this)
      vk_dev_ctx->lock_queue(
        (AVHWDeviceContext *)((AVHWFramesContext *)hw_frames_ctx->data)->device_ref->data,
        compute_qf, 0);
      auto res = vkQueueSubmit(compute_queue, 1, &submit, VK_NULL_HANDLE);
      vk_dev_ctx->unlock_queue(
        (AVHWDeviceContext *)((AVHWFramesContext *)hw_frames_ctx->data)->device_ref->data,
        compute_qf, 0);

      if (res != VK_SUCCESS) {
        BOOST_LOG(error) << "vkQueueSubmit failed: " << res;
        return -1;
      }

      // Update frame layouts for FFmpeg
      for (int i = 0; i < AV_NUM_DATA_POINTERS && vk_frame->img[i]; i++) {
        vk_frame->layout[i] = VK_IMAGE_LAYOUT_GENERAL;
        vk_frame->access[i] = (VkAccessFlagBits)VK_ACCESS_SHADER_WRITE_BIT;
      }

      target_initialized = true;

      return 0;
    }

    uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags props) {
      VkPhysicalDeviceMemoryProperties mem_props;
      vkGetPhysicalDeviceMemoryProperties(phys_dev, &mem_props);
      for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_bits & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & props) == props)
          return i;
      }
      // Fallback: any matching type bit
      for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if (type_bits & (1 << i)) return i;
      }
      return 0;
    }

    void destroy_src_image() {
      if (src.image) {
        // Defer destruction — the GPU may still be using this image.
        // By the time we wrap around (4 frames later), it's guaranteed done.
        auto &slot = defer_ring[defer_idx];
        if (slot.view) vkDestroyImageView(dev, slot.view, nullptr);
        if (slot.image) vkDestroyImage(dev, slot.image, nullptr);
        if (slot.mem) vkFreeMemory(dev, slot.mem, nullptr);
        slot = src;
        defer_idx = (defer_idx + 1) % DEFER_RING_SIZE;
      }
      src = {};
    }

    void cleanup_pipeline() {
      if (!dev) return;
      vkDeviceWaitIdle(dev);
      destroy_src_image();
      // Flush deferred destroys
      for (auto &slot : defer_ring) {
        if (slot.view) vkDestroyImageView(dev, slot.view, nullptr);
        if (slot.image) vkDestroyImage(dev, slot.image, nullptr);
        if (slot.mem) vkFreeMemory(dev, slot.mem, nullptr);
        slot = {};
      }
      if (y_view) vkDestroyImageView(dev, y_view, nullptr);
      if (uv_view) vkDestroyImageView(dev, uv_view, nullptr);
      destroy_cursor_image();
      if (cmd_pool) vkDestroyCommandPool(dev, cmd_pool, nullptr);
      if (sampler) vkDestroySampler(dev, sampler, nullptr);
      if (desc_pool) vkDestroyDescriptorPool(dev, desc_pool, nullptr);
      if (pipeline) vkDestroyPipeline(dev, pipeline, nullptr);
      if (pipeline_layout) vkDestroyPipelineLayout(dev, pipeline_layout, nullptr);
      if (ds_layout) vkDestroyDescriptorSetLayout(dev, ds_layout, nullptr);
      if (shader_module) vkDestroyShaderModule(dev, shader_module, nullptr);
    }

    static int init_hw_device(platf::avcodec_encode_device_t *, AVBufferRef **hw_device_buf) {
      return create_vulkan_hwdevice(hw_device_buf);
    }

    // Dimensions
    int width = 0, height = 0;
    int offset_x = 0, offset_y = 0;
    AVBufferRef *hw_frames_ctx = nullptr;
    frame_t hwframe;
    std::uint64_t sequence = 0;

    // Vulkan device (from FFmpeg)
    VkDevice dev = VK_NULL_HANDLE;
    VkPhysicalDevice phys_dev = VK_NULL_HANDLE;
    AVVulkanDeviceContext *vk_dev_ctx = nullptr;
    int compute_qf = -1;
    VkQueue compute_queue = VK_NULL_HANDLE;

    // Compute pipeline
    VkShaderModule shader_module = VK_NULL_HANDLE;
    VkDescriptorSetLayout ds_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    VkDescriptorSet desc_set = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    // Command submission — ring of buffers to avoid reuse while in-flight.
    // No CPU waits: by the time we wrap around, the old submission is long done.
    static constexpr int CMD_RING_SIZE = 3;
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ring[CMD_RING_SIZE] = {};
    int cmd_ring_idx = 0;

    // Source DMA-BUF image with deferred destruction
    struct src_image_t {
      VkImage image = VK_NULL_HANDLE;
      VkDeviceMemory mem = VK_NULL_HANDLE;
      VkImageView view = VK_NULL_HANDLE;
    };
    src_image_t src = {};
    static constexpr int DEFER_RING_SIZE = 4;
    src_image_t defer_ring[DEFER_RING_SIZE] = {};
    int defer_idx = 0;

    // Target NV12 plane views
    VkImageView y_view = VK_NULL_HANDLE;
    VkImageView uv_view = VK_NULL_HANDLE;
    bool target_views_created = false;
    bool target_initialized = false;
    bool descriptors_dirty = false;

    // Cursor image
    struct {
      VkImage image = VK_NULL_HANDLE;
      VkDeviceMemory mem = VK_NULL_HANDLE;
      VkImageView view = VK_NULL_HANDLE;
      bool needs_transition = false;
    } cursor = {};
    unsigned long cursor_serial = 0;

    // Push constants (color matrix)
    PushConstants push = {};

    PFN_vkGetMemoryFdPropertiesKHR vkGetMemoryFdPropertiesKHR_fn = nullptr;
  };

  // Free functions

  int vulkan_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *, AVBufferRef **hw_device_buf) {
    return create_vulkan_hwdevice(hw_device_buf);
  }

  bool validate() {
    if (!avcodec_find_encoder_by_name("h264_vulkan") && !avcodec_find_encoder_by_name("hevc_vulkan"))
      return false;
    AVBufferRef *dev = nullptr;
    if (create_vulkan_hwdevice(&dev) < 0)
      return false;
    av_buffer_unref(&dev);
    return true;
  }

  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device_vram(int w, int h, int offset_x, int offset_y) {
    auto dev = std::make_unique<vk_vram_t>();
    if (dev->init(w, h, offset_x, offset_y) < 0) return nullptr;
    return dev;
  }

  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device_ram(int, int) {
    return nullptr;
  }

}  // namespace vk
