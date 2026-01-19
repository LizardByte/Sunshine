/**
 * @file src/platform/linux/vulkan_encode.cpp
 * @brief FFmpeg Vulkan encoder with zero-copy DMA-BUF import.
 */

#include <fcntl.h>
#include <drm_fourcc.h>
#include <gbm.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
}

#include <vulkan/vulkan.h>

#include "vulkan_encode.h"
#include "graphics.h"
#include "src/logging.h"
#include "misc.h"

using namespace std::literals;

namespace vk {

  class vk_vram_t: public platf::avcodec_encode_device_t {
  public:
    ~vk_vram_t() = default;

    int init(int in_width, int in_height, file_t &&render_device) {
      file = std::move(render_device);
      width = in_width;
      height = in_height;

      if (!gbm::create_device) {
        BOOST_LOG(warning) << "libgbm not initialized"sv;
        return -1;
      }

      this->data = (void *) vulkan_init_avcodec_hardware_input_buffer;

      gbm.reset(gbm::create_device(file.el));
      if (!gbm) {
        BOOST_LOG(error) << "Couldn't create GBM device"sv;
        return -1;
      }

      display = egl::make_display(gbm.get());
      if (!display) return -1;

      auto ctx_opt = egl::make_ctx(display.get());
      if (!ctx_opt) return -1;
      ctx = std::move(*ctx_opt);

      return 0;
    }

    int set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx_buf) override {
      this->hwframe.reset(frame);
      this->frame = frame;
      this->hw_frames_ctx = hw_frames_ctx_buf;

      auto *frames_ctx = (AVHWFramesContext *) hw_frames_ctx_buf->data;
      auto *dev_ctx = (AVHWDeviceContext *) frames_ctx->device_ref->data;
      vk_dev_ctx = (AVVulkanDeviceContext *) dev_ctx->hwctx;
      vk_dev = vk_dev_ctx->act_dev;

      // Load Vulkan extension functions
      if (!vkGetMemoryFdKHR_fn) {
        vkGetMemoryFdKHR_fn = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(vk_dev, "vkGetMemoryFdKHR");
      }

      // Create sws for RGB->NV12 conversion with scaling from capture to encode resolution
      BOOST_LOG(info) << "Vulkan sws: capture " << width << "x" << height << " -> encode " << frame->width << "x" << frame->height;
      auto sws_opt = egl::sws_t::make(width, height, frame->width, frame->height, frames_ctx->sw_format);
      if (!sws_opt) return -1;
      sws = std::move(*sws_opt);

      return 0;
    }

    void apply_colorspace() override {}
    
