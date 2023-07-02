#include "nvprefs_common.h"

namespace nvprefs {

  void
  info_message(const std::wstring &message) {
    BOOST_LOG(info) << "nvprefs: " << message;
  }

  void
  info_message(const std::string &message) {
    BOOST_LOG(info) << "nvprefs: " << message;
  }

  void
  error_message(const std::wstring &message) {
    BOOST_LOG(error) << "nvprefs: " << message;
  }

  void
  error_message(const std::string &message) {
    BOOST_LOG(error) << "nvprefs: " << message;
  }

}  // namespace nvprefs
