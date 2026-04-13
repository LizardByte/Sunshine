/**
 * @file src/platform/linux/portalgrab.cpp
 * @brief Definitions for XDG portal grab.
 */
// standard includes
#include <array>
#include <cstdlib>
#include <fcntl.h>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <string.h>
#include <string_view>
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
#include "vulkan_encode.h"
#include "wayland.h"

#if !defined(__FreeBSD__)
  // platform includes
  #include <sys/capability.h>
  #include <sys/prctl.h>
#endif

namespace {
  // Buffer and limit constants
  constexpr int SPA_POD_BUFFER_SIZE = 4096;
  constexpr int MAX_PARAMS = 200;
  constexpr int MAX_DMABUF_FORMATS = 200;
  constexpr int MAX_DMABUF_MODIFIERS = 200;

  // Portal configuration constants
  constexpr uint32_t SOURCE_TYPE_MONITOR = 1;
  constexpr uint32_t CURSOR_MODE_EMBEDDED = 2;

  constexpr uint32_t PERSIST_FORGET = 0;
  constexpr uint32_t PERSIST_WHILE_RUNNING = 1;
  constexpr uint32_t PERSIST_UNTIL_REVOKED = 2;

  constexpr uint32_t TYPE_KEYBOARD = 1;
  constexpr uint32_t TYPE_POINTER = 2;
  constexpr uint32_t TYPE_TOUCHSCREEN = 4;

  // Portal D-Bus interface names and paths
  constexpr const char *PORTAL_NAME = "org.freedesktop.portal.Desktop";
  constexpr const char *PORTAL_PATH = "/org/freedesktop/portal/desktop";
  constexpr const char *REMOTE_DESKTOP_IFACE = "org.freedesktop.portal.RemoteDesktop";
  constexpr const char *SCREENCAST_IFACE = "org.freedesktop.portal.ScreenCast";
  constexpr const char *REQUEST_IFACE = "org.freedesktop.portal.Request";

  constexpr const char REQUEST_PREFIX[] = "/org/freedesktop/portal/desktop/request/";
  constexpr const char SESSION_PREFIX[] = "/org/freedesktop/portal/desktop/session/";

  bool env_truthy(const char *name) {
    const char *value = std::getenv(name);
    if (!value) {
      return false;
    }

    const std::string_view v {value};
    return v == "1" || v == "true" || v == "TRUE" || v == "yes" || v == "YES" || v == "on" || v == "ON";
  }
}  // namespace

using namespace std::literals;

namespace portal {
  // Forward declarations
  class runtime_t;

  class restore_token_t {
  public:
    static std::string get() {
      return *token_;
    }

    static void set(std::string_view value) {
      *token_ = value;
    }

    static bool empty() {
      return token_->empty();
    }

    static void load() {
      std::ifstream file(get_file_path());
      if (file.is_open()) {
        std::getline(file, *token_);
        if (!token_->empty()) {
          BOOST_LOG(info) << "[portalgrab] Loaded portal restore token from disk"sv;
        }
      }
    }

    static void save() {
      if (token_->empty()) {
        return;
      }
      std::ofstream file(get_file_path());
      if (file.is_open()) {
        file << *token_;
        BOOST_LOG(info) << "[portalgrab] Saved portal restore token to disk"sv;
      } else {
        BOOST_LOG(warning) << "[portalgrab] Failed to save portal restore token"sv;
      }
    }

  private:
    static inline const std::unique_ptr<std::string> token_ = std::make_unique<std::string>();

    static std::string get_file_path() {
      return platf::appdata().string() + "/portal_token";
    }
  };

  struct format_map_t {
    uint64_t fourcc;
    int32_t pw_format;
  };

  static constexpr std::array<format_map_t, 3> format_map = {{
    {DRM_FORMAT_ARGB8888, SPA_VIDEO_FORMAT_BGRA},
    {DRM_FORMAT_XRGB8888, SPA_VIDEO_FORMAT_BGRx},
    {0, 0},
  }};

  struct dbus_response_t {
    GMainLoop *loop;
    GVariant *response;
    guint subscription_id;
  };

  struct shared_state_t {
    std::atomic<int> negotiated_width {0};
    std::atomic<int> negotiated_height {0};
    std::atomic<bool> stream_dead {false};
  };

  struct stream_data_t {
    struct pw_stream *stream;
    struct spa_hook stream_listener;
    struct spa_video_info format;
    struct pw_buffer *current_buffer;
    uint64_t drm_format;
    std::shared_ptr<shared_state_t> shared;
    std::mutex frame_mutex;
    std::condition_variable frame_cv;
    size_t local_stride = 0;
    bool frame_ready = false;
    // Two distinct memory pools
    std::vector<uint8_t> buffer_a;
    std::vector<uint8_t> buffer_b;
    // Points to the buffer currently owned by fill_img
    std::vector<uint8_t> *front_buffer;
    // Points to the buffer currently being written by on_process
    std::vector<uint8_t> *back_buffer;

    stream_data_t():
        front_buffer(&buffer_a),
        back_buffer(&buffer_b) {}
  };

  struct dmabuf_format_info_t {
    int32_t format;
    uint64_t *modifiers;
    int n_modifiers;
  };

  struct pipewire_streaminfo_t {
    int pipewire_node = -1;
    int width = 0;
    int height = 0;
    int pos_x = 0;
    int pos_y = 0;
    std::string monitor_name;

    std::string to_display_name() {
      if (!monitor_name.empty()) {
        return std::format("n{}", monitor_name);
      }
      return std::format("p{},{},{},{}", pos_x, pos_y, width, height);
    }

    bool match_display_name(const std::string &display_name) const {
      // Empty display_name never matches
      if (display_name.empty()) {
        return false;
      }
      // Method 1: Display_name starts with 'n' match by monitor_name starting from pos 1
      if (display_name[0] == 'n') {
        return display_name.substr(1) == monitor_name;
      }
      // Method 2: Display_name starts with 'p' match by position+resolution starting from pos 1
      if (display_name[0] == 'p') {
        auto stringstream = std::stringstream(display_name.substr(1));
        std::string stringvalue;
        std::vector<int> values;
        constexpr char display_name_delimiter = ',';
        while (std::getline(stringstream, stringvalue, display_name_delimiter)) {
          if (std::ranges::all_of(stringvalue, ::isdigit)) {
            values.emplace_back(std::stoi(stringvalue));
          } else {
            BOOST_LOG(debug) << "[portalgrab] Failed to parse int value: '"sv << stringvalue << "'";
          }
        }
        // Check if the vector has 4 values (pos_x, pos_y, width, height) from display_names formatting
        if (values.size() != 4) {
          BOOST_LOG(debug) << "[portalgrab] Display name does not match expected format 'x,y,w,h': '"sv << display_name << "'";
          return false;
        }
        return pos_x == values[0] && pos_y == values[1] && width == values[2] && height == values[3];
      }
      // All matching methods have failed. No match!
      return false;
    }
  };

  class dbus_t {
  public:
    dbus_t &operator=(dbus_t &&) = delete;  // Do not allow to copying

    ~dbus_t() noexcept {
      try {
        if (conn && !session_handle.empty()) {
          g_autoptr(GError) err = nullptr;
          // This is a blocking C call; it won't throw, but we wrap for safety
          g_dbus_connection_call_sync(
            conn,
            "org.freedesktop.portal.Desktop",
            session_handle.c_str(),
            "org.freedesktop.portal.Session",
            "Close",
            nullptr,
            nullptr,
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            nullptr,
            &err
          );

          if (err) {
            BOOST_LOG(warning) << "[portalgrab] Failed to explicitly close portal session: "sv << err->message;
          } else {
            BOOST_LOG(debug) << "[portalgrab] Explicitly closed portal session: "sv << session_handle;
          }
        }
      } catch (const std::exception &e) {
        BOOST_LOG(error) << "[portalgrab] Standard exception caught in ~dbus_t: "sv << e.what();
      } catch (...) {
        BOOST_LOG(error) << "[portalgrab] Unknown exception caught in ~dbus_t"sv;
      }

      if (pipewire_fd >= 0) {
        close(pipewire_fd);
      }
      if (screencast_proxy) {
        g_clear_object(&screencast_proxy);
      }
      if (remote_desktop_proxy) {
        g_clear_object(&remote_desktop_proxy);
      }
      if (conn) {
        g_clear_object(&conn);
      }
    }

