/**
 * @file src/platform/linux/portalgrab.cpp
 * @brief Definitions for XDG portal grab.
 */
// standard includes
#include <fcntl.h>
#include <string.h>
#include <thread>

// lib includes
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <libdrm/drm_fourcc.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/type-info.h>
#include <spa/pod/builder.h>

// local includes
#include "cuda.h"
#include "graphics.h"
#include "src/main.h"
#include "src/platform/common.h"
#include "src/video.h"
#include "vaapi.h"
#include "wayland.h"

#define SPA_POD_BUFFER_SIZE 4096
#define MAX_PARAMS 200
#define MAX_DMABUF_FORMATS 200
#define MAX_DMABUF_MODIFIERS 200

#define SOURCE_TYPE_MONITOR 1
#define CURSOR_MODE_EMBEDDED 2

#define PERSIST_FORGET 0
#define PERSIST_WHILE_RUNNING 1

#define PORTAL_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define REMOTE_DESKTOP_IFACE "org.freedesktop.portal.RemoteDesktop"
#define SCREENCAST_IFACE "org.freedesktop.portal.ScreenCast"
#define REQUEST_IFACE "org.freedesktop.portal.Request"

#define REQUEST_PREFIX "/org/freedesktop/portal/desktop/request/"
#define SESSION_PREFIX "/org/freedesktop/portal/desktop/session/"

using namespace std::literals;

namespace portal {
  static char *restore_token;

  struct format_map_t {
    uint64_t fourcc;
    int32_t pw_format;
  } format_map[] = {
    {DRM_FORMAT_ARGB8888, SPA_VIDEO_FORMAT_BGRA},
    {DRM_FORMAT_XRGB8888, SPA_VIDEO_FORMAT_BGRx},
    {0, 0},
  };

  struct dbus_response_t {
    GMainLoop *loop;
    GVariant *response;
    guint subscription_id;
  };

  struct stream_data_t {
    struct pw_stream *stream;
    struct spa_hook stream_listener;
    struct spa_video_info format;
    struct pw_buffer *current_buffer;
    uint64_t drm_format;
  };

  struct dmabuf_format_info_t {
    int32_t format;
    uint64_t *modifiers;
    int n_modifiers;
  };

  class dbus_t {
  public:
    ~dbus_t() {
      g_object_unref(screencast_proxy);
      g_object_unref(remote_desktop_proxy);
      g_object_unref(conn);
    }

    int init() {
      conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
      if (!conn) {
        return -1;
      }
      remote_desktop_proxy = g_dbus_proxy_new_sync(conn, G_DBUS_PROXY_FLAGS_NONE, NULL, PORTAL_NAME, PORTAL_PATH, REMOTE_DESKTOP_IFACE, NULL, NULL);
      if (!remote_desktop_proxy) {
        return -1;
      }
      screencast_proxy = g_dbus_proxy_new_sync(conn, G_DBUS_PROXY_FLAGS_NONE, NULL, PORTAL_NAME, PORTAL_PATH, SCREENCAST_IFACE, NULL, NULL);
      if (!screencast_proxy) {
        return -1;
      }

      return 0;
    }

    int connect_to_portal() {
      g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
      g_autofree gchar *session_path = NULL, *session_token = NULL;
      create_session_path(conn, &session_path, &session_token);

      if (create_session(loop, session_path, session_token) < 0) {
        return -1;
      }
      if (select_remote_desktop_devices(loop, session_path) < 0) {
        return -1;
      }
      if (select_screencast_sources(loop, session_path) < 0) {
        return -1;
      }
      if (start_session(loop, session_path, pipewire_node, width, height) < 0) {
        return -1;
      }
      if (open_pipewire_remote(session_path, pipewire_fd) < 0) {
        return -1;
      }

      return 0;
    }

    int pipewire_fd;
    int pipewire_node;
    int width;
    int height;

  private:
    GDBusConnection *conn;
    GDBusProxy *screencast_proxy;
    GDBusProxy *remote_desktop_proxy;

