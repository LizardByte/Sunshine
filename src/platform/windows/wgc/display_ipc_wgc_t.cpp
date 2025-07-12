
#include "src/platform/windows/display.h"

namespace platf::dxgi {

class display_ipc_wgc_t : public display_wgc_vram_t {
public:
    display_ipc_wgc_t() = default;
    ~display_ipc_wgc_t() override = default;

    int init(const ::video::config_t &config, const std::string &display_name) override {
        // TODO: implement
        return display_wgc_vram_t::init(config, display_name);
    }

    capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) override {
        // TODO: implement
        return display_wgc_vram_t::snapshot(pull_free_image_cb, img_out, timeout, cursor_visible);
    }

    capture_e release_snapshot() override {
        // TODO: implement
        return display_wgc_vram_t::release_snapshot();
    }
};

} // namespace platf::dxgi
