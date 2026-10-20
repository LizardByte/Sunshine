/**
 * @file tests/unit/platform/linux/test_wayland_shm_protocol.cpp
 * @brief Exercise shared-memory capture over a private Wayland connection.
 */
#ifdef SUNSHINE_BUILD_WAYLAND
  // test includes
  #include "../../../tests_common.h"

  // standard includes
  #include <array>
  #include <cstring>
  #include <memory>
  #include <string_view>
  #include <vector>

// ffmpeg includes
extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavutil/opt.h>
  #include <libswscale/swscale.h>
}

  // system includes
  #include <drm_fourcc.h>
  #include <poll.h>
  #include <sys/socket.h>
  #include <wayland-server.h>

  // local includes
  #include <src/config.h>
  #include <src/platform/linux/wayland.h>

namespace {
  /**
   * @brief Private compositor using libwayland's real SHM pool implementation.
   */
  class WaylandShmProtocolTest: public testing::Test {  // NOSONAR(cpp:S3656): TEST_F generates subclasses that access the shared fixture state.
  protected:
    /**
     * @brief Observe the active proxy without replacing capture's protocol handling.
     */
    struct capture_t: wl::dmabuf_t {
      zwlr_screencopy_frame_v1 *request {};  ///< Last frame advertised by the compositor.
    };

