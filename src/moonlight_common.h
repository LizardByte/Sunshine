/**
 * @file src/moonlight_common.h
 * @brief Import Moonlight's C declarations without changing C++ errno definitions.
 */
#pragma once

// Load the CRT definitions before saving them, even when this is the first include.
#include <cerrno>

// PlatformSockets.h maps these names to Winsock values for Moonlight's C code.
// Let that code see its mappings, but do not leak them into Boost or the C++ library.
#ifdef _WIN32
  #pragma push_macro("EAGAIN")
  #pragma push_macro("EINTR")
  #pragma push_macro("EWOULDBLOCK")
  #pragma push_macro("EINPROGRESS")
  #pragma push_macro("ETIMEDOUT")
  #pragma push_macro("ECONNREFUSED")
  #pragma push_macro("EMSGSIZE")
#endif

extern "C" {
#include <moonlight-common-c/src/Limelight-internal.h>
}

#ifdef _WIN32
  #pragma pop_macro("EMSGSIZE")
  #pragma pop_macro("ECONNREFUSED")
  #pragma pop_macro("ETIMEDOUT")
  #pragma pop_macro("EINPROGRESS")
  #pragma pop_macro("EWOULDBLOCK")
  #pragma pop_macro("EINTR")
  #pragma pop_macro("EAGAIN")
#endif
