#pragma once

#include "undo_data.h"

namespace nvprefs {

  class undo_file_t {
  public:
    static std::optional<undo_file_t>
    open_existing_file(std::filesystem::path file_path, bool &access_denied);

    static std::optional<undo_file_t>
    create_new_file(std::filesystem::path file_path);

    bool
    delete_file();

    bool
    write_undo_data(const undo_data_t &undo_data);

    std::optional<undo_data_t>
    read_undo_data();

  private:
    undo_file_t() = default;
    safe_handle file_handle;
  };

}  // namespace nvprefs