    /**
     * @brief Create an in-process compositor without connecting to the desktop.
     */
    void SetUp() override {
      connection.server = wl_display_create();
      ASSERT_NE(connection.server, nullptr);
      ASSERT_EQ(wl_display_init_shm(connection.server), 0);
      ASSERT_NE(wl_global_create(connection.server, &zwlr_screencopy_manager_v1_interface, 3, this, bind_manager), nullptr);
      ASSERT_NE(wl_global_create(connection.server, &zwp_linux_dmabuf_v1_interface, 3, this, bind_dmabuf), nullptr);
      ASSERT_NE(wl_global_create(connection.server, &wl_output_interface, 1, this, bind_output), nullptr);
      std::array<int, 2> sockets;
      ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets.data()), 0);
      ASSERT_NE(wl_client_create(connection.server, sockets[0]), nullptr);
      connection.client = wl_display_connect_to_fd(sockets[1]);
      ASSERT_NE(connection.client, nullptr);
      connection.registry = wl_display_get_registry(connection.client);
      static const wl_registry_listener listener {global, global_remove};
      wl_registry_add_listener(connection.registry, &listener, this);
      ASSERT_TRUE(sync());
      ASSERT_TRUE(sync());
      ASSERT_NE(connection.shm, nullptr);
      ASSERT_NE(connection.manager, nullptr);
      ASSERT_NE(connection.dmabuf, nullptr);
      ASSERT_NE(connection.output, nullptr);
      capture = std::make_unique<capture_t>();
      capture->listener.buffer = [](void *data, zwlr_screencopy_frame_v1 *frame, std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t stride) {  // NOSONAR(cpp:S5008): Required by the Wayland C listener signature.
        auto &capture = *static_cast<capture_t *>(static_cast<wl::dmabuf_t *>(data));
        capture.request = frame;
        capture.buffer(frame, format, width, height, stride);
      };
    }

    /**
     * @brief Release capture objects before disconnecting the private client.
     */
    void TearDown() override {
      if (capture) {
        capture->cancel();
        capture.reset();
      }
      if (connection.client) {
        wl_display_disconnect(connection.client);
      }
      if (connection.server) {
        wl_display_destroy_clients(connection.server);
        wl_display_destroy(connection.server);
      }
    }

    /**
     * @brief Pump both ends until a real Wayland synchronization callback arrives.
     * @return True when the callback completes without a protocol error or timeout.
     */
    bool sync() {
      bool done = false;
      auto callback = wl_display_sync(connection.client);
      static const wl_callback_listener listener {
        [](void *data, wl_callback *callback, std::uint32_t) {  // NOSONAR(cpp:S5008): Required by the Wayland C listener signature.
          *static_cast<bool *>(data) = true;
          wl_callback_destroy(callback);
        },
      };
      wl_callback_add_listener(callback, &listener, &done);
      while (!done) {
        if (wl_display_flush(connection.client) < 0 || wl_event_loop_dispatch(wl_display_get_event_loop(connection.server), 0) < 0) {
          return false;
        }
        wl_display_flush_clients(connection.server);
        pollfd fd {wl_display_get_fd(connection.client), POLLIN, 0};
        if (poll(&fd, 1, 1000) <= 0 || wl_display_dispatch(connection.client) < 0) {
          return false;
        }
      }
      return wl_display_get_error(connection.client) == 0;
    }

    /**
     * @brief Begin a capture and process its advertised buffer formats.
     */
    void start() {
      capture->listen(connection.manager, connection.dmabuf, nullptr, connection.shm, connection.output);
      ASSERT_TRUE(sync());
      ASSERT_TRUE(sync());
    }

    /**
     * @brief Inject successful DMA-BUF creation followed by a terminal copy failure.
     * @param fallback Whether the capture has a SHM factory for recovery.
     */
    void fail_dmabuf_copy(bool fallback = true) {
      hold_buffer_done = true;
      advertise_dmabuf = true;
      accept_dmabuf = true;
      capture->with_damage = true;
      capture->listen(connection.manager, connection.dmabuf, nullptr, fallback ? connection.shm : nullptr, connection.output, true);
      ASSERT_TRUE(sync());
      ASSERT_TRUE(sync());
      hold_buffer_done = false;
      auto params = zwp_linux_dmabuf_v1_create_params(connection.dmabuf);
      static const zwp_linux_buffer_params_v1_listener listener {
        wl::dmabuf_t::buffer_params_created,
        wl::dmabuf_t::buffer_params_failed,
      };
      zwp_linux_buffer_params_v1_add_listener(params, &listener, capture->request);
      zwp_linux_buffer_params_v1_create(params, width, height, DRM_FORMAT_XRGB8888, 0);
      for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(sync());
      }
    }

    /**
     * @brief Complete a pending copy through the real shared-memory buffer.
     */
    void complete() {
      ASSERT_NE(frame_resource, nullptr);
      ASSERT_NE(buffer_resource, nullptr);
      auto buffer = wl_shm_buffer_get(buffer_resource);
      ASSERT_NE(buffer, nullptr);
      wl_shm_buffer_begin_access(buffer);
      auto pixels = static_cast<std::uint8_t *>(wl_shm_buffer_get_data(buffer));
      for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
          const std::array<std::uint8_t, 4> pixel {static_cast<std::uint8_t>(10 + y), 20, 30, 255};
          std::memcpy(pixels + y * stride + x * 4, pixel.data(), pixel.size());
        }
      }
      wl_shm_buffer_end_access(buffer);
      wl_resource_post_event(frame_resource, 1, inverted ? ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT : 0);
      wl_resource_post_event(frame_resource, 2, 0u, 1u, 0u);
      ASSERT_TRUE(sync());
    }

    /**
     * @brief Bind the globals advertised by the private compositor.
     * @param data Test fixture.
     * @param registry Client registry.
     * @param name Global object identifier.
     * @param interface Advertised interface name.
     * @param version Advertised interface version.
     */
    static void global(void *data, wl_registry *registry, std::uint32_t name, const char *interface, std::uint32_t version) {  // NOSONAR(cpp:S5008): Required by the Wayland C registry listener signature.
      auto &self = *static_cast<WaylandShmProtocolTest *>(data);
      if (std::string_view(interface) == "wl_shm") {
        self.connection.shm = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
        static const wl_shm_listener listener {[](void *, wl_shm *, std::uint32_t) {  // NOSONAR(cpp:S5008): Required by the Wayland C SHM listener signature.
          // The private compositor always advertises XRGB8888 to capture.
        }};
        wl_shm_add_listener(self.connection.shm, &listener, nullptr);
      } else if (std::string_view(interface) == "wl_output") {
        self.connection.output = static_cast<wl_output *>(wl_registry_bind(registry, name, &wl_output_interface, 1));
      } else if (std::string_view(interface) == "zwlr_screencopy_manager_v1") {
        self.connection.manager = static_cast<zwlr_screencopy_manager_v1 *>(wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, version));
      } else if (std::string_view(interface) == "zwp_linux_dmabuf_v1") {
        self.connection.dmabuf = static_cast<zwp_linux_dmabuf_v1 *>(wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, version));
      }
    }

    /**
     * @brief Ignore global removal; this compositor retains globals until teardown.
     */
    static void global_remove(void *, wl_registry *, std::uint32_t) {  // NOSONAR(cpp:S5008): Required by the Wayland C registry listener signature.
      // The private compositor retains all globals until teardown.
    }

    /**
     * @brief Bind an output used only as the screencopy request's target.
     */
    static void bind_output(wl_client *client, void *, std::uint32_t version, std::uint32_t id) {  // NOSONAR(cpp:S5008): Required by the Wayland C global-bind signature.
      wl_resource_create(client, &wl_output_interface, version, id);
    }

    /**
     * @brief Bind the screencopy manager and install its request dispatcher.
     */
    static void bind_manager(wl_client *client, void *data, std::uint32_t version, std::uint32_t id) {  // NOSONAR(cpp:S5008): Required by the Wayland C global-bind signature.
      auto resource = wl_resource_create(client, &zwlr_screencopy_manager_v1_interface, version, id);
      wl_resource_set_dispatcher(resource, dispatch_manager, nullptr, data, nullptr);
    }

    /**
     * @brief Bind the controlled DMA-BUF import factory.
     */
    static void bind_dmabuf(wl_client *client, void *data, std::uint32_t version, std::uint32_t id) {  // NOSONAR(cpp:S5008): Required by the Wayland C global-bind signature.
      auto resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
      wl_resource_set_dispatcher(resource, dispatch_dmabuf, nullptr, data, nullptr);
    }

    /**
     * @brief Advertise a SHM frame, optionally holding buffer_done for an import rejection.
     */
    static int dispatch_manager(const void *, void *target, std::uint32_t opcode, const wl_message *, wl_argument *args) {  // NOSONAR(cpp:S5008): Required by wl_dispatcher_func_t.
      auto resource = static_cast<wl_resource *>(target);
      auto &self = *static_cast<WaylandShmProtocolTest *>(wl_resource_get_user_data(resource));
      if (opcode == 2) {
        wl_resource_destroy(resource);
        return 0;
      }
      ++self.requests;
      self.blend_cursor = args[1].i;
      self.frame_resource = wl_resource_create(wl_resource_get_client(resource), &zwlr_screencopy_frame_v1_interface, 3, args[0].n);
      wl_resource_set_dispatcher(self.frame_resource, dispatch_frame, nullptr, &self, nullptr);
      wl_resource_post_event(self.frame_resource, 0, WL_SHM_FORMAT_XRGB8888, self.width, self.height, self.stride);
      if (self.advertise_dmabuf) {
        wl_resource_post_event(self.frame_resource, 5, DRM_FORMAT_XRGB8888, self.width, self.height);
      }
      if (!self.hold_buffer_done) {
        wl_resource_post_event(self.frame_resource, 6);
      }
      return 0;
    }

    /**
     * @brief Retain copy buffers until the test completes or cancels a request.
     */
    static int dispatch_frame(const void *, void *target, std::uint32_t opcode, const wl_message *, wl_argument *args) {  // NOSONAR(cpp:S5008): Required by wl_dispatcher_func_t.
      auto resource = static_cast<wl_resource *>(target);
      auto &self = *static_cast<WaylandShmProtocolTest *>(wl_resource_get_user_data(resource));
      if (opcode == 1) {
        self.frame_resource = nullptr;
        self.buffer_resource = nullptr;
        ++self.destroyed_frames;
        if (self.failed_frame == resource) {
          self.failed_frame = nullptr;
        }
        wl_resource_destroy(resource);
      } else if (resource == self.failed_frame) {
        wl_resource_post_error(resource, 0, "Cannot copy a terminal screencopy frame");
      } else {
        self.damage_copy = opcode == 2;
        self.buffer_resource = reinterpret_cast<wl_resource *>(args[0].o);  // NOSONAR(cpp:S3630): Wayland's server dispatcher encodes wl_resource pointers in wl_argument::o.
        ++self.copies;
        if (!wl_shm_buffer_get(self.buffer_resource)) {
          self.failed_frame = resource;
          wl_resource_post_event(resource, 3);
        }
      }
      return 0;
    }

    /**
     * @brief Create import parameter resources without using a GPU.
     */
    static int dispatch_dmabuf(const void *, void *target, std::uint32_t opcode, const wl_message *, wl_argument *args) {  // NOSONAR(cpp:S5008): Required by wl_dispatcher_func_t.
      auto resource = static_cast<wl_resource *>(target);
      if (opcode == 0) {
        wl_resource_destroy(resource);
      } else if (opcode == 1) {
        auto params = wl_resource_create(wl_resource_get_client(resource), &zwp_linux_buffer_params_v1_interface, 3, args[0].n);
        wl_resource_set_dispatcher(params, dispatch_params, nullptr, wl_resource_get_user_data(resource), nullptr);
      }
      return 0;
    }

    /**
     * @brief Complete or reject an asynchronous import through the Wayland event queue.
     */
    static int dispatch_params(const void *, void *target, std::uint32_t opcode, const wl_message *, wl_argument *) {  // NOSONAR(cpp:S5008): Required by wl_dispatcher_func_t.
      auto resource = static_cast<wl_resource *>(target);
      if (opcode == 0) {
        wl_resource_destroy(resource);
      } else if (opcode == 2) {
        auto &self = *static_cast<WaylandShmProtocolTest *>(wl_resource_get_user_data(resource));
        if (self.accept_dmabuf) {
          auto buffer = wl_resource_create(wl_resource_get_client(resource), &wl_buffer_interface, 1, 0);
          wl_resource_set_dispatcher(buffer, [](const void *, void *target, std::uint32_t, const wl_message *, wl_argument *) {  // NOSONAR(cpp:S5008): Required by wl_dispatcher_func_t.
            wl_resource_destroy(static_cast<wl_resource *>(target));
            return 0;
          },
                                     nullptr,
                                     nullptr,
                                     nullptr);
          wl_resource_post_event(resource, 0, buffer);
        } else {
          wl_resource_post_event(resource, 1);
        }
      }
      return 0;
    }

    struct {
      wl_display *server {};  ///< Private compositor display.
      wl_display *client {};  ///< Private capture connection.
      wl_registry *registry {};  ///< Registry used to bind test globals.
      wl_shm *shm {};  ///< Real libwayland SHM factory.
      wl_output *output {};  ///< Output targeted by capture requests.
      zwlr_screencopy_manager_v1 *manager {};  ///< Private screencopy factory.
      zwp_linux_dmabuf_v1 *dmabuf {};  ///< Controlled DMA-BUF import factory.
    } connection;  ///< Objects belonging to the private Wayland connection.

    wl_resource *frame_resource {};  ///< Active server-side screencopy frame.
    wl_resource *buffer_resource {};  ///< Buffer retained by the pending copy.
    wl_resource *failed_frame {};  ///< Terminal frame that must not receive another copy.
    std::unique_ptr<capture_t> capture;  ///< Actual Sunshine capture implementation.
    std::uint32_t width {2};  ///< Advertised frame width.
    std::uint32_t height {2};  ///< Advertised frame height.
    std::uint32_t stride {12};  ///< Advertised padded row size.
    bool hold_buffer_done {false};  ///< Defer the allocation decision for the rejection test.
    bool advertise_dmabuf {false};  ///< Attempt real GBM initialization before falling back.
    bool accept_dmabuf {false};  ///< Accept buffer construction but reject the later copy.
    bool blend_cursor {false};  ///< Cursor option of the most recently requested capture.
    int requests {0};  ///< Number of capture requests issued.
    bool inverted {false};  ///< Advertise vertically inverted captured pixels.
    bool damage_copy {false};  ///< Whether the last request used copy_with_damage.
    int copies {0};  ///< Number of real copy requests received.
    int destroyed_frames {0};  ///< Number of screencopy objects released by the client.
  };
}  // namespace

