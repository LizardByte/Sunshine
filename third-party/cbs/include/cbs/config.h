#ifndef CBS_CONFIG_H
#define CBS_CONFIG_H

#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN || \
  defined(__BIG_ENDIAN__) ||                                 \
  defined(__ARMEB__) ||                                      \
  defined(__THUMBEB__) ||                                    \
  defined(__AARCH64EB__) ||                                  \
  defined(_MIBSEB) || defined(__MIBSEB) || defined(__MIBSEB__)
// It's a big-endian target architecture
#define AV_HAVE_BIGENDIAN 1

#elif defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN || \
  defined(__LITTLE_ENDIAN__) ||                                   \
  defined(__ARMEL__) ||                                           \
  defined(__THUMBEL__) ||                                         \
  defined(__AARCH64EL__) ||                                       \
  defined(_MIPSEL) || defined(__MIPSEL) || defined(__MIPSEL__) || \
  defined(_WIN32)
// It's a little-endian target architecture
#define AV_HAVE_BIGENDIAN 0

#else
#error "Unknown Endianness"
#endif

#endif