    int init() {
      restore_token_t::load();

      conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
      if (!conn) {
        return -1;
      }
      remote_desktop_proxy = g_dbus_proxy_new_sync(conn, G_DBUS_PROXY_FLAGS_NONE, nullptr, PORTAL_NAME, PORTAL_PATH, REMOTE_DESKTOP_IFACE, nullptr, nullptr);
      if (!remote_desktop_proxy) {
        return -1;
      }
      screencast_proxy = g_dbus_proxy_new_sync(conn, G_DBUS_PROXY_FLAGS_NONE, nullptr, PORTAL_NAME, PORTAL_PATH, SCREENCAST_IFACE, nullptr, nullptr);
      if (!screencast_proxy) {
        return -1;
      }

      return 0;
    }

    int connect_to_portal() {
      g_autoptr(GMainLoop) loop = g_main_loop_new(nullptr, FALSE);
      g_autofree gchar *session_path = nullptr;
      g_autofree gchar *session_token = nullptr;
      create_session_path(conn, nullptr, &session_token);

      // Try combined RemoteDesktop + ScreenCast session first
      bool use_screencast_only = !try_remote_desktop_session(loop, &session_path, session_token);

      // Fall back to ScreenCast-only if RemoteDesktop failed
      if (use_screencast_only && try_screencast_only_session(loop, &session_path) < 0) {
        return -1;
      }

      if (start_portal_session(loop, session_path, pipewire_streams, use_screencast_only) < 0) {
        return -1;
      }

      if (open_pipewire_remote(session_path, pipewire_fd) < 0) {
        return -1;
      }

      return 0;
    }

    // Try to create a combined RemoteDesktop + ScreenCast session
    // Returns true on success, false if should fall back to ScreenCast-only
    bool try_remote_desktop_session(GMainLoop *loop, gchar **session_path, const gchar *session_token) {
      if (create_portal_session(loop, session_path, session_token, false) < 0) {
        return false;
      }

      if (select_remote_desktop_devices(loop, *session_path) < 0) {
        BOOST_LOG(warning) << "[portalgrab] RemoteDesktop.SelectDevices failed, falling back to ScreenCast-only mode"sv;
        g_free(*session_path);
        *session_path = nullptr;
        return false;
      }

      if (select_screencast_sources(loop, *session_path, false) < 0) {
        BOOST_LOG(warning) << "[portalgrab] ScreenCast.SelectSources failed with RemoteDesktop session, trying ScreenCast-only mode"sv;
        g_free(*session_path);
        *session_path = nullptr;
        return false;
      }

      return true;
    }

    // Create a ScreenCast-only session
    int try_screencast_only_session(GMainLoop *loop, gchar **session_path) {
      g_autofree gchar *new_session_token = nullptr;
      create_session_path(conn, nullptr, &new_session_token);
      if (create_portal_session(loop, session_path, new_session_token, true) < 0) {
        return -1;
      }
      if (select_screencast_sources(loop, *session_path, true) < 0) {
        g_free(*session_path);
        *session_path = nullptr;
        return -1;
      }
      return 0;
    }

    bool is_session_closed() const {
      if (conn && !session_handle.empty()) {
        // Try to retrieve property org.freedesktop.portal.Session::version
        g_autoptr(GError) err = nullptr;
        g_dbus_connection_call_sync(
          conn,
          "org.freedesktop.portal.Desktop",
          session_handle.c_str(),
          "org.freedesktop.DBus.Properties",
          "Get",
          g_variant_new("(ss)", "org.freedesktop.portal.Session", "version"),
          G_VARIANT_TYPE("(v)"),
          G_DBUS_CALL_FLAGS_NONE,
          -1,
          nullptr,
          &err
        );
        // If we cannot get the property then the session portal was closed.
        if (err) {
          BOOST_LOG(debug) << "[portalgrab] Session closed as check failed: "sv << err->message;
          return true;
        }
      }
      // The session is not closed (or might not have been opened yet).
      return false;
    }

    std::vector<pipewire_streaminfo_t> pipewire_streams;
    int pipewire_fd;

  private:
    GDBusConnection *conn;
    GDBusProxy *screencast_proxy;
    GDBusProxy *remote_desktop_proxy;
    std::string session_handle;

    int create_portal_session(GMainLoop *loop, gchar **session_path_out, const gchar *session_token, bool use_screencast) {
      GDBusProxy *proxy = use_screencast ? screencast_proxy : remote_desktop_proxy;
      const char *session_type = use_screencast ? "ScreenCast" : "RemoteDesktop";

      dbus_response_t response = {
        nullptr,
      };
      g_autofree gchar *request_token = nullptr;
      create_request_path(conn, nullptr, &request_token);

      GVariantBuilder builder;
      g_variant_builder_init(&builder, G_VARIANT_TYPE("(a{sv})"));
      g_variant_builder_open(&builder, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&builder, "{sv}", "handle_token", g_variant_new_string(request_token));
      g_variant_builder_add(&builder, "{sv}", "session_handle_token", g_variant_new_string(session_token));
      g_variant_builder_close(&builder);

      g_autoptr(GError) err = nullptr;
      g_autoptr(GVariant) reply = g_dbus_proxy_call_sync(proxy, "CreateSession", g_variant_builder_end(&builder), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &err);

      if (err) {
        BOOST_LOG(error) << "[portalgrab] Could not create "sv << session_type << " session: "sv << err->message;
        return -1;
      }

      const gchar *request_path = nullptr;
      g_variant_get(reply, "(o)", &request_path);
      dbus_response_init(&response, loop, conn, request_path);

      g_autoptr(GVariant) create_response = dbus_response_wait(&response);

      if (!create_response) {
        BOOST_LOG(error) << "[portalgrab] " << session_type << " CreateSession: no response received"sv;
        return -1;
      }

      guint32 response_code;
      g_autoptr(GVariant) results = nullptr;
      g_variant_get(create_response, "(u@a{sv})", &response_code, &results);

      BOOST_LOG(debug) << "[portalgrab] " << session_type << " CreateSession response_code: "sv << response_code;

      if (response_code != 0) {
        BOOST_LOG(error) << "[portalgrab] " << session_type << " CreateSession failed with response code: "sv << response_code;
        return -1;
      }

      g_autoptr(GVariant) session_handle_v = g_variant_lookup_value(results, "session_handle", nullptr);
      if (!session_handle_v) {
        BOOST_LOG(error) << "[portalgrab] " << session_type << " CreateSession: session_handle not found in response"sv;
        return -1;
      }

      if (g_variant_is_of_type(session_handle_v, G_VARIANT_TYPE_VARIANT)) {
        g_autoptr(GVariant) inner = g_variant_get_variant(session_handle_v);
        *session_path_out = g_strdup(g_variant_get_string(inner, nullptr));
      } else {
        *session_path_out = g_strdup(g_variant_get_string(session_handle_v, nullptr));
      }

      BOOST_LOG(debug) << "[portalgrab] " << session_type << " CreateSession: got session handle: "sv << *session_path_out;
      // Save it for the destructor to use during cleanup
      this->session_handle = *session_path_out;
      return 0;
    }

    int select_remote_desktop_devices(GMainLoop *loop, const gchar *session_path) {
      dbus_response_t response = {
        nullptr,
      };
      g_autofree gchar *request_token = nullptr;
      create_request_path(conn, nullptr, &request_token);

      GVariantBuilder builder;
      g_variant_builder_init(&builder, G_VARIANT_TYPE("(oa{sv})"));
      g_variant_builder_add(&builder, "o", session_path);
      g_variant_builder_open(&builder, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&builder, "{sv}", "handle_token", g_variant_new_string(request_token));
      g_variant_builder_add(&builder, "{sv}", "types", g_variant_new_uint32(TYPE_KEYBOARD | TYPE_POINTER | TYPE_TOUCHSCREEN));
      g_variant_builder_add(&builder, "{sv}", "persist_mode", g_variant_new_uint32(PERSIST_UNTIL_REVOKED));
      if (!restore_token_t::empty()) {
        g_variant_builder_add(&builder, "{sv}", "restore_token", g_variant_new_string(restore_token_t::get().c_str()));
      }
      g_variant_builder_close(&builder);

      g_autoptr(GError) err = nullptr;
      g_autoptr(GVariant) reply = g_dbus_proxy_call_sync(remote_desktop_proxy, "SelectDevices", g_variant_builder_end(&builder), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &err);

      if (err) {
        BOOST_LOG(error) << "[portalgrab] Could not select devices: "sv << err->message;
        return -1;
      }

      const gchar *request_path = nullptr;
      g_variant_get(reply, "(o)", &request_path);
      dbus_response_init(&response, loop, conn, request_path);

      g_autoptr(GVariant) devices_response = dbus_response_wait(&response);

      if (!devices_response) {
        BOOST_LOG(error) << "[portalgrab] SelectDevices: no response received"sv;
        return -1;
      }

      guint32 response_code;
      g_variant_get(devices_response, "(u@a{sv})", &response_code, nullptr);
      BOOST_LOG(debug) << "[portalgrab] SelectDevices response_code: "sv << response_code;

      if (response_code != 0) {
        BOOST_LOG(error) << "[portalgrab] SelectDevices failed with response code: "sv << response_code;
        return -1;
      }

      return 0;
    }