TEST_F(WaylandShmProtocolTest, CompletedPixelsRemainOwnedAcrossResolutionChanges) {
  start();
  ASSERT_EQ(copies, 1);
  ASSERT_EQ(capture->status, wl::dmabuf_t::WAITING);
  complete();
  ASSERT_EQ(capture->status, wl::dmabuf_t::READY);
  ASSERT_TRUE(capture->current_frame->is_shm);
  const auto first = capture->current_frame->shm_data;
  ASSERT_EQ(first.size(), stride * height);
  EXPECT_EQ(first[0], 10);
  EXPECT_EQ(first[stride], 11);
  width = 3;
  height = 1;
  stride = 16;
  start();
  EXPECT_EQ(capture->current_frame->shm_data, first);
  complete();
  EXPECT_EQ(capture->current_frame->sd.width, width);
  EXPECT_EQ(capture->current_frame->sd.height, height);
  EXPECT_EQ(capture->current_frame->shm_data.size(), stride * height);
}

TEST_F(WaylandShmProtocolTest, DamageCopyCanBeCancelledAndReplaced) {
  capture->with_damage = true;
  start();
  EXPECT_TRUE(damage_copy);
  EXPECT_TRUE(capture->cancel());
  ASSERT_TRUE(sync());
  EXPECT_EQ(destroyed_frames, 1);
  EXPECT_FALSE(capture->cancel());
  capture->with_damage = false;
  start();
  EXPECT_FALSE(damage_copy);
  complete();
  EXPECT_EQ(capture->status, wl::dmabuf_t::READY);
}

