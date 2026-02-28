#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <cstdint>
#include <ostream>
#include <string>

namespace ca {

  // Display FourCC error codes, with fallback to integer.
  // Usage: BOOST_LOG(error) << ca::Status(err);

  // Some CoreAudio error examples:
  // kAudioHardwareNoError                   = 0,
  // kAudioHardwareNotRunningError           = 'stop',
  // kAudioHardwareUnspecifiedError          = 'what',
  // kAudioHardwareUnknownPropertyError      = 'who?',
  // kAudioHardwareBadPropertySizeError      = '!siz',
  // kAudioHardwareIllegalOperationError     = 'nope',
  // kAudioHardwareBadObjectError            = '!obj',
  // kAudioHardwareBadDeviceError            = '!dev',
  // kAudioHardwareBadStreamError            = '!str',
  // kAudioHardwareUnsupportedOperationError = 'unop',
  // kAudioHardwareNotReadyError             = 'nrdy',
  // kAudioDeviceUnsupportedFormatError      = '!dat',
  // kAudioDevicePermissionsError            = '!hog'

  inline std::string OSStatusToString(OSStatus error) {
    uint32_t be = CFSwapInt32HostToBig(static_cast<uint32_t>(error));
    const unsigned char c1 = static_cast<unsigned char>((be >> 24) & 0xFF);
    const unsigned char c2 = static_cast<unsigned char>((be >> 16) & 0xFF);
    const unsigned char c3 = static_cast<unsigned char>((be >> 8) & 0xFF);
    const unsigned char c4 = static_cast<unsigned char>((be >> 0) & 0xFF);

    auto is_printable = [](unsigned char c) -> bool {
      return c >= 32 && c <= 126;
    };

    if (is_printable(c1) && is_printable(c2) && is_printable(c3) && is_printable(c4)) {
      char buf[8] = {};
      buf[0] = '\'';
      buf[1] = static_cast<char>(c1);
      buf[2] = static_cast<char>(c2);
      buf[3] = static_cast<char>(c3);
      buf[4] = static_cast<char>(c4);
      buf[5] = '\'';
      buf[6] = '\0';
      return std::string(buf);
    }

    return std::to_string(static_cast<int32_t>(error));
  }

  namespace detail {
    struct StatusView {
      OSStatus e;
    };

    inline std::ostream &operator<<(std::ostream &os, StatusView v) {
      return os << OSStatusToString(v.e);
    }
  }  // namespace detail

  inline detail::StatusView Status(OSStatus e) {
    return detail::StatusView {e};
  }

}  // namespace ca