    int select_screencast_sources(GMainLoop *loop, const gchar *session_path, bool persist) {
      dbus_response_t response = {
        nullptr,
      };
      g_autofree gchar *request_token = nullptr;
      create_request_path(conn, nullptr, &request_token);

      GVariantBuilder builder;
      g_variant_builder_init(&builder, G_VARIANT_TYPE("(oa{sv})"));
      g_variant_builder_add(&builder, "o", session_path);
      g_variant_builder_open(&builder, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&builder, "{sv}", "handle_token", g_variant_new_string(request_token));
      g_variant_builder_add(&builder, "{sv}", "types", g_variant_new_uint32(SOURCE_TYPE_MONITOR));
      g_variant_builder_add(&builder, "{sv}", "cursor_mode", g_variant_new_uint32(CURSOR_MODE_EMBEDDED));
      g_variant_builder_add(&builder, "{sv}", "multiple", g_variant_new_boolean(TRUE));
      if (persist) {
        g_variant_builder_add(&builder, "{sv}", "persist_mode", g_variant_new_uint32(PERSIST_UNTIL_REVOKED));
        if (!restore_token_t::empty()) {
          g_variant_builder_add(&builder, "{sv}", "restore_token", g_variant_new_string(restore_token_t::get().c_str()));
        }
      }
      g_variant_builder_close(&builder);

      g_autoptr(GError) err = nullptr;
      g_autoptr(GVariant) reply = g_dbus_proxy_call_sync(screencast_proxy, "SelectSources", g_variant_builder_end(&builder), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &err);
      if (err) {
        BOOST_LOG(error) << "[portalgrab] Could not select sources: "sv << err->message;
        return -1;
      }

      const gchar *request_path = nullptr;
      g_variant_get(reply, "(o)", &request_path);
      dbus_response_init(&response, loop, conn, request_path);

      g_autoptr(GVariant) sources_response = dbus_response_wait(&response);

      if (!sources_response) {
        BOOST_LOG(error) << "[portalgrab] SelectSources: no response received"sv;
        return -1;
      }

      guint32 response_code;
      g_variant_get(sources_response, "(u@a{sv})", &response_code, nullptr);
      BOOST_LOG(debug) << "[portalgrab] SelectSources response_code: "sv << response_code;

      if (response_code != 0) {
        BOOST_LOG(error) << "[portalgrab] SelectSources failed with response code: "sv << response_code;
        return -1;
      }

      return 0;
    }

    int start_portal_session(GMainLoop *loop, const gchar *session_path, std::vector<pipewire_streaminfo_t> &out_pipewire_streams, bool use_screencast) {
      GDBusProxy *proxy = use_screencast ? screencast_proxy : remote_desktop_proxy;
      const char *session_type = use_screencast ? "ScreenCast" : "RemoteDesktop";

      dbus_response_t response = {
        nullptr,
      };
      g_autofree gchar *request_token = nullptr;
      create_request_path(conn, nullptr, &request_token);

      GVariantBuilder builder;
      g_variant_builder_init(&builder, G_VARIANT_TYPE("(osa{sv})"));
      g_variant_builder_add(&builder, "o", session_path);
      g_variant_builder_add(&builder, "s", "");  // parent_window
      g_variant_builder_open(&builder, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&builder, "{sv}", "handle_token", g_variant_new_string(request_token));
      g_variant_builder_close(&builder);

      g_autoptr(GError) err = nullptr;
      g_autoptr(GVariant) reply = g_dbus_proxy_call_sync(proxy, "Start", g_variant_builder_end(&builder), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &err);
      if (err) {
        BOOST_LOG(error) << "[portalgrab] Could not start "sv << session_type << " session: "sv << err->message;
        return -1;
      }

      const gchar *request_path = nullptr;
      g_variant_get(reply, "(o)", &request_path);
      dbus_response_init(&response, loop, conn, request_path);

      g_autoptr(GVariant) start_response = dbus_response_wait(&response);

      if (!start_response) {
        BOOST_LOG(error) << "[portalgrab] " << session_type << " Start: no response received"sv;
        return -1;
      }

      guint32 response_code;
      g_autoptr(GVariant) dict = nullptr;
      g_autoptr(GVariant) streams = nullptr;
      g_variant_get(start_response, "(u@a{sv})", &response_code, &dict);

      BOOST_LOG(debug) << "[portalgrab] " << session_type << " Start response_code: "sv << response_code;

      if (response_code != 0) {
        BOOST_LOG(error) << "[portalgrab] " << session_type << " Start failed with response code: "sv << response_code;
        return -1;
      }

      streams = g_variant_lookup_value(dict, "streams", G_VARIANT_TYPE("a(ua{sv})"));
      if (!streams) {
        BOOST_LOG(error) << "[portalgrab] " << session_type << " Start: no streams in response"sv;
        return -1;
      }

      if (const gchar *new_token = nullptr; g_variant_lookup(dict, "restore_token", "s", &new_token) && new_token && new_token[0] != '\0' && restore_token_t::get() != new_token) {
        restore_token_t::set(new_token);
        restore_token_t::save();
      }

      GVariantIter iter;
      const auto wl_monitors = wl::monitors();
      int out_pipewire_node;
      g_autoptr(GVariant) value = nullptr;
      g_variant_iter_init(&iter, streams);
      while (g_variant_iter_next(&iter, "(u@a{sv})", &out_pipewire_node, &value)) {
        int out_width;
        int out_height;
        bool result = g_variant_lookup(value, "size", "(ii)", &out_width, &out_height, nullptr);
        if (!result) {
          BOOST_LOG(warning) << "[portalgrab] Ignoring stream without proper resolution on pipewire node "sv << out_pipewire_node;
          continue;
        }

        int out_pos_x;
        int out_pos_y;
        result = g_variant_lookup(value, "position", "(ii)", &out_pos_x, &out_pos_y, nullptr);
        if (!result) {
          BOOST_LOG(warning) << "[portalgrab] Falling back to position 0x0 for stream with resolution "sv << out_width << "x"sv << out_height << "on pipewire node "sv << out_pipewire_node;
          out_pos_x = 0;
          out_pos_y = 0;
        }
        auto stream = pipewire_streaminfo_t {out_pipewire_node, out_width, out_height, out_pos_x, out_pos_y};

        // Try to match the stream to a monitor_name by position/resolution and update stream info
        for (const auto &monitor : wl_monitors) {
          if (monitor->viewport.offset_x == out_pos_x && monitor->viewport.offset_y == out_pos_y && monitor->viewport.logical_width == out_width && monitor->viewport.logical_height == out_height) {
            stream.monitor_name = monitor->name;
            break;
          }
        }

        out_pipewire_streams.emplace_back(stream);
      }

      // The portal call returns the streams sorted by out_pipewire_node which can shuffle displays around, so
      // we have to sort pipewire streams by position here to be consistent
      std::ranges::sort(out_pipewire_streams, [](const auto &a, const auto &b) {
        return a.pos_x < b.pos_x || a.pos_y < b.pos_y;
      });

      return 0;
    }

    int open_pipewire_remote(const gchar *session_path, int &fd) {
      g_autoptr(GUnixFDList) fd_list = nullptr;
      g_autoptr(GVariant) msg = g_variant_ref_sink(g_variant_new("(oa{sv})", session_path, nullptr));

      g_autoptr(GError) err = nullptr;
      g_autoptr(GVariant) reply = g_dbus_proxy_call_with_unix_fd_list_sync(screencast_proxy, "OpenPipeWireRemote", msg, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &fd_list, nullptr, &err);
      if (err) {
        BOOST_LOG(error) << "[portalgrab] Could not open pipewire remote: "sv << err->message;
        return -1;
      }

      int fd_handle;
      g_variant_get(reply, "(h)", &fd_handle);
      fd = g_unix_fd_list_get(fd_list, fd_handle, nullptr);
      return 0;
    }

    static void on_response_received_cb([[maybe_unused]] GDBusConnection *connection, [[maybe_unused]] const gchar *sender_name, [[maybe_unused]] const gchar *object_path, [[maybe_unused]] const gchar *interface_name, [[maybe_unused]] const gchar *signal_name, GVariant *parameters, gpointer user_data) {
      auto *response = static_cast<dbus_response_t *>(user_data);
      response->response = g_variant_ref_sink(parameters);
      g_main_loop_quit(response->loop);
    }