TEST_F(WaylandShmProtocolTest, AsyncImportRejectionCompletesShmWithoutDisplayRecreation) {
  hold_buffer_done = true;
  start();
  ASSERT_EQ(copies, 0);
  auto params = zwp_linux_dmabuf_v1_create_params(connection.dmabuf);
  static const zwp_linux_buffer_params_v1_listener listener {
    wl::dmabuf_t::buffer_params_created,
    wl::dmabuf_t::buffer_params_failed,
  };
  // Inject the import request, not the result: the real failed event must reach
  // Sunshine's callback and keep the original screencopy proxy usable.
  zwp_linux_buffer_params_v1_add_listener(params, &listener, capture->request);
  zwp_linux_buffer_params_v1_create(params, width, height, WL_SHM_FORMAT_XRGB8888, 0);
  ASSERT_TRUE(sync());
  ASSERT_TRUE(sync());
  EXPECT_EQ(copies, 1);
  EXPECT_EQ(destroyed_frames, 0);
  ASSERT_EQ(capture->status, wl::dmabuf_t::WAITING);
  complete();
  EXPECT_EQ(capture->status, wl::dmabuf_t::READY);
  EXPECT_TRUE(capture->current_frame->is_shm);
}

TEST_F(WaylandShmProtocolTest, LateDmabufCopyFailureRetriesShmOnFreshRequest) {
  fail_dmabuf_copy();
  ASSERT_EQ(requests, 2);
  EXPECT_EQ(destroyed_frames, 1);
  EXPECT_EQ(copies, 2);
  EXPECT_TRUE(blend_cursor);
  EXPECT_FALSE(damage_copy);
  ASSERT_EQ(capture->status, wl::dmabuf_t::WAITING);
  complete();
  EXPECT_EQ(capture->status, wl::dmabuf_t::READY);
  ASSERT_TRUE(capture->current_frame->is_shm);
  ASSERT_EQ(capture->current_frame->shm_data.size(), stride * height);
  EXPECT_EQ(capture->current_frame->shm_data[0], 10);
}

