/**
 * @file globals.h
 * @brief Header for globally accessible variables and functions.
 */
#pragma once

#include "entry_handler.h"
#include "thread_pool.h"

extern thread_pool_util::ThreadPool task_pool;
extern bool display_cursor;

#ifdef _WIN32
  // Declare global singleton used for NVIDIA control panel modifications
  #include "platform/windows/nvprefs/nvprefs_interface.h"
extern nvprefs::nvprefs_interface nvprefs_instance;
#endif

namespace mail {
#define MAIL(x)                         \
  constexpr auto x = std::string_view { \
    #x                                  \
  }

  extern safe::mail_t man;

  // Global mail
  MAIL(shutdown);
  MAIL(broadcast_shutdown);
  MAIL(video_packets);
  MAIL(audio_packets);
  MAIL(switch_display);

  // Local mail
  MAIL(touch_port);
  MAIL(idr);
  MAIL(invalidate_ref_frames);
  MAIL(gamepad_feedback);
  MAIL(hdr);
#undef MAIL

}  // namespace mail