    static gchar *get_sender_string(GDBusConnection *conn) {
      gchar *sender = g_strdup(g_dbus_connection_get_unique_name(conn) + 1);
      gchar *dot;
      while ((dot = strstr(sender, ".")) != nullptr) {
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
        *out_path = g_strdup(std::format("{}{}{}{}", REQUEST_PREFIX, sender, "/Sunshine", request_count).c_str());
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
        *out_path = g_strdup(std::format("{}{}{}{}", SESSION_PREFIX, sender, "/Sunshine", session_count).c_str());
      }
    }

    static void dbus_response_init(struct dbus_response_t *response, GMainLoop *loop, GDBusConnection *conn, const char *request_path) {
      response->loop = loop;
      response->subscription_id = g_dbus_connection_signal_subscribe(conn, PORTAL_NAME, REQUEST_IFACE, "Response", request_path, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_response_received_cb, response, nullptr);
    }

    static GVariant *dbus_response_wait(struct dbus_response_t *response) {
      g_main_loop_run(response->loop);
      return response->response;
    }
  };

  /**
   * @brief Singleton for portalgrab stuff persistent during an application run.
   *
   */
  class runtime_t {
  public:
    static runtime_t &instance();

    bool is_maxframerate_failed() const {
      return maxframerate_failed_;
    }

    void set_maxframerate_failed() {
      maxframerate_failed_ = true;
    }

    bool is_portal_secured() const {
      return is_portal_secured_;
    }

    void finalize_portal_security() {
#if !defined(__FreeBSD__)
      BOOST_LOG(debug) << "[portalgrab] Finalizing Portal security: dropping capabilities and resetting dumpable"sv;

      cap_t caps = cap_get_proc();
      if (!caps) {
        BOOST_LOG(error) << "[portalgrab] Failed to get process capabilities"sv;
        return;
      }

      std::array<cap_value_t, 2> effective_list {CAP_SYS_ADMIN, CAP_SYS_NICE};
      std::array<cap_value_t, 2> permitted_list {CAP_SYS_ADMIN, CAP_SYS_NICE};

      cap_set_flag(caps, CAP_EFFECTIVE, effective_list.size(), effective_list.data(), CAP_CLEAR);
      cap_set_flag(caps, CAP_PERMITTED, permitted_list.size(), permitted_list.data(), CAP_CLEAR);

      if (cap_set_proc(caps) != 0) {
        BOOST_LOG(error) << "[portalgrab] Failed to prune capabilities: "sv << std::strerror(errno);
      }
      cap_free(caps);

      // Reset dumpable AFTER the caps have been pruned to ensure the Portal can
      // access /proc/pid/root.
      if (prctl(PR_SET_DUMPABLE, 1) != 0) {
        BOOST_LOG(error) << "[portalgrab] Failed to set PR_SET_DUMPABLE: "sv << std::strerror(errno);
      }
#endif
      is_portal_secured_ = true;
    }

  private:
    runtime_t() = default;

    // Prevent copying
    runtime_t(const runtime_t &) = delete;
    runtime_t &operator=(const runtime_t &) = delete;

    bool maxframerate_failed_ = false;
    bool is_portal_secured_ = false;
  };

  runtime_t &runtime_t::instance() {
    alignas(runtime_t) static std::array<std::byte, sizeof(runtime_t)> storage;
    static auto instance_ = new (storage.data()) runtime_t();
    return *instance_;
  }

  class pipewire_t {
  public:
    pipewire_t():
        loop(pw_thread_loop_new("Pipewire thread", nullptr)) {
      BOOST_LOG(debug) << "[portalgrab] Start PW thread loop"sv;
      pw_thread_loop_start(loop);
    }

    ~pipewire_t() {
      BOOST_LOG(debug) << "[portalgrab] Destroying pipewire_t"sv;
      if (loop) {
        BOOST_LOG(debug) << "[portalgrab] Stop PW thread loop"sv;
        pw_thread_loop_stop(loop);
      }
      try {
        cleanup_stream();
      } catch (const std::exception &e) {
        BOOST_LOG(error) << "[portalgrab] Standard exception caught in ~pipewire_t: "sv << e.what();
      } catch (...) {
        BOOST_LOG(error) << "[portalgrab] Unknown exception caught in ~pipewire_t"sv;
      }

      pw_thread_loop_lock(loop);

      if (core) {
        BOOST_LOG(debug) << "[portalgrab] Disconnect PW core"sv;
        pw_core_disconnect(core);
        core = nullptr;
      }
      if (context) {
        BOOST_LOG(debug) << "[portalgrab] Destroy PW context"sv;
        pw_context_destroy(context);
        context = nullptr;
      }

      pw_thread_loop_unlock(loop);

      if (fd >= 0) {
        BOOST_LOG(debug) << "[portalgrab] Close pipewire_fd"sv;
        close(fd);
      }
      BOOST_LOG(debug) << "[portalgrab] Stop PW thread loop"sv;
      pw_thread_loop_stop(loop);
      BOOST_LOG(debug) << "[portalgrab] Destroy PW thread loop"sv;
      pw_thread_loop_destroy(loop);
    }

    std::mutex &frame_mutex() {
      return stream_data.frame_mutex;
    }

    std::condition_variable &frame_cv() {
      return stream_data.frame_cv;
    }

    bool is_frame_ready() const {
      return stream_data.frame_ready;
    }

    void set_frame_ready(bool ready) {
      stream_data.frame_ready = ready;
    }

    int init(int stream_fd, int stream_node, std::shared_ptr<shared_state_t> shared_state) {
      fd = stream_fd;
      node = stream_node;
      stream_data.shared = std::move(shared_state);

      pw_thread_loop_lock(loop);
      BOOST_LOG(debug) << "[portalgrab] Setup PW context"sv;
      context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
      if (context) {
        BOOST_LOG(debug) << "[portalgrab] Connect PW context to fd"sv;
        core = pw_context_connect_fd(context, fd, nullptr, 0);
        if (core) {
          pw_core_add_listener(core, &core_listener, &core_events, nullptr);
        } else {
          BOOST_LOG(debug) << "[portalgrab] Failed to connect to PW core. Error: "sv << errno << "(" << strerror(errno) << ")"sv;
          return -1;
        }
      } else {
        BOOST_LOG(debug) << "[portalgrab] Failed to setup PW context. Error: "sv << errno << "(" << strerror(errno) << ")"sv;
        return -1;
      }

      pw_thread_loop_unlock(loop);
      return 0;
    }

    void cleanup_stream() {
      BOOST_LOG(debug) << "[portalgrab] Cleaning up stream"sv;
      if (loop && stream_data.stream) {
        pw_thread_loop_lock(loop);

        // 1. Lock the frame mutex to stop fill_img
        BOOST_LOG(debug) << "[portalgrab] Stop fill_img"sv;
        {
          std::scoped_lock lock(stream_data.frame_mutex);
          stream_data.frame_ready = false;
          stream_data.current_buffer = nullptr;
        }

        if (stream_data.stream) {
          BOOST_LOG(debug) << "[portalgrab] Disconnect stream"sv;
          pw_stream_disconnect(stream_data.stream);
          BOOST_LOG(debug) << "[portalgrab] Destroy stream"sv;
          pw_stream_destroy(stream_data.stream);
          stream_data.stream = nullptr;
        }

        pw_thread_loop_unlock(loop);
      }
    }

    int ensure_stream(const platf::mem_type_e mem_type, const uint32_t width, const uint32_t height, const uint32_t refresh_rate, const struct dmabuf_format_info_t *dmabuf_infos, const int n_dmabuf_infos, const bool display_is_nvidia) {
      pw_thread_loop_lock(loop);
      if (!stream_data.stream) {
        if (!core) {
          BOOST_LOG(debug) << "[portalgrab] PW core not available. Cannot ensure stream."sv;
          pw_thread_loop_unlock(loop);
          return -1;
        }

        struct pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr);

        BOOST_LOG(debug) << "[portalgrab] Create PW stream"sv;
        stream_data.stream = pw_stream_new(core, "Sunshine Video Capture", props);
        pw_stream_add_listener(stream_data.stream, &stream_data.stream_listener, &stream_events, &stream_data);

        std::array<uint8_t, SPA_POD_BUFFER_SIZE> buffer;
        struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buffer.data(), buffer.size());

        int n_params = 0;
        std::array<const struct spa_pod *, MAX_PARAMS> params;