TEST_F(WaylandShmProtocolTest, FailedShmRetryDoesNotIssueAnotherCapture) {
  fail_dmabuf_copy();
  ASSERT_EQ(requests, 2);
  ASSERT_NE(frame_resource, nullptr);
  wl_resource_post_event(frame_resource, 3);
  ASSERT_TRUE(sync());
  ASSERT_TRUE(sync());
  EXPECT_EQ(requests, 2);
  EXPECT_EQ(destroyed_frames, 2);
  EXPECT_EQ(capture->status, wl::dmabuf_t::REINIT);
  EXPECT_FALSE(capture->cancel());
}

TEST_F(WaylandShmProtocolTest, LateDmabufCopyFailureWithoutShmReinitializes) {
  fail_dmabuf_copy(false);
  EXPECT_EQ(requests, 1);
  EXPECT_EQ(copies, 1);
  EXPECT_EQ(destroyed_frames, 1);
  EXPECT_EQ(capture->status, wl::dmabuf_t::REINIT);
}

TEST_F(WaylandShmProtocolTest, FailureBeforeCopyDoesNotRetry) {
  hold_buffer_done = true;
  start();
  wl_resource_post_event(frame_resource, 3);
  ASSERT_TRUE(sync());
  ASSERT_TRUE(sync());
  EXPECT_EQ(requests, 1);
  EXPECT_EQ(copies, 0);
  EXPECT_EQ(capture->status, wl::dmabuf_t::REINIT);
}