    int create_session(GMainLoop *loop, const gchar *session_path, const gchar *session_token) {
      dbus_response_t response = {
        0,
      };
      g_autofree gchar *request_path = NULL, *request_token = NULL;
      create_request_path(conn, &request_path, &request_token);
      dbus_response_init(&response, loop, conn, request_path);

      GVariantBuilder builder;
      g_variant_builder_init(&builder, G_VARIANT_TYPE("(a{sv})"));
      g_variant_builder_open(&builder, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&builder, "{sv}", "handle_token", g_variant_new_string(request_token));
      g_variant_builder_add(&builder, "{sv}", "session_handle_token", g_variant_new_string(session_token));
      g_variant_builder_close(&builder);

      g_autoptr(GError) err = NULL;
      g_autoptr(GVariant) reply = g_dbus_proxy_call_sync(remote_desktop_proxy, "CreateSession", g_variant_builder_end(&builder), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);

      if (err) {
        BOOST_LOG(error) << "Could not create session: "sv << err->message;
        return -1;
      }

      g_autoptr(GVariant) ignore = dbus_response_wait(&response);
      return 0;
    }

    int select_remote_desktop_devices(GMainLoop *loop, const gchar *session_path) {
      dbus_response_t response = {
        0,
      };
      g_autofree gchar *request_path = NULL, *request_token = NULL;
      create_request_path(conn, &request_path, &request_token);
      dbus_response_init(&response, loop, conn, request_path);

      GVariantBuilder builder;
      g_variant_builder_init(&builder, G_VARIANT_TYPE("(oa{sv})"));
      g_variant_builder_add(&builder, "o", session_path);
      g_variant_builder_open(&builder, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&builder, "{sv}", "handle_token", g_variant_new_string(request_token));
      g_variant_builder_add(&builder, "{sv}", "persist_mode", g_variant_new_uint32(PERSIST_WHILE_RUNNING));
      if (restore_token) {
        g_variant_builder_add(&builder, "{sv}", "restore_token", g_variant_new_string(restore_token));
      }
      g_variant_builder_close(&builder);

      g_autoptr(GError) err = NULL;
      g_autoptr(GVariant) reply = g_dbus_proxy_call_sync(remote_desktop_proxy, "SelectDevices", g_variant_builder_end(&builder), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);

      if (err) {
        BOOST_LOG(error) << "Could not select devices: "sv << err->message;
        return -1;
      }

      g_autoptr(GVariant) ignore = dbus_response_wait(&response);
      return 0;
    }

