/**
 * @file src/platform/linux/pipewire_node_grab.cpp
 * @brief Direct PipeWire node stream capture for GNOME Mutter virtual displays.
 */
// standard includes
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

// lib includes
#include <lizardbyte/common/env.h>
#include <pipewire/pipewire.h>

// local includes
#include "cuda.h"
#include "graphics.h"
#include "pipewire.cpp"
#include "src/platform/common.h"
#include "src/video.h"

using namespace std::literals;

namespace pipewire_node {
  struct node_info_sync_t {
    uint32_t target_id;
    uint64_t object_serial;
    std::string node_name;
    bool found;
  };

  static void registry_event_global(void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props) {
    auto info = static_cast<node_info_sync_t *>(data);
    if (id == info->target_id && props) {
      info->found = true;
      const char *serial_str = spa_dict_lookup(props, PW_KEY_OBJECT_SERIAL);
      if (serial_str) {
        try {
          info->object_serial = std::stoull(serial_str);
        } catch (...) {}
      }
      const char *name_str = spa_dict_lookup(props, PW_KEY_NODE_NAME);
      if (name_str) {
        info->node_name = name_str;
      }
    }
  }

  static uint64_t resolve_node_serial(uint32_t target_node_id, std::string &out_name) {
    struct pw_main_loop *loop = pw_main_loop_new(nullptr);
    if (!loop) return 0;
    struct pw_context *context = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
    if (!context) {
      pw_main_loop_destroy(loop);
      return 0;
    }
    struct pw_core *core = pw_context_connect(context, nullptr, 0);
    if (!core) {
      pw_context_destroy(context);
      pw_main_loop_destroy(loop);
      return 0;
    }

    node_info_sync_t info = {target_node_id, 0, "", false};
    struct pw_registry *registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
    struct spa_hook registry_listener;
    static const struct pw_registry_events registry_events = {
      .version = PW_VERSION_REGISTRY_EVENTS,
      .global = registry_event_global,
    };
    spa_zero(registry_listener);
    pw_registry_add_listener(registry, &registry_listener, &registry_events, &info);

    struct core_sync_data_t {
      struct pw_main_loop *loop;
      int sync_seq;
      bool done;
    } sync_data = {loop, 0, false};

    static const struct pw_core_events core_events = {
      .version = PW_VERSION_CORE_EVENTS,
      .done = [](void *data, uint32_t id, int seq) {
        auto d = static_cast<core_sync_data_t *>(data);
        if (id == PW_ID_CORE && seq == d->sync_seq) {
          d->done = true;
          pw_main_loop_quit(d->loop);
        }
      }
    };
    struct spa_hook core_listener;
    spa_zero(core_listener);
    pw_core_add_listener(core, &core_listener, &core_events, &sync_data);

    sync_data.sync_seq = pw_core_sync(core, PW_ID_CORE, 0);

    pw_main_loop_run(loop);

    spa_hook_remove(&registry_listener);
    spa_hook_remove(&core_listener);
    pw_proxy_destroy(reinterpret_cast<struct pw_proxy *>(registry));
    pw_core_disconnect(core);
    pw_context_destroy(context);
    pw_main_loop_destroy(loop);

    if (info.found) {
      out_name = info.node_name;
      return info.object_serial;
    }
    return 0;
  }

  class pipewire_node_display_impl_t: public pipewire::pipewire_display_t {
  public:
    int configure_stream(const std::string &display_name, int &out_pipewire_fd, uint32_t &out_pipewire_node, uint64_t &out_pipewire_objectserial) override {
      uint32_t target_node = 0;
      std::string env_node;
      if (lizardbyte::common::get_env("SUNSHINE_PIPEWIRE_NODE", env_node) && !env_node.empty()) {
        try {
          target_node = static_cast<uint32_t>(std::stoul(env_node));
        } catch (...) {
          target_node = 0;
        }
      }
      if (target_node == 0 && !display_name.empty()) {
        if (display_name.rfind("node:", 0) == 0) {
          try {
            target_node = static_cast<uint32_t>(std::stoul(display_name.substr(5)));
          } catch (...) {}
        } else if (display_name.find_first_not_of("0123456789") == std::string::npos) {
          try {
            target_node = static_cast<uint32_t>(std::stoul(display_name));
          } catch (...) {}
        }
      }

      if (target_node == 0) {
        BOOST_LOG(error) << "[pipewire_node] No valid PipeWire node id specified in SUNSHINE_PIPEWIRE_NODE or output_name"sv;
        return -1;
      }

      BOOST_LOG(info) << "[pipewire_node] Resolving PipeWire stream node: "sv << target_node;

      std::string node_name;
      uint64_t resolved_serial = resolve_node_serial(target_node, node_name);
      if (resolved_serial != 0) {
        BOOST_LOG(info) << "[pipewire_node] Resolved node "sv << target_node << " to object.serial: "sv << resolved_serial << " (name: "sv << node_name << ")"sv;
      } else {
        BOOST_LOG(warning) << "[pipewire_node] Could not find object.serial for node "sv << target_node << "; falling back to node id"sv;
      }

      out_pipewire_fd = -1;  // Connect to local pipewire core
      out_pipewire_node = target_node;
      out_pipewire_objectserial = resolved_serial;

      this->offset_x = 0;
      this->offset_y = 0;
      this->width = 1920;
      this->height = 1080;
      this->logical_width = 0;
      this->logical_height = 0;

      std::string w_str, h_str;
      if (lizardbyte::common::get_env("SUNSHINE_PIPEWIRE_WIDTH", w_str) && lizardbyte::common::get_env("SUNSHINE_PIPEWIRE_HEIGHT", h_str)) {
        try {
          this->width = std::stoi(w_str);
          this->height = std::stoi(h_str);
        } catch (...) {}
      }

      std::string x_str, y_str;
      if (lizardbyte::common::get_env("SUNSHINE_PIPEWIRE_OFFSET_X", x_str)) {
        try {
          this->offset_x = std::stoi(x_str);
        } catch (...) {}
      }
      if (lizardbyte::common::get_env("SUNSHINE_PIPEWIRE_OFFSET_Y", y_str)) {
        try {
          this->offset_y = std::stoi(y_str);
        } catch (...) {}
      }

      BOOST_LOG(info) << "[pipewire_node] Direct stream node: "sv << target_node 
                      << " resolution: "sv << this->width << "x"sv << this->height
                      << " offset: "sv << this->offset_x << "x"sv << this->offset_y;

      return 0;
    }
  };
}  // namespace pipewire_node

namespace platf {
  bool pipewire_node_available() {
    std::string env_node;
    if (lizardbyte::common::get_env("SUNSHINE_PIPEWIRE_NODE", env_node) && !env_node.empty()) {
      return true;
    }
    return false;
  }

  std::vector<std::string> pipewire_node_display_names() {
    std::string env_node;
    if (lizardbyte::common::get_env("SUNSHINE_PIPEWIRE_NODE", env_node) && !env_node.empty()) {
      return {env_node};
    }
    return {"init"};
  }

  std::shared_ptr<display_t> pipewire_node_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
    if (!pipewire::pipewire_display_t::init_pipewire_and_check_hwdevice_type(hwdevice_type)) {
      BOOST_LOG(error) << "[pipewire_node] Could not initialize pipewire-based display with the given hw device type."sv;
      return nullptr;
    }

    auto display = std::make_shared<pipewire_node::pipewire_node_display_impl_t>();
    if (display->init(hwdevice_type, display_name, config)) {
      return nullptr;
    }

    return display;
  }
}  // namespace platf
