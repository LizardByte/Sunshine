#include "nvprefs_common.h"

#include "undo_data.h"

namespace {

  const auto opengl_swapchain_our_value_key = "/opengl_swapchain/our_value";
  const auto opengl_swapchain_undo_value_key = "/opengl_swapchain/undo_value";

}  // namespace

namespace nvprefs {

  void
  undo_data_t::set_opengl_swapchain(uint32_t our_value, std::optional<uint32_t> undo_value) {
    data.set_at_pointer(opengl_swapchain_our_value_key, our_value);
    if (undo_value) {
      data.set_at_pointer(opengl_swapchain_undo_value_key, *undo_value);
    }
    else {
      data.set_at_pointer(opengl_swapchain_undo_value_key, nullptr);
    }
  }

  std::tuple<bool, uint32_t, std::optional<uint32_t>>
  undo_data_t::get_opengl_swapchain() const {
    auto get_value = [this](const auto &key) -> std::tuple<bool, std::optional<uint32_t>> {
      try {
        auto value = data.at_pointer(key);
        if (value.is_null()) {
          return { true, std::nullopt };
        }
        else if (value.is_number()) {
          return { true, value.template to_number<uint32_t>() };
        }
      }
      catch (...) {
      }
      error_message(std::string("Couldn't find ") + key + " element");
      return { false, std::nullopt };
    };

    auto [our_value_present, our_value] = get_value(opengl_swapchain_our_value_key);
    auto [undo_value_present, undo_value] = get_value(opengl_swapchain_undo_value_key);

    if (!our_value_present || !undo_value_present || !our_value) {
      return { false, 0, std::nullopt };
    }

    return { true, *our_value, undo_value };
  }

  std::string
  undo_data_t::write() const {
    return boost::json::serialize(data);
  }

  void
  undo_data_t::read(const std::vector<char> &buffer) {
    data = boost::json::parse(std::string_view(buffer.data(), buffer.size()));
  }

  void
  undo_data_t::merge(const undo_data_t &newer_data) {
    auto [opengl_swapchain_saved, opengl_swapchain_our_value, opengl_swapchain_undo_value] = newer_data.get_opengl_swapchain();
    if (opengl_swapchain_saved) {
      set_opengl_swapchain(opengl_swapchain_our_value, opengl_swapchain_undo_value);
    }
  }

}  // namespace nvprefs