TEST_F(WaylandShmProtocolTest, ShmCopyFailureDoesNotRetry) {
  start();
  wl_resource_post_event(frame_resource, 3);
  ASSERT_TRUE(sync());
  ASSERT_TRUE(sync());
  EXPECT_EQ(requests, 1);
  EXPECT_EQ(copies, 1);
  EXPECT_EQ(capture->status, wl::dmabuf_t::REINIT);
}

TEST_F(WaylandShmProtocolTest, LateDmabufFailureRetryCanBeCancelled) {
  fail_dmabuf_copy();
  ASSERT_EQ(requests, 2);
  EXPECT_TRUE(capture->cancel());
  ASSERT_TRUE(sync());
  EXPECT_EQ(destroyed_frames, 2);
  EXPECT_EQ(capture->status, wl::dmabuf_t::READY);
  EXPECT_FALSE(capture->cancel());
}

TEST_F(WaylandShmProtocolTest, InvertedPixelsConvertIntoFrameOwnedBgra) {
  inverted = true;
  start();
  complete();
  std::array<std::uint8_t, 16> pixels {};
  platf::img_t img;
  img.data = pixels.data();
  img.width = width;
  img.height = height;
  img.row_pitch = width * 4;
  img.pixel_pitch = 4;
  ASSERT_TRUE(wl::convert_shm_frame(*capture->current_frame, img));
  EXPECT_EQ(pixels[0], 11);
  EXPECT_EQ(pixels[8], 10);
}