        // Add preferred parameters for DMA-BUF with modifiers
        // Use DMA-BUF for VAAPI, or for CUDA when the display GPU is NVIDIA (pure NVIDIA system).
        // On hybrid GPU systems (Intel+NVIDIA), DMA-BUFs come from the Intel GPU and cannot
        // be imported into CUDA, so we fall back to memory buffers in that case.
        bool use_dmabuf = n_dmabuf_infos > 0 && (mem_type == platf::mem_type_e::vaapi ||
                                                 mem_type == platf::mem_type_e::vulkan ||
                                                 (mem_type == platf::mem_type_e::cuda && display_is_nvidia));
        if (use_dmabuf) {
          for (int i = 0; i < n_dmabuf_infos; i++) {
            auto format_param = build_format_parameter(&pod_builder, width, height, refresh_rate, dmabuf_infos[i].format, dmabuf_infos[i].modifiers, dmabuf_infos[i].n_modifiers);
            params[n_params] = format_param;
            n_params++;
          }
        }

        // Add fallback for memptr
        for (const auto &fmt : format_map) {
          if (fmt.fourcc == 0) {
            break;
          }
          auto format_param = build_format_parameter(&pod_builder, width, height, refresh_rate, fmt.pw_format, nullptr, 0);
          params[n_params] = format_param;
          n_params++;
        }
        BOOST_LOG(debug) << "[portalgrab] Connect PW stream - fd "sv << fd << " node "sv << node;
        pw_stream_connect(stream_data.stream, PW_DIRECTION_INPUT, node, (enum pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params.data(), n_params);
      }
      pw_thread_loop_unlock(loop);
      return 0;
    }

    static void close_img_fds(egl::img_descriptor_t *img_descriptor) {
      for (int &fd : img_descriptor->sd.fds) {
        if (fd >= 0) {
          close(fd);
          fd = -1;
        }
      }
    }

    static void fill_img_metadata(egl::img_descriptor_t *img_descriptor, struct spa_buffer *buf) {
      img_descriptor->frame_timestamp = std::chrono::steady_clock::now();

      struct spa_meta_header *h = static_cast<struct spa_meta_header *>(
        spa_buffer_find_meta_data(buf, SPA_META_Header, sizeof(*h))
      );
      if (h) {
        img_descriptor->seq = h->seq;
        img_descriptor->pts = h->pts;
      }

      if (buf->n_datas > 0) {
        img_descriptor->pw_flags = buf->datas[0].chunk->flags;
      }

      struct spa_meta_region *damage = static_cast<struct spa_meta_region *>(
        spa_buffer_find_meta_data(buf, SPA_META_VideoDamage, sizeof(*damage))
      );
      img_descriptor->pw_damage = (damage && damage->region.size.width > 0 && damage->region.size.height > 0) ? std::optional<bool>(true) : std::nullopt;
    }

    static void fill_img_dmabuf(egl::img_descriptor_t *img_descriptor, struct spa_buffer *buf, const stream_data_t &d) {
      img_descriptor->sd.width = d.format.info.raw.size.width;
      img_descriptor->sd.height = d.format.info.raw.size.height;
      img_descriptor->sd.modifier = d.format.info.raw.modifier;
      img_descriptor->sd.fourcc = d.drm_format;
      for (int i = 0; i < MIN(buf->n_datas, 4); i++) {
        img_descriptor->sd.fds[i] = dup(buf->datas[i].fd);
        img_descriptor->sd.pitches[i] = buf->datas[i].chunk->stride;
        img_descriptor->sd.offsets[i] = buf->datas[i].chunk->offset;
      }
    }

    void fill_img(platf::img_t *img) {
      pw_thread_loop_lock(loop);
      std::scoped_lock lock(stream_data.frame_mutex);

      if (stream_data.shared && stream_data.shared->stream_dead.load()) {
        img->data = nullptr;
        close_img_fds(static_cast<egl::img_descriptor_t *>(img));
        pw_thread_loop_unlock(loop);
        return;
      }

      if (!stream_data.current_buffer) {
        img->data = nullptr;
        pw_thread_loop_unlock(loop);
        return;
      }

      struct spa_buffer *buf = stream_data.current_buffer->buffer;
      if (buf->datas[0].chunk->size != 0) {
        auto *img_descriptor = static_cast<egl::img_descriptor_t *>(img);
        fill_img_metadata(img_descriptor, buf);
        if (buf->datas[0].type == SPA_DATA_DmaBuf) {
          fill_img_dmabuf(img_descriptor, buf, stream_data);
        } else {
          img->data = stream_data.front_buffer->data();
          img->row_pitch = stream_data.local_stride;
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
      struct spa_pod_frame object_frame;
      struct spa_pod_frame modifier_frame;
      std::array<struct spa_rectangle, 3> sizes;
      std::array<struct spa_fraction, 3> framerates;

      sizes[0] = SPA_RECTANGLE(width, height);  // Preferred
      sizes[1] = SPA_RECTANGLE(1, 1);
      sizes[2] = SPA_RECTANGLE(8192, 4096);

      framerates[0] = SPA_FRACTION(0, 1);  // default; we only want variable rate, thus bypassing compositor pacing
      framerates[1] = SPA_FRACTION(0, 1);  // min
      framerates[2] = SPA_FRACTION(0, 1);  // max

      spa_pod_builder_push_object(b, &object_frame, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
      spa_pod_builder_add(b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
      spa_pod_builder_add(b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
      spa_pod_builder_add(b, SPA_FORMAT_VIDEO_format, SPA_POD_Id(format), 0);
      spa_pod_builder_add(b, SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&sizes[0], &sizes[1], &sizes[2]), 0);
      spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&framerates[0]), 0);
      if (!runtime_t::instance().is_maxframerate_failed()) {
        spa_pod_builder_add(b, SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction(&framerates[0], &framerates[1], &framerates[2]), 0);
      }

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

      return static_cast<struct spa_pod *>(spa_pod_builder_pop(b, &object_frame));
    }

    static void on_core_info_cb([[maybe_unused]] void *user_data, const struct pw_core_info *pw_info) {
      BOOST_LOG(info) << "[portalgrab] Connected to pipewire version "sv << pw_info->version;
    }

    static void on_core_error_cb([[maybe_unused]] void *user_data, const uint32_t id, const int seq, [[maybe_unused]] int res, const char *message) {
      BOOST_LOG(info) << "[portalgrab] Pipewire Error, id:"sv << id << " seq:"sv << seq << " message: "sv << message;
    }

    constexpr static const struct pw_core_events core_events = {
      .version = PW_VERSION_CORE_EVENTS,
      .info = on_core_info_cb,
      .error = on_core_error_cb,
    };

    static void on_stream_state_changed(void *user_data, enum pw_stream_state old, enum pw_stream_state state, const char *err_msg) {
      BOOST_LOG(debug) << "[portalgrab] PipeWire stream state: " << pw_stream_state_as_string(old)
                       << " -> " << pw_stream_state_as_string(state);

      auto *d = static_cast<stream_data_t *>(user_data);

      switch (state) {
        case PW_STREAM_STATE_PAUSED:
          if (d->shared && old == PW_STREAM_STATE_STREAMING) {
            {
              std::scoped_lock lock(d->frame_mutex);
              d->frame_ready = false;
              d->current_buffer = nullptr;
              d->shared->stream_dead.store(true, std::memory_order_relaxed);
            }
            d->frame_cv.notify_all();
          }
          break;
        case PW_STREAM_STATE_ERROR:
          if (old != PW_STREAM_STATE_STREAMING && !runtime_t::instance().is_maxframerate_failed()) {
            BOOST_LOG(warning) << "[portalgrab] Negotiation failed, will retry without maxFramerate"sv;
            runtime_t::instance().set_maxframerate_failed();
          }
          [[fallthrough]];
        case PW_STREAM_STATE_UNCONNECTED:
          if (d->shared) {
            d->shared->stream_dead.store(true, std::memory_order_relaxed);
            d->frame_cv.notify_all();
          }
          break;
        default:
          break;
      }
    }

    static void on_process(void *user_data) {
      const auto d = static_cast<struct stream_data_t *>(user_data);
      struct pw_buffer *b = nullptr;

      // 1. Drain the queue: Always grab the most recent buffer
      while (struct pw_buffer *aux = pw_stream_dequeue_buffer(d->stream)) {
        if (b) {
          pw_stream_queue_buffer(d->stream, b);  // Return the older, unused buffer
        }
        b = aux;
      }

      if (!b) {
        return;
      }

      // 2. Fast Path: DMA-BUF
      if (b->buffer->datas[0].type == SPA_DATA_DmaBuf) {
        std::scoped_lock lock(d->frame_mutex);
        if (d->current_buffer) {
          pw_stream_queue_buffer(d->stream, d->current_buffer);
        }
        d->current_buffer = b;
        d->frame_ready = true;
      }
      // 3. Optimized Path: Software/MemPtr
      else if (b->buffer->datas[0].data != nullptr) {
        size_t size = b->buffer->datas[0].chunk->size;

        // Perform the copy to the BACK buffer while NOT holding the lock
        if (d->back_buffer->size() < size) {
          d->back_buffer->resize(size);
        }
        std::memcpy(d->back_buffer->data(), b->buffer->datas[0].data, size);

        {
          // Lock only for the pointer swap and state update
          std::scoped_lock lock(d->frame_mutex);
          std::swap(d->front_buffer, d->back_buffer);

          d->local_stride = b->buffer->datas[0].chunk->stride;
          d->frame_ready = true;
          d->current_buffer = b;
        }

        // Release the PW buffer immediately after copy
        pw_stream_queue_buffer(d->stream, b);
      }

      d->frame_cv.notify_one();
    }

    static void on_param_changed(void *user_data, uint32_t id, const struct spa_pod *param) {
      const auto d = static_cast<struct stream_data_t *>(user_data);

      d->current_buffer = nullptr;

      if (param == nullptr || id != SPA_PARAM_Format) {
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

      BOOST_LOG(info) << "[portalgrab] Video format: "sv << d->format.info.raw.format;
      BOOST_LOG(info) << "[portalgrab] Size: "sv << d->format.info.raw.size.width << "x"sv << d->format.info.raw.size.height;
      if (d->format.info.raw.max_framerate.num == 0 && d->format.info.raw.max_framerate.denom == 1) {
        BOOST_LOG(info) << "[portalgrab] Framerate (from compositor): 0/1 (variable rate capture)";
      } else {
        BOOST_LOG(info) << "[portalgrab] Framerate (from compositor): "sv << d->format.info.raw.framerate.num << "/"sv << d->format.info.raw.framerate.denom;
        BOOST_LOG(info) << "[portalgrab] Framerate (from compositor, max): "sv << d->format.info.raw.max_framerate.num << "/"sv << d->format.info.raw.max_framerate.denom;
      }

      int physical_w = d->format.info.raw.size.width;
      int physical_h = d->format.info.raw.size.height;

      if (d->shared) {
        int old_w = d->shared->negotiated_width.load(std::memory_order_relaxed);
        int old_h = d->shared->negotiated_height.load(std::memory_order_relaxed);

        if (physical_w != old_w || physical_h != old_h) {
          d->shared->negotiated_width.store(physical_w, std::memory_order_relaxed);
          d->shared->negotiated_height.store(physical_h, std::memory_order_relaxed);
        }
      }

      uint64_t drm_format = 0;
      for (const auto &fmt : format_map) {
        if (fmt.fourcc == 0) {
          break;
        }
        if (fmt.pw_format == d->format.info.raw.format) {
          drm_format = fmt.fourcc;
        }
      }
      d->drm_format = drm_format;

      uint32_t buffer_types = 0;
      if (spa_pod_find_prop(param, nullptr, SPA_FORMAT_VIDEO_modifier) != nullptr && d->drm_format) {
        BOOST_LOG(info) << "[portalgrab] using DMA-BUF buffers"sv;
        buffer_types |= 1 << SPA_DATA_DmaBuf;
      } else {
        BOOST_LOG(info) << "[portalgrab] using memory buffers"sv;
        buffer_types |= 1 << SPA_DATA_MemPtr;
      }

      // Ack the buffer type and metadata
      std::array<uint8_t, SPA_POD_BUFFER_SIZE> buffer;
      std::array<const struct spa_pod *, 3> params;
      int n_params = 0;
      struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buffer.data(), buffer.size());
      auto buffer_param = static_cast<const struct spa_pod *>(spa_pod_builder_add_object(&pod_builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_dataType, SPA_POD_Int(buffer_types)));
      params[n_params] = buffer_param;
      n_params++;
      auto meta_param = static_cast<const struct spa_pod *>(spa_pod_builder_add_object(&pod_builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta, SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header), SPA_PARAM_META_size, SPA_POD_Int(sizeof(struct spa_meta_header))));
      params[n_params] = meta_param;
      n_params++;
      int videoDamageRegionCount = 16;
      auto damage_param = static_cast<const struct spa_pod *>(spa_pod_builder_add_object(&pod_builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta, SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoDamage), SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int(sizeof(struct spa_meta_region) * videoDamageRegionCount, sizeof(struct spa_meta_region) * 1, sizeof(struct spa_meta_region) * videoDamageRegionCount)));
      params[n_params] = damage_param;
      n_params++;

      pw_stream_update_params(d->stream, params.data(), n_params);
    }

