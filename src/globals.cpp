/**
 * @file globals.cpp
 * @brief Definitions for application infrastructure ownership.
 */
// local includes
#include "globals.h"

namespace runtime {
  infrastructure_t::infrastructure_t(char **argv):
      lifetime_ {argv},
      mailbox_ {std::make_shared<safe::mail_raw_t>()} {
  }

  infrastructure_t::~infrastructure_t() {
    scheduler_.stop();
    scheduler_.join();
  }

  thread_pool_util::ThreadPool &infrastructure_t::scheduler() {
    return scheduler_;
  }

  safe::mail_t infrastructure_t::mailbox() const {
    return mailbox_;
  }

  bool &infrastructure_t::cursor() {
    return cursor_;
  }

  lifetime::controller_t &infrastructure_t::lifetime() {
    return lifetime_;
  }

#ifdef _WIN32
  nvprefs::nvprefs_interface &infrastructure_t::preferences() {
    return preferences_;
  }
#endif
}  // namespace runtime