    void init_hwframes(AVHWFramesContext *frames) override {
      frames->initial_pool_size = 4;
      
      // Request linear tiling for simpler interop
      auto *vk_frames = (AVVulkanFramesContext *)frames->hwctx;
      vk_frames->tiling = VK_IMAGE_TILING_LINEAR;
      vk_frames->usage = (VkImageUsageFlagBits)(VK_IMAGE_USAGE_TRANSFER_DST_BIT | 
                                                 VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    int convert(platf::img_t &img) override {
      auto &descriptor = (egl::img_descriptor_t &) img;

      // Get Vulkan frame
      if (!frame->buf[0]) {
        if (av_hwframe_get_buffer(hw_frames_ctx, frame, 0) < 0) {
          BOOST_LOG(error) << "Failed to get Vulkan frame"sv;
          return -1;
        }
      }

      // Import source RGB texture
      if (descriptor.sequence == 0) {
        rgb = egl::create_blank(img);
      } else if (descriptor.sequence > sequence) {
        sequence = descriptor.sequence;
        rgb = egl::rgb_t {};
        auto rgb_opt = egl::import_source(display.get(), descriptor.sd);
        if (!rgb_opt) return -1;
        rgb = std::move(*rgb_opt);
      }

      // Setup Vulkan→EGL zero-copy interop if needed
      if (!nv12_imported) {
        if (!setup_vulkan_egl_interop()) {
          BOOST_LOG(error) << "Failed to setup Vulkan-EGL interop"sv;
          return -1;
        }
        nv12_imported = true;
      }

      // Render RGB→NV12 directly into Vulkan memory via EGL (zero-copy)
      sws.load_vram(descriptor, 0, 0, rgb->tex[0]);
      sws.convert(nv12->buf);
      gl::ctx.Finish();  // Ensure EGL rendering completes before Vulkan encoder reads

      return 0;
    }

  private:
    bool setup_vulkan_egl_interop() {
      if (!vkGetMemoryFdKHR_fn) {
        BOOST_LOG(warning) << "vkGetMemoryFdKHR not available"sv;
        return false;
      }

      AVVkFrame *vk_frame = (AVVkFrame *) frame->data[0];
      if (!vk_frame) {
        BOOST_LOG(warning) << "No Vulkan frame"sv;
        return false;
      }

      // Export the first memory object
      VkMemoryGetFdInfoKHR fd_info = {};
      fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
      fd_info.memory = vk_frame->mem[0];
      fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

      int fd = -1;
      VkResult res = vkGetMemoryFdKHR_fn(vk_dev, &fd_info, &fd);
      if (res != VK_SUCCESS || fd < 0) {
        BOOST_LOG(warning) << "vkGetMemoryFdKHR failed: " << res;
        return false;
      }

      std::array<file_t, 4> fds;
      fds[0].el = fd;
      fds[1].el = dup(fd);  // Both planes use same memory

      egl::surface_descriptor_t sds[2] = {};

      // Count images and memories for multiplane detection
      int num_imgs = 0, num_mems = 0;
      for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
        if (vk_frame->img[i]) num_imgs++;
        if (vk_frame->mem[i]) num_mems++;
      }
      bool multiplane_single_image = (num_imgs == 1 && num_mems == 1);
      
      for (int i = 0; i < 2; i++) {
        auto &sd = sds[i];
        sd.fourcc = (i == 0) ? DRM_FORMAT_R8 : DRM_FORMAT_GR88;
        sd.width = frame->width >> (i ? 1 : 0);
        sd.height = frame->height >> (i ? 1 : 0);
        sd.modifier = DRM_FORMAT_MOD_LINEAR;
        sd.fds[0] = fds[i].el;
        sd.fds[1] = sd.fds[2] = sd.fds[3] = -1;
        
        VkImageSubresource subres = {};
        if (multiplane_single_image) {
          subres.aspectMask = (i == 0) ? VK_IMAGE_ASPECT_PLANE_0_BIT : VK_IMAGE_ASPECT_PLANE_1_BIT;
        } else {
          subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }
        
        VkSubresourceLayout layout;
        vkGetImageSubresourceLayout(vk_dev, vk_frame->img[multiplane_single_image ? 0 : i], &subres, &layout);
        sd.pitches[0] = layout.rowPitch;
        sd.offsets[0] = layout.offset;
      }

      // Import into EGL
      auto nv12_opt = egl::import_target(display.get(), std::move(fds), sds[0], sds[1]);
      if (!nv12_opt) {
        BOOST_LOG(warning) << "Failed to import Vulkan frame into EGL"sv;
        return false;
      }
      nv12 = std::move(*nv12_opt);
      return true;
    }

    static int vulkan_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *, AVBufferRef **hw_device_buf) {
      return av_hwdevice_ctx_create(hw_device_buf, AV_HWDEVICE_TYPE_VULKAN, nullptr, nullptr, 0);
    }

    int width = 0, height = 0;
    AVBufferRef *hw_frames_ctx = nullptr;
    frame_t hwframe;

    file_t file;
    gbm::gbm_t gbm;
    egl::display_t display;
    egl::ctx_t ctx;
    egl::sws_t sws;
    egl::nv12_t nv12;
    egl::rgb_t rgb;
    std::uint64_t sequence = 0;
    bool nv12_imported = false;

    // Vulkan device state (from FFmpeg)
    VkDevice vk_dev = VK_NULL_HANDLE;
    AVVulkanDeviceContext *vk_dev_ctx = nullptr;
    
    PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR_fn = nullptr;
  };

  int vulkan_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *, AVBufferRef **hw_device_buf) {
    // Try render device path first, then fallback to device indices
    if (av_hwdevice_ctx_create(hw_device_buf, AV_HWDEVICE_TYPE_VULKAN, "/dev/dri/renderD128", nullptr, 0) >= 0) {
      return 0;
    }

    // Fallback: try device indices for multi-GPU systems
    const char *devices[] = {"1", "0", "2", "3", nullptr};
    for (int i = 0; devices[i]; i++) {
      if (av_hwdevice_ctx_create(hw_device_buf, AV_HWDEVICE_TYPE_VULKAN, devices[i], nullptr, 0) >= 0) {
        return 0;
      }
    }
    return -1;
  }

  bool validate() {
    if (!avcodec_find_encoder_by_name("h264_vulkan") && !avcodec_find_encoder_by_name("hevc_vulkan"))
      return false;
    AVBufferRef *dev = nullptr;
    if (av_hwdevice_ctx_create(&dev, AV_HWDEVICE_TYPE_VULKAN, nullptr, nullptr, 0) < 0)
      return false;
    av_buffer_unref(&dev);
    BOOST_LOG(info) << "Vulkan Video encoding available"sv;
    return true;
  }

  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device_vram(int w, int h, int, int) {
    file_t file = open("/dev/dri/renderD128", O_RDWR);
    if (file.el < 0) {
      BOOST_LOG(error) << "Failed to open render device"sv;
      return nullptr;
    }
    auto dev = std::make_unique<vk_vram_t>();
    if (dev->init(w, h, std::move(file)) < 0) return nullptr;
    return dev;
  }

  std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device_ram(int, int) {
    return nullptr;
  }

}  // namespace vk