    int select_screencast_sources(GMainLoop *loop, const gchar *session_path) {
      dbus_response_t response = {
        0,
      };
      g_autofree gchar *request_path = NULL, *request_token = NULL;
      create_request_path(conn, &request_path, &request_token);
      dbus_response_init(&response, loop, conn, request_path);

      GVariantBuilder builder;
      g_variant_builder_init(&builder, G_VARIANT_TYPE("(oa{sv})"));
      g_variant_builder_add(&builder, "o", session_path);
      g_variant_builder_open(&builder, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&builder, "{sv}", "handle_token", g_variant_new_string(request_token));
      g_variant_builder_add(&builder, "{sv}", "types", g_variant_new_uint32(SOURCE_TYPE_MONITOR));
      g_variant_builder_add(&builder, "{sv}", "cursor_mode", g_variant_new_uint32(CURSOR_MODE_EMBEDDED));
      g_variant_builder_add(&builder, "{sv}", "persist_mode", g_variant_new_uint32(PERSIST_FORGET));
      g_variant_builder_close(&builder);

      g_autoptr(GError) err = NULL;
      g_autoptr(GVariant) reply = g_dbus_proxy_call_sync(screencast_proxy, "SelectSources", g_variant_builder_end(&builder), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
      if (err) {
        BOOST_LOG(error) << "Could not select sources: "sv << err->message;
        return -1;
      }

      g_autoptr(GVariant) ignore = dbus_response_wait(&response);
      return 0;
    }

    int start_session(GMainLoop *loop, const gchar *session_path, int &pipewire_node, int &width, int &height) {
      dbus_response_t response = {
        0,
      };
      g_autofree gchar *request_path = NULL, *request_token = NULL;
      create_request_path(conn, &request_path, &request_token);
      dbus_response_init(&response, loop, conn, request_path);

      GVariantBuilder builder;
      g_variant_builder_init(&builder, G_VARIANT_TYPE("(osa{sv})"));
      g_variant_builder_add(&builder, "o", session_path);
      g_variant_builder_add(&builder, "s", "");
      g_variant_builder_open(&builder, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&builder, "{sv}", "handle_token", g_variant_new_string(request_token));
      g_variant_builder_close(&builder);

      g_autoptr(GError) err = NULL;
      g_autoptr(GVariant) reply = g_dbus_proxy_call_sync(remote_desktop_proxy, "Start", g_variant_builder_end(&builder), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
      if (err) {
        BOOST_LOG(error) << "Could not start session: "sv << err->message;
        return -1;
      }

      g_autoptr(GVariant) start_response = dbus_response_wait(&response);

      g_autoptr(GVariant) dict = NULL, streams = NULL;
      g_variant_get(start_response, "(u@a{sv})", NULL, &dict, NULL);
      streams = g_variant_lookup_value(dict, "streams", G_VARIANT_TYPE("a(ua{sv})"));
      // Preserve restore token for multiple runs (e.g. probing)
      if (!restore_token) {
        g_variant_lookup(dict, "restore_token", "s", &restore_token, NULL);
      }

      GVariantIter iter;
      g_autoptr(GVariant) value = NULL;
      g_variant_iter_init(&iter, streams);
      while (g_variant_iter_next(&iter, "(u@a{sv})", &pipewire_node, &value)) {
        g_variant_lookup(value, "size", "(ii)", &width, &height, NULL);
      }

      return 0;
    }

    int open_pipewire_remote(const gchar *session_path, int &fd) {
      GUnixFDList *fd_list;
      GVariant *msg = g_variant_new("(oa{sv})", session_path, NULL);

      g_autoptr(GError) err = NULL;
      g_autoptr(GVariant) reply = g_dbus_proxy_call_with_unix_fd_list_sync(screencast_proxy, "OpenPipeWireRemote", msg, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &fd_list, NULL, &err);
      if (err) {
        BOOST_LOG(error) << "Could not open pipewire remote: "sv << err->message;
        return -1;
      }

      int fd_handle;
      g_variant_get(reply, "(h)", &fd_handle);
      fd = g_unix_fd_list_get(fd_list, fd_handle, NULL);
      return 0;
    }

    static void on_response_received_cb(GDBusConnection *connection, const gchar *sender_name, const gchar *object_path, const gchar *interface_name, const gchar *signal_name, GVariant *parameters, gpointer user_data) {
      dbus_response_t *response = (dbus_response_t *) user_data;
      response->response = g_variant_ref_sink(parameters);
      g_main_loop_quit(response->loop);
    }

    static gchar *get_sender_string(GDBusConnection *conn) {
      gchar *sender = g_strdup(g_dbus_connection_get_unique_name(conn) + 1);
      gchar *dot;
      while ((dot = strstr(sender, ".")) != NULL) {
        *dot = '_';
      }
      return sender;
    }

    static void create_request_path(GDBusConnection *conn, gchar **out_path, gchar **out_token) {
      static uint32_t request_count = 0;

      request_count++;

      if (out_token) {
        *out_token = g_strdup_printf("Sunshine%u", request_count);
      }
      if (out_path) {
        g_autofree gchar *sender = get_sender_string(conn);
        *out_path = g_strdup_printf(REQUEST_PREFIX "%s/Sunshine%u", sender, request_count);
      }
    }

    static void create_session_path(GDBusConnection *conn, gchar **out_path, gchar **out_token) {
      static uint32_t session_count = 0;

      session_count++;

      if (out_token) {
        *out_token = g_strdup_printf("Sunshine%u", session_count);
      }

      if (out_path) {
        g_autofree gchar *sender = get_sender_string(conn);
        *out_path = g_strdup_printf(SESSION_PREFIX "%s/Sunshine%u", sender, session_count);
      }
    }

    static void dbus_response_init(struct dbus_response_t *response, GMainLoop *loop, GDBusConnection *conn, const char *request_path) {
      response->loop = loop;
      g_dbus_connection_signal_subscribe(conn, PORTAL_NAME, REQUEST_IFACE, "Response", request_path, NULL, G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, on_response_received_cb, response, NULL);
    }

    static GVariant *dbus_response_wait(struct dbus_response_t *response) {
      g_main_loop_run(response->loop);
      return response->response;
    }
  };

  class pipewire_t {
  public:
    pipewire_t() {
      loop = pw_thread_loop_new("Pipewire thread", NULL);
      pw_thread_loop_start(loop);
    }

    ~pipewire_t() {
      pw_thread_loop_stop(loop);
      if (stream_data.stream) {
        pw_stream_set_active(stream_data.stream, false);
        pw_stream_disconnect(stream_data.stream);
        pw_stream_destroy(stream_data.stream);
      }
      if (core) {
        pw_core_disconnect(core);
      }
      if (context) {
        pw_context_destroy(context);
      }
      if (fd >= 0) {
        close(fd);
      }
      pw_thread_loop_destroy(loop);
    }

    void init(int stream_fd, int stream_node) {
      fd = stream_fd;
      node = stream_node;

      context = pw_context_new(pw_thread_loop_get_loop(loop), NULL, 0);
      core = pw_context_connect_fd(context, dup(fd), NULL, 0);
      pw_core_add_listener(core, &core_listener, &core_events, NULL);
    }

    void ensure_stream(platf::mem_type_e mem_type, uint32_t width, uint32_t height, uint32_t refresh_rate, struct dmabuf_format_info_t *dmabuf_infos, int n_dmabuf_infos) {
      pw_thread_loop_lock(loop);
      if (!stream_data.stream) {
        struct pw_properties *props;

        props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Screen", NULL);

        stream_data.stream = pw_stream_new(core, "Sunshine Video Capture", props);
        pw_stream_add_listener(stream_data.stream, &stream_data.stream_listener, &stream_events, &stream_data);

        uint8_t buffer[SPA_POD_BUFFER_SIZE];
        struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

        int n_params = 0;
        const struct spa_pod *params[MAX_PARAMS];

        // Add preferred parameters for DMA-BUF with modifiers
        if (n_dmabuf_infos > 0 && (mem_type == platf::mem_type_e::vaapi || mem_type == platf::mem_type_e::cuda)) {
          for (int i = 0; i < n_dmabuf_infos; i++) {
            params[n_params++] = build_format_parameter(&pod_builder, width, height, refresh_rate, dmabuf_infos[i].format, dmabuf_infos[i].modifiers, dmabuf_infos[i].n_modifiers);
          }
        }

        // Add fallback for memptr
        for (int i = 0; format_map[i].fourcc != 0; i++) {
          params[n_params++] = build_format_parameter(&pod_builder, width, height, refresh_rate, format_map[i].pw_format, NULL, 0);
        }

        pw_stream_connect(stream_data.stream, PW_DIRECTION_INPUT, node, (enum pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, n_params);
      }
      pw_thread_loop_unlock(loop);
    }

    void fill_img(platf::img_t *img) {
      pw_thread_loop_lock(loop);

      if (stream_data.current_buffer) {
        struct spa_buffer *buf;
        buf = stream_data.current_buffer->buffer;
        if (buf->datas[0].chunk->size != 0) {
          if (buf->datas[0].type == SPA_DATA_DmaBuf) {
            auto img_descriptor = (egl::img_descriptor_t *) img;
            img_descriptor->sd.width = stream_data.format.info.raw.size.width;
            img_descriptor->sd.height = stream_data.format.info.raw.size.height;
            img_descriptor->sd.modifier = stream_data.format.info.raw.modifier;
            img_descriptor->sd.fourcc = stream_data.drm_format;

            for (int i = 0; i < MIN(buf->n_datas, 4); i++) {
              img_descriptor->sd.fds[i] = dup(buf->datas[i].fd);
              img_descriptor->sd.pitches[i] = buf->datas[i].chunk->stride;
              img_descriptor->sd.offsets[i] = buf->datas[i].chunk->offset;
            }
          } else {
            img->data = (std::uint8_t *) buf->datas[0].data;
          }
        }
      }

      pw_thread_loop_unlock(loop);
    }

  private:
    struct pw_thread_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct spa_hook core_listener;
    struct stream_data_t stream_data;
    int fd;
    int node;

    static struct spa_pod *build_format_parameter(struct spa_pod_builder *b, uint32_t width, uint32_t height, uint32_t refresh_rate, int32_t format, uint64_t *modifiers, int n_modifiers) {
      struct spa_pod_frame object_frame, modifier_frame;
      struct spa_rectangle sizes[3];
      struct spa_fraction framerates[3];

      sizes[0] = SPA_RECTANGLE(width, height);  // Preferred
      sizes[1] = SPA_RECTANGLE(1, 1);
      sizes[2] = SPA_RECTANGLE(8192, 4096);

      framerates[0] = SPA_FRACTION(refresh_rate, 1);  // Preferred
      framerates[1] = SPA_FRACTION(0, 1);
      framerates[2] = SPA_FRACTION(1000, 1);

      spa_pod_builder_push_object(b, &object_frame, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
      spa_pod_builder_add(b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
      spa_pod_builder_add(b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
      spa_pod_builder_add(b, SPA_FORMAT_VIDEO_format, SPA_POD_Id(format), 0);
      spa_pod_builder_add(b, SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&sizes[0], &sizes[1], &sizes[2]), 0);
      spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&framerates[0], &framerates[1], &framerates[2]), 0);

      if (n_modifiers) {
        spa_pod_builder_prop(b, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
        spa_pod_builder_push_choice(b, &modifier_frame, SPA_CHOICE_Enum, 0);

        // Preferred value, we pick the first modifier be the preferred one
        spa_pod_builder_long(b, modifiers[0]);
        for (uint32_t i = 0; i < n_modifiers; i++) {
          spa_pod_builder_long(b, modifiers[i]);
        }

        spa_pod_builder_pop(b, &modifier_frame);
      }

      return (struct spa_pod *) spa_pod_builder_pop(b, &object_frame);
    }

    static void on_core_info_cb(void *user_data, const struct pw_core_info *pw_info) {
      BOOST_LOG(info) << "Connected to pipewire version "sv << pw_info->version;
    }

    static void on_core_error_cb(void *user_data, uint32_t id, int seq, int res, const char *message) {
      BOOST_LOG(info) << "Pipewire Error, id:"sv << id << " seq:"sv << seq << " message: "sv << message;
    }

    constexpr static const struct pw_core_events core_events = {
      .version = PW_VERSION_CORE_EVENTS,
      .info = on_core_info_cb,
      .error = on_core_error_cb,
    };

    static void on_process(void *user_data) {
      struct stream_data_t *d = (struct stream_data_t *) user_data;
      struct pw_buffer *b = NULL;

      while (true) {
        struct pw_buffer *aux = pw_stream_dequeue_buffer(d->stream);
        if (!aux) {
          break;
        }
        if (b) {
          pw_stream_queue_buffer(d->stream, b);
        }
        b = aux;
      }

      if (b == NULL) {
        BOOST_LOG(warning) << "out of pipewire buffers"sv;
        return;
      }

      if (d->current_buffer) {
        pw_stream_queue_buffer(d->stream, d->current_buffer);
      }
      d->current_buffer = b;
    }

    static void on_param_changed(void *user_data, uint32_t id, const struct spa_pod *param) {
      struct stream_data_t *d = (struct stream_data_t *) user_data;

      d->current_buffer = NULL;

      if (param == NULL || id != SPA_PARAM_Format) {
        return;
      }
      if (spa_format_parse(param, &d->format.media_type, &d->format.media_subtype) < 0) {
        return;
      }
      if (d->format.media_type != SPA_MEDIA_TYPE_video || d->format.media_subtype != SPA_MEDIA_SUBTYPE_raw) {
        return;
      }
      if (spa_format_video_raw_parse(param, &d->format.info.raw) < 0) {
        return;
      }

      BOOST_LOG(info) << "Video format: "sv << d->format.info.raw.format;
      BOOST_LOG(info) << "Size: "sv << d->format.info.raw.size.width << "x"sv << d->format.info.raw.size.height;
      BOOST_LOG(info) << "Framerate: "sv << d->format.info.raw.framerate.num << "/"sv << d->format.info.raw.framerate.denom;

      uint64_t drm_format = 0;
      for (int n = 0; format_map[n].fourcc != 0; n++) {
        if (format_map[n].pw_format == d->format.info.raw.format) {
          drm_format = format_map[n].fourcc;
        }
      }
      d->drm_format = drm_format;

      uint32_t buffer_types = 0;
      if (spa_pod_find_prop(param, NULL, SPA_FORMAT_VIDEO_modifier) != NULL && d->drm_format) {
        BOOST_LOG(info) << "using DMA-BUF buffers"sv;
        buffer_types |= 1 << SPA_DATA_DmaBuf;
      } else {
        BOOST_LOG(info) << "using memory buffers"sv;
        buffer_types |= 1 << SPA_DATA_MemPtr;
      }

      // Ack the buffer type
      uint8_t buffer[SPA_POD_BUFFER_SIZE];
      const struct spa_pod *params[1];
      int n_params = 0;
      struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
      params[n_params++] = (const struct spa_pod *) spa_pod_builder_add_object(&pod_builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_dataType, SPA_POD_Int(buffer_types));
      pw_stream_update_params(d->stream, params, n_params);
    }

    constexpr static const struct pw_stream_events stream_events = {
      .version = PW_VERSION_STREAM_EVENTS,
      .param_changed = on_param_changed,
      .process = on_process,
    };
  };

  class portal_t: public platf::display_t {
  public:
    int init(platf::mem_type_e hwdevice_type, const std::string &display_name, const ::video::config_t &config) {
      framerate = config.framerate;
      delay = std::chrono::nanoseconds {1s} / framerate;
      mem_type = hwdevice_type;

      if (get_dmabuf_modifiers() < 0) {
        return -1;
      }
      if (dbus.init() < 0) {
        return -1;
      }
      if (dbus.connect_to_portal() < 0) {
        return -1;
      }

      width = dbus.width;
      height = dbus.height;
      framerate = config.framerate;

      pipewire.init(dbus.pipewire_fd, dbus.pipewire_node);

      return 0;
    }

    platf::capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool show_cursor) {
      // FIXME: show_cursor is ignored
      if (!pull_free_image_cb(img_out)) {
        return platf::capture_e::interrupted;
      }

      auto img_egl = (egl::img_descriptor_t *) img_out.get();
      img_egl->reset();
      pipewire.fill_img(img_egl);
      img_egl->sequence = ++sequence;

      return platf::capture_e::ok;
    }

    std::shared_ptr<platf::img_t> alloc_img() override {
      // Note: this img_t type is also used for memory buffers
      auto img = std::make_shared<egl::img_descriptor_t>();

      img->width = width;
      img->height = height;
      img->pixel_pitch = 4;
      img->row_pitch = img->pixel_pitch * width;
      img->sequence = 0;
      img->serial = std::numeric_limits<decltype(img->serial)>::max();
      img->data = nullptr;
      std::fill_n(img->sd.fds, 4, -1);

      return img;
    }

    platf::capture_e capture(const push_captured_image_cb_t &push_captured_image_cb, const pull_free_image_cb_t &pull_free_image_cb, bool *cursor) override {
      auto next_frame = std::chrono::steady_clock::now();

      pipewire.ensure_stream(mem_type, width, height, framerate, (struct dmabuf_format_info_t *) dmabuf_infos, n_dmabuf_infos);

      while (true) {
        auto now = std::chrono::steady_clock::now();

        if (next_frame > now) {
          std::this_thread::sleep_for((next_frame - now) / 3 * 2);
        }
        while (next_frame > now) {
          std::this_thread::sleep_for(1ns);
          now = std::chrono::steady_clock::now();
        }
        next_frame = now + delay;

        std::shared_ptr<platf::img_t> img_out;
        auto status = snapshot(pull_free_image_cb, img_out, 1000ms, *cursor);
        switch (status) {
          case platf::capture_e::reinit:
          case platf::capture_e::error:
          case platf::capture_e::interrupted:
            return status;
          case platf::capture_e::timeout:
            push_captured_image_cb(std::move(img_out), false);
            break;
          case platf::capture_e::ok:
            push_captured_image_cb(std::move(img_out), true);
            break;
          default:
            BOOST_LOG(error) << "Unrecognized capture status ["sv << (int) status << ']';
            return status;
        }
      }

      return platf::capture_e::ok;
    }

    std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(platf::pix_fmt_e pix_fmt) override {
#ifdef SUNSHINE_BUILD_VAAPI
      if (mem_type == platf::mem_type_e::vaapi) {
        return va::make_avcodec_encode_device(width, height, n_dmabuf_infos > 0);
      }
#endif

#ifdef SUNSHINE_BUILD_CUDA
      if (mem_type == platf::mem_type_e::cuda) {
        if (n_dmabuf_infos > 0) {
          return cuda::make_avcodec_gl_encode_device(width, height, 0, 0);
        } else {
          return cuda::make_avcodec_encode_device(width, height, false);
        }
      }
#endif

      return std::make_unique<platf::avcodec_encode_device_t>();
    }

    int dummy_img(platf::img_t *img) override {
      if (!img) {
        return -1;
      };

      img->data = new std::uint8_t[img->height * img->row_pitch];
      std::fill_n((std::uint8_t *) img->data, img->height * img->row_pitch, 0);
      return 0;
    }

  private:
    int get_dmabuf_modifiers() {
      if (wl_display.init() < 0) {
        return -1;
      }

      auto egl_display = egl::make_display(wl_display.get());
      if (!egl_display) {
        return -1;
      }

      if (eglQueryDmaBufFormatsEXT && eglQueryDmaBufModifiersEXT) {
        EGLint num_dmabuf_formats = 0;
        EGLint dmabuf_formats[MAX_DMABUF_FORMATS] = {
          0,
        };
        eglQueryDmaBufFormatsEXT(egl_display.get(), MAX_DMABUF_FORMATS, (EGLint *) &dmabuf_formats, &num_dmabuf_formats);
        if (num_dmabuf_formats > MAX_DMABUF_FORMATS) {
          BOOST_LOG(warning) << "Some DMA-BUF formats are being ignored"sv;
        }

        EGLint i;
        for (i = 0; i < MIN(num_dmabuf_formats, MAX_DMABUF_FORMATS); i++) {
          uint32_t pw_format = 0;
          for (int n = 0; format_map[n].fourcc != 0; n++) {
            if (format_map[n].fourcc == dmabuf_formats[i]) {
              pw_format = format_map[n].pw_format;
            }
          }

          if (pw_format == 0) {
            continue;
          }

          EGLint num_modifiers = 0;
          EGLuint64KHR mods[MAX_DMABUF_MODIFIERS] = {
            0,
          };
          EGLBoolean external_only;
          eglQueryDmaBufModifiersEXT(egl_display.get(), dmabuf_formats[i], MAX_DMABUF_MODIFIERS, (EGLuint64KHR *) &mods, &external_only, &num_modifiers);
          if (num_modifiers > MAX_DMABUF_MODIFIERS) {
            BOOST_LOG(warning) << "Some DMA-BUF modifiers are being ignored"sv;
          }

          dmabuf_infos[n_dmabuf_infos].format = pw_format;
          dmabuf_infos[n_dmabuf_infos].n_modifiers = MIN(num_modifiers, MAX_DMABUF_MODIFIERS);
          dmabuf_infos[n_dmabuf_infos].modifiers =
            (uint64_t *) g_memdup2(mods, sizeof(uint64_t) * dmabuf_infos[n_dmabuf_infos].n_modifiers);
          ++n_dmabuf_infos;
        }
      }

      return 0;
    }

    platf::mem_type_e mem_type;
    wl::display_t wl_display;
    dbus_t dbus;
    pipewire_t pipewire;
    struct dmabuf_format_info_t dmabuf_infos[MAX_DMABUF_FORMATS];
    int n_dmabuf_infos;
    std::chrono::nanoseconds delay;
    std::uint64_t sequence {};
    uint32_t framerate;
  };
}  // namespace portal

namespace platf {
  std::shared_ptr<display_t> portal_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
    if (hwdevice_type != platf::mem_type_e::system && hwdevice_type != platf::mem_type_e::vaapi && hwdevice_type != platf::mem_type_e::cuda) {
      BOOST_LOG(error) << "Could not initialize display with the given hw device type."sv;
      return nullptr;
    }

    auto portal = std::make_shared<portal::portal_t>();
    if (portal->init(hwdevice_type, display_name, config)) {
      return nullptr;
    }

    return portal;
  }

  std::vector<std::string> portal_display_names() {
    std::vector<std::string> display_names;
    auto dbus = std::make_shared<portal::dbus_t>();

    if (dbus->init() < 0) {
      return {};
    }

    pw_init(NULL, NULL);

    display_names.emplace_back("org.freedesktop.portal.Desktop");
    return display_names;
  }
}  // namespace platf
