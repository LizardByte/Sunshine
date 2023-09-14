#pragma once

namespace nvprefs {

  class undo_data_t {
  public:
    void
    set_opengl_swapchain(uint32_t our_value, std::optional<uint32_t> undo_value);

    std::tuple<bool, uint32_t, std::optional<uint32_t>>
    get_opengl_swapchain() const;

    void
    write(std::ostream &stream) const;

    std::string
    write() const;

    void
    read(std::istream &stream);

    void
    read(const std::vector<char> &buffer);

    void
    merge(const undo_data_t &newer_data);

  private:
    boost::json::value data;
  };

}  // namespace nvprefs