    constexpr static const struct pw_stream_events stream_events = {
      .version = PW_VERSION_STREAM_EVENTS,
      .state_changed = on_stream_state_changed,
      .param_changed = on_param_changed,
      .process = on_process,
    };
  };

  class portal_t: public platf::display_t {
  public:
    int init(platf::mem_type_e hwdevice_type, const std::string &display_name, const ::video::config_t &config) {
      // calculate frame interval we should capture at
      framerate = config.framerate;
      if (config.framerateX100 > 0) {
        AVRational fps_strict = ::video::framerateX100_to_rational(config.framerateX100);
        delay = std::chrono::nanoseconds(
          (static_cast<int64_t>(fps_strict.den) * 1'000'000'000LL) / fps_strict.num
        );
        BOOST_LOG(info) << "[portalgrab] Requested frame rate [" << fps_strict.num << "/" << fps_strict.den << ", approx. " << av_q2d(fps_strict) << " fps]";
      } else {
        delay = std::chrono::nanoseconds {1s} / framerate;
        BOOST_LOG(info) << "[portalgrab] Requested frame rate [" << framerate << "fps]";
      }
      mem_type = hwdevice_type;

      if (get_dmabuf_modifiers() < 0) {
        return -1;
      }

      // Connect DBus portal session
      if (dbus.init() < 0) {
        BOOST_LOG(error) << "[portalgrab] Failed to connect to dbus. portal_t::init() failed.";
        return -1;
      }
      if (dbus.connect_to_portal() < 0) {
        BOOST_LOG(error) << "[portalgrab] Failed to connect to portal. portal_t::init() failed.";
        return -1;
      }

      // Match display_name to a stream from the pipewire_streams vector
      bool use_fallback = true;
      pipewire_streaminfo_t stream;
      auto streams = dbus.pipewire_streams;
      if (streams.empty()) {
        BOOST_LOG(error) << "[portalgrab] No streams found on portal. portal_t::init() failed.";
        return -1;
      }
      for (const auto &stream_ : streams) {
        if (stream_.match_display_name(display_name)) {
          stream = stream_;
          use_fallback = false;
          break;
        }
      }
      // Fall back to first stream if we cannot match the given display_name to a stream in currently available streams.
      if (use_fallback) {
        BOOST_LOG(info) << "[portalgrab] Using first available stream as no matching stream was found for: '"sv << display_name << "'";
        stream = dbus.pipewire_streams.at(0);
      }
      // Set values inherited from display_t
      width = stream.width;
      height = stream.height;
      BOOST_LOG(info) << "[portalgrab] Streaming display '"sv << stream.monitor_name << "' from position: "sv << stream.pos_x << "x"sv << stream.pos_y << " resolution: "sv << width << "x"sv << height;

      framerate = config.framerate;

      if (!shared_state) {
        shared_state = std::make_shared<shared_state_t>();
      } else {
        shared_state->stream_dead.store(false);
        shared_state->negotiated_width.store(0);
        shared_state->negotiated_height.store(0);
      }
      if (pipewire.init(dbus.pipewire_fd, stream.pipewire_node, shared_state) < 0) {
        BOOST_LOG(error) << "[portalgrab] Failed to init pipewire. portal_t::init() failed.";
        return -1;
      }

      // Start PipeWire now so format negotiation can proceed before capture start
      if (pipewire.ensure_stream(mem_type, width, height, framerate, dmabuf_infos.data(), n_dmabuf_infos, display_is_nvidia) < 0) {
        BOOST_LOG(error) << "[portalgrab] Failed to ensure pipewire stream. portal_t::init() failed.";
        return -1;
      }

      int timeout_ms = 1500;
      int negotiated_w = 0;
      int negotiated_h = 0;

      while (timeout_ms > 0) {
        negotiated_w = shared_state->negotiated_width.load();
        negotiated_h = shared_state->negotiated_height.load();
        if (negotiated_w > 0 && negotiated_h > 0) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        timeout_ms -= 10;
      }

      // Set width and height to the values negotiated by pipewire
      if (negotiated_w > 0 && negotiated_h > 0 && (negotiated_w != width || negotiated_h != height)) {
        BOOST_LOG(info) << "[portalgrab] Using negotiated resolution "sv
                        << negotiated_w << "x" << negotiated_h;

        width = negotiated_w;
        height = negotiated_h;
      }

      // Set env dimensions to match the captured display.
      // Portal captures a single display, so the environment size equals the capture size.
      // Without this, touch input is silently dropped because touch_port_t::operator bool()
      // checks env_width and env_height are non-zero.
      env_width = width;
      env_height = height;

      return 0;
    }

    platf::capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool show_cursor) {
      // FIXME: show_cursor is ignored
      auto deadline = std::chrono::steady_clock::now() + timeout;
      int retries = 0;

      while (std::chrono::steady_clock::now() < deadline) {
        if (!wait_for_frame(deadline)) {
          return platf::capture_e::timeout;
        }

        if (!pull_free_image_cb(img_out)) {
          return platf::capture_e::interrupted;
        }

        auto *img_egl = static_cast<egl::img_descriptor_t *>(img_out.get());
        img_egl->reset();
        pipewire.fill_img(img_egl);

        // Check if we got valid data (either DMA-BUF fd or memory pointer), then filter duplicates
        const bool has_valid_data = img_egl->sd.fds[0] >= 0 || img_egl->data != nullptr;
        const bool redundant_frame = has_valid_data && is_buffer_redundant(img_egl);
        if (has_valid_data && !redundant_frame) {
          // Update frame metadata
          update_metadata(img_egl, retries);
          return platf::capture_e::ok;
        }

        if (redundant_frame) {
          ++redundant_drop_events_total;
        } else if (!has_valid_data) {
          ++invalid_frame_events_total;
        }

        // No valid frame yet, or it was a duplicate
        retries++;
      }
      return platf::capture_e::timeout;
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
      auto capture_stats_window_start = next_frame;
      auto next_capture_stats_log = next_frame + 5s;
      std::uint64_t capture_attempts = 0;
      std::uint64_t capture_ok = 0;
      std::uint64_t capture_timeout = 0;
      std::uint64_t last_redundant_drop_events = redundant_drop_events_total;
      std::uint64_t last_redundant_retry_events = redundant_retry_events_total;
      std::uint64_t last_invalid_frame_events = invalid_frame_events_total;

      if (pipewire.ensure_stream(mem_type, width, height, framerate, dmabuf_infos.data(), n_dmabuf_infos, display_is_nvidia) < 0) {
        BOOST_LOG(error) << "[portalgrab] Failed to ensure pipewire stream. capture() failed with error.";
        return platf::capture_e::error;
      }
      sleep_overshoot_logger.reset();

      while (true) {
        ++capture_attempts;
        // Check if PipeWire signaled a dead stream
        if (shared_state->stream_dead.exchange(false)) {
          // If the pipewire stream stopped due to closed portal session stop the capture with an error
          if (dbus.is_session_closed()) {
            BOOST_LOG(warning) << "[portalgrab] PipeWire stream stopped by closed portal session."sv;
            pipewire.frame_cv().notify_all();
            return platf::capture_e::error;
          }
          // Re-init the capture if the stream is dead for any other reason
          BOOST_LOG(warning) << "[portalgrab] PipeWire stream disconnected. Forcing session reset."sv;
          return platf::capture_e::reinit;
        }

        // Advance to (or catch up with) next delay interval
        auto now = std::chrono::steady_clock::now();
        while (next_frame < now) {
          next_frame += delay;
        }

        if (next_frame > now) {
          std::this_thread::sleep_until(next_frame);
          sleep_overshoot_logger.first_point(next_frame);
          sleep_overshoot_logger.second_point_now_and_log();
        }

        std::shared_ptr<platf::img_t> img_out;
        switch (const auto status = snapshot(pull_free_image_cb, img_out, 1000ms, *cursor)) {
          case platf::capture_e::reinit:
          case platf::capture_e::error:
          case platf::capture_e::interrupted:
            pipewire.frame_cv().notify_all();
            return status;
          case platf::capture_e::timeout:
            ++capture_timeout;
            if (!pull_free_image_cb(img_out)) {
              // Detect if shutdown is pending
              BOOST_LOG(debug) << "[portalgrab] PipeWire: timeout -> interrupt nudge";
              pipewire.frame_cv().notify_all();
              return platf::capture_e::interrupted;
            }
            if (!push_captured_image_cb(std::move(img_out), false)) {
              BOOST_LOG(debug) << "[portalgrab] PipeWire: !push_captured_image_cb -> ok";
              return platf::capture_e::ok;
            }
            break;
          case platf::capture_e::ok:
            ++capture_ok;
            if (!push_captured_image_cb(std::move(img_out), true)) {
              BOOST_LOG(debug) << "[portalgrab] PipeWire: !push_captured_image_cb -> ok";
              return platf::capture_e::ok;
            }
            break;
          default:
            BOOST_LOG(error) << "[portalgrab] Unrecognized capture status ["sv << std::to_underlying(status) << ']';
            return status;
        }

        auto stats_now = std::chrono::steady_clock::now();
        if (stats_now >= next_capture_stats_log) {
          const auto window_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stats_now - capture_stats_window_start).count();
          const auto effective_fps = window_ns > 0 ? (double) capture_ok * 1'000'000'000.0 / (double) window_ns : 0.0;
          const auto redundant_drop_delta = redundant_drop_events_total - last_redundant_drop_events;
          const auto redundant_retry_delta = redundant_retry_events_total - last_redundant_retry_events;
          const auto invalid_frame_delta = invalid_frame_events_total - last_invalid_frame_events;

          BOOST_LOG(debug)
            << "[portalgrab] capture stats mode="sv << (display_is_nvidia && n_dmabuf_infos > 0 ? "dmabuf" : "mem")
            << " nvidia="sv << display_is_nvidia
            << " attempts="sv << capture_attempts
            << " ok="sv << capture_ok
            << " timeout="sv << capture_timeout
            << " effective_fps="sv << effective_fps
            << " redundant_drop_delta="sv << redundant_drop_delta
            << " redundant_retry_delta="sv << redundant_retry_delta
            << " invalid_frame_delta="sv << invalid_frame_delta;

          capture_stats_window_start = stats_now;
          next_capture_stats_log = stats_now + 5s;
          capture_attempts = 0;
          capture_ok = 0;
          capture_timeout = 0;
          last_redundant_drop_events = redundant_drop_events_total;
          last_redundant_retry_events = redundant_retry_events_total;
          last_invalid_frame_events = invalid_frame_events_total;
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

#ifdef SUNSHINE_BUILD_VULKAN
      if (mem_type == platf::mem_type_e::vulkan && n_dmabuf_infos > 0) {
        return vk::make_avcodec_encode_device_vram(width, height, 0, 0);
      }
#endif

#ifdef SUNSHINE_BUILD_CUDA
      if (mem_type == platf::mem_type_e::cuda) {
        if (display_is_nvidia && n_dmabuf_infos > 0) {
          // Display GPU is NVIDIA - can use DMA-BUF directly
          return cuda::make_avcodec_gl_encode_device(width, height, 0, 0);
        } else {
          // Hybrid system (Intel display + NVIDIA encode) - use memory buffer path
          // DMA-BUFs from Intel GPU cannot be imported into CUDA
          return cuda::make_avcodec_encode_device(width, height, false);
        }
      }
#endif

      return std::make_unique<platf::avcodec_encode_device_t>();
    }

    int dummy_img(platf::img_t *img) override {
      if (!img) {
        return -1;
      }

      img->data = new std::uint8_t[img->height * img->row_pitch];
      std::fill_n(img->data, img->height * img->row_pitch, 0);
      return 0;
    }

    // This capture method is event driven; don't insert duplicate frames
    bool is_event_driven() override {
      return true;
    }

  private:
    bool is_buffer_redundant(const egl::img_descriptor_t *img) {
      // Check for corrupted frame
      if (img->pw_flags.has_value() && (img->pw_flags.value() & SPA_CHUNK_FLAG_CORRUPTED)) {
        return true;
      }

      // If PTS is identical, only drop if damage metadata confirms no change
      if (img->pts.has_value() && last_pts.has_value() && img->pts.value() == last_pts.value()) {
        return img->pw_damage.has_value() && !img->pw_damage.value();
      }

      return false;
    }

    void update_metadata(egl::img_descriptor_t *img, int retries) {
      last_seq = img->seq;
      last_pts = img->pts;
      img->sequence = ++sequence;
      redundant_retry_events_total += retries;

      if (retries > 0) {
        BOOST_LOG(debug) << "[portalgrab] Processed frame after " << retries << " redundant events."sv;
      }
    }

    bool wait_for_frame(std::chrono::steady_clock::time_point deadline) {
      std::unique_lock<std::mutex> lock(pipewire.frame_mutex());

      bool success = pipewire.frame_cv().wait_until(lock, deadline, [&] {
        return pipewire.is_frame_ready() || shared_state->stream_dead.load();
      });

      if (success) {
        pipewire.set_frame_ready(false);
        return true;
      }
      return false;
    }

    static uint32_t lookup_pw_format(uint64_t fourcc) {
      for (const auto &fmt : format_map) {
        if (fmt.fourcc == 0) {
          break;
        }
        if (fmt.fourcc == fourcc) {
          return fmt.pw_format;
        }
      }
      return 0;
    }

    void query_dmabuf_formats(EGLDisplay egl_display) {
      EGLint num_dmabuf_formats = 0;
      std::array<EGLint, MAX_DMABUF_FORMATS> dmabuf_formats = {0};
      eglQueryDmaBufFormatsEXT(egl_display, MAX_DMABUF_FORMATS, dmabuf_formats.data(), &num_dmabuf_formats);

      if (num_dmabuf_formats > MAX_DMABUF_FORMATS) {
        BOOST_LOG(warning) << "[portalgrab] Some DMA-BUF formats are being ignored"sv;
      }

      for (EGLint i = 0; i < MIN(num_dmabuf_formats, MAX_DMABUF_FORMATS); i++) {
        uint32_t pw_format = lookup_pw_format(dmabuf_formats[i]);
        if (pw_format == 0) {
          continue;
        }

        EGLint num_modifiers = 0;
        std::array<EGLuint64KHR, MAX_DMABUF_MODIFIERS> mods = {0};
        eglQueryDmaBufModifiersEXT(egl_display, dmabuf_formats[i], MAX_DMABUF_MODIFIERS, mods.data(), nullptr, &num_modifiers);

        if (num_modifiers > MAX_DMABUF_MODIFIERS) {
          BOOST_LOG(warning) << "[portalgrab] Some DMA-BUF modifiers are being ignored"sv;
        }

        dmabuf_infos[n_dmabuf_infos].format = pw_format;
        dmabuf_infos[n_dmabuf_infos].n_modifiers = MIN(num_modifiers, MAX_DMABUF_MODIFIERS);
        dmabuf_infos[n_dmabuf_infos].modifiers =
          static_cast<uint64_t *>(g_memdup2(mods.data(), sizeof(uint64_t) * dmabuf_infos[n_dmabuf_infos].n_modifiers));
        ++n_dmabuf_infos;
      }
    }

    int get_dmabuf_modifiers() {
      if (env_truthy("SUNSHINE_PORTAL_FORCE_MEM")) {
        n_dmabuf_infos = 0;
        display_is_nvidia = false;
        BOOST_LOG(info) << "[portalgrab] SUNSHINE_PORTAL_FORCE_MEM=1 set: forcing memory buffers (DMA-BUF disabled)"sv;
        return 0;
      }

      const bool allow_nvidia_dmabuf = env_truthy("SUNSHINE_PORTAL_ENABLE_NVIDIA_DMABUF");

      if (wl_display.init() < 0) {
        return -1;
      }

      auto egl_display = egl::make_display(wl_display.get());
      if (!egl_display) {
        return -1;
      }

      // Detect if this is a pure NVIDIA system (not hybrid Intel+NVIDIA)
      // On hybrid systems, the wayland compositor typically runs on Intel,
      // so DMA-BUFs from portal will come from Intel and cannot be imported into CUDA.
      // Check if Intel GPU exists - if so, assume hybrid system and disable CUDA DMA-BUF.
      bool has_intel_gpu = std::ifstream("/sys/class/drm/card0/device/vendor").good() ||
                           std::ifstream("/sys/class/drm/card1/device/vendor").good();
      if (has_intel_gpu) {
        // Read vendor IDs to check for Intel (0x8086)
        auto check_intel = [](const std::string &path) {
          if (std::ifstream f(path); f.good()) {
            std::string vendor;
            f >> vendor;
            return vendor == "0x8086";
          }
          return false;
        };
        bool intel_present = check_intel("/sys/class/drm/card0/device/vendor") ||
                             check_intel("/sys/class/drm/card1/device/vendor");
        if (intel_present) {
          BOOST_LOG(info) << "[portalgrab] Hybrid GPU system detected (Intel + discrete) - CUDA will use memory buffers"sv;
          display_is_nvidia = false;
        } else {
          // No Intel GPU found, check if NVIDIA is present
          const char *vendor = eglQueryString(egl_display.get(), EGL_VENDOR);
          if (vendor && std::string_view(vendor).contains("NVIDIA")) {
            if (allow_nvidia_dmabuf) {
              BOOST_LOG(info) << "[portalgrab] Pure NVIDIA system - DMA-BUF enabled for CUDA (SUNSHINE_PORTAL_ENABLE_NVIDIA_DMABUF=1)"sv;
            } else {
              BOOST_LOG(info) << "[portalgrab] Pure NVIDIA system - using memory buffers for CUDA by default; set SUNSHINE_PORTAL_ENABLE_NVIDIA_DMABUF=1 to re-enable DMA-BUF"sv;
            }
            display_is_nvidia = true;
          }
        }
      }

      if (mem_type == platf::mem_type_e::cuda && display_is_nvidia && !allow_nvidia_dmabuf) {
        display_is_nvidia = false;
      }

      if (eglQueryDmaBufFormatsEXT && eglQueryDmaBufModifiersEXT) {
        query_dmabuf_formats(egl_display.get());
      }

      return 0;
    }

    platf::mem_type_e mem_type;
    wl::display_t wl_display;
    dbus_t dbus;
    pipewire_t pipewire;
    std::array<struct dmabuf_format_info_t, MAX_DMABUF_FORMATS> dmabuf_infos;
    int n_dmabuf_infos;
    bool display_is_nvidia = false;  // Track if display GPU is NVIDIA
    std::chrono::nanoseconds delay;
    std::optional<std::uint64_t> last_pts {};
    std::optional<std::uint64_t> last_seq {};
    std::uint64_t sequence {};
    std::uint64_t redundant_drop_events_total {};
    std::uint64_t redundant_retry_events_total {};
    std::uint64_t invalid_frame_events_total {};
    uint32_t framerate;
    std::shared_ptr<shared_state_t> shared_state;
  };
}  // namespace portal

