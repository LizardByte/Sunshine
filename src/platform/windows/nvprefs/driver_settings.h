#pragma once

#include "undo_data.h"

namespace nvprefs {

  class driver_settings_t {
  public:
    ~driver_settings_t();

    bool
    init();

    void
    destroy();

    bool
    load_settings();

    bool
    save_settings();

    bool
    restore_global_profile_to_undo(const undo_data_t &undo_data);

    bool
    check_and_modify_global_profile(std::optional<undo_data_t> &undo_data);

    bool
    check_and_modify_application_profile(bool &modified);

  private:
    NvDRSSessionHandle session_handle = 0;
  };

}  // namespace nvprefs