TEST_F(WaylandShmProtocolTest, GbmInitializationFailureStillCompletesShm) {
  const auto previous = config::video.adapter_name;
  auto restore = util::fail_guard([&previous] {
    config::video.adapter_name = previous;
  });
  config::video.adapter_name = "/nonexistent-sunshine-test-render-node";
  advertise_dmabuf = true;
  start();
  ASSERT_EQ(copies, 1);
  complete();
  EXPECT_EQ(capture->status, wl::dmabuf_t::READY);
  EXPECT_TRUE(capture->current_frame->is_shm);
}

TEST_F(WaylandShmProtocolTest, CapturedShmPixelsCanBeEncodedAsH264) {
  width = 16;
  height = 16;
  stride = width * 4;
  start();
  complete();

  std::vector<std::uint8_t> pixels(stride * height + AV_INPUT_BUFFER_PADDING_SIZE);
  platf::img_t img;
  img.data = pixels.data();
  img.width = width;
  img.height = height;
  img.row_pitch = stride;
  img.pixel_pitch = 4;
  ASSERT_TRUE(wl::convert_shm_frame(*capture->current_frame, img));

  auto codec = avcodec_find_encoder_by_name("libx264");
  ASSERT_NE(codec, nullptr);
  auto free_context = [](AVCodecContext *context) {
    avcodec_free_context(&context);
  };
  std::unique_ptr<AVCodecContext, decltype(free_context)> context(avcodec_alloc_context3(codec), free_context);
  ASSERT_NE(context, nullptr);
  context->width = width;
  context->height = height;
  context->pix_fmt = AV_PIX_FMT_YUV420P;
  context->time_base = AVRational {1, 60};
  context->thread_count = 1;
  ASSERT_EQ(av_opt_set(context->priv_data, "preset", "ultrafast", 0), 0);
  ASSERT_EQ(av_opt_set(context->priv_data, "tune", "zerolatency", 0), 0);
  ASSERT_EQ(avcodec_open2(context.get(), codec, nullptr), 0);

  auto free_frame = [](AVFrame *frame) {
    av_frame_free(&frame);
  };
  std::unique_ptr<AVFrame, decltype(free_frame)> frame(av_frame_alloc(), free_frame);
  ASSERT_NE(frame, nullptr);
  frame->format = context->pix_fmt;
  frame->width = width;
  frame->height = height;
  frame->pts = 0;
  ASSERT_EQ(av_frame_get_buffer(frame.get(), 0), 0);

  std::unique_ptr<SwsContext, decltype(&sws_freeContext)> converter(sws_getContext(width, height, AV_PIX_FMT_BGRA, width, height, AV_PIX_FMT_YUV420P, SWS_POINT, nullptr, nullptr, nullptr), &sws_freeContext);
  ASSERT_NE(converter, nullptr);
  const std::array<const std::uint8_t *, 4> source {pixels.data()};
  const std::array<int, 4> pitches {static_cast<int>(stride)};
  ASSERT_EQ(sws_scale(converter.get(), source.data(), pitches.data(), 0, height, frame->data, frame->linesize), height);
  ASSERT_EQ(avcodec_send_frame(context.get(), frame.get()), 0);

  auto free_packet = [](AVPacket *packet) {
    av_packet_free(&packet);
  };
  std::unique_ptr<AVPacket, decltype(free_packet)> packet(av_packet_alloc(), free_packet);
  ASSERT_NE(packet, nullptr);
  ASSERT_EQ(avcodec_receive_packet(context.get(), packet.get()), 0);
  EXPECT_GT(packet->size, 0);
}

TEST_F(WaylandShmProtocolTest, MissingShmReinitializesWithoutIssuingCopy) {
  capture->listen(connection.manager, connection.dmabuf, nullptr, nullptr, connection.output);
  ASSERT_TRUE(sync());
  ASSERT_TRUE(sync());
  EXPECT_EQ(copies, 0);
  EXPECT_EQ(destroyed_frames, 1);
  EXPECT_EQ(capture->status, wl::dmabuf_t::REINIT);
}
#endif