namespace platf {
  std::shared_ptr<display_t> portal_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
    using enum platf::mem_type_e;
    if (hwdevice_type != system && hwdevice_type != vaapi && hwdevice_type != cuda && hwdevice_type != vulkan) {
      BOOST_LOG(error) << "[portalgrab] Could not initialize display with the given hw device type."sv;
      return nullptr;
    }

    // Drop CAP_SYS_ADMIN and set DUMPABLE flag to allow XDG /root access
    portal::runtime_t::instance().finalize_portal_security();

    // Ensure pipewire is initialized and modules are loaded
    pw_init(nullptr, nullptr);

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
      BOOST_LOG(warning) << "[portalgrab] Failed to connect to dbus. Cannot enumerate displays, returning empty list.";
      return {};
    }

    if (!portal::runtime_t::instance().is_portal_secured()) {
      // We're still in the probing phase of Sunshine startup. Dropping portal security early will break KMS.
      // Just return a dummy screen for now. Display re-enumeration after encoder probing will yield full result.
      display_names.emplace_back("init");
      return display_names;
    }

    if (dbus->connect_to_portal() < 0) {
      BOOST_LOG(warning) << "[portalgrab] Failed to connect to portal. Cannot enumerate displays, returning empty list.";
      return {};
    }

    for (auto stream_ : dbus->pipewire_streams) {
      BOOST_LOG(info) << "[portalgrab] Found stream for display: '"sv << stream_.monitor_name << "' position: "sv << stream_.pos_x << "x"sv << stream_.pos_y << " resolution: "sv << stream_.width << "x"sv << stream_.height;
      display_names.emplace_back(stream_.to_display_name());
    }
    // Release the portal session as soon as possible to properly release related resources early.
    dbus.reset();

    // Return currently active display names
    return display_names;
  }
}  // namespace platf
