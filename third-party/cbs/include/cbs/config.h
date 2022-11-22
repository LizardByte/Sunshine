// [sunshine] Copied, generated file
#ifndef CBS_CONFIG_H
#define CBS_CONFIG_H

#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN ||                       \
  defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ ||             \
  defined(__FLOAT_WORD_ORDER__) && __FLOAT_WORD_ORDER__ == __ORDER_BIG_ENDIAN__ || \
  defined(__BIG_ENDIAN__) ||                                                       \
  defined(__ARMEB__) ||                                                            \
  defined(__THUMBEB__) ||                                                          \
  defined(__AARCH64EB__) ||                                                        \
  defined(_MIBSEB) || defined(__MIBSEB) || defined(__MIBSEB__)
// It's a big-endian target architecture
#define AV_HAVE_BIGENDIAN 1

#elif defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN ||                     \
  defined(__BYTE_ORDER) && __BYTE_ORDER == __PDP_ENDIAN ||                            \
  defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ ||             \
  defined(__FLOAT_WORD_ORDER__) && __FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__ || \
  defined(__LITTLE_ENDIAN__) ||                                                       \
  defined(__ARMEL__) ||                                                               \
  defined(__THUMBEL__) ||                                                             \
  defined(__AARCH64EL__) ||                                                           \
  defined(_MIPSEL) || defined(__MIPSEL) || defined(__MIPSEL__) ||                     \
  defined(_WIN32)
// It's a little-endian target architecture
#define AV_HAVE_BIGENDIAN 0

#else
// https://manhnt.github.io/programming_technique/2018/08/15/oneline-macro-endian-check.html
#define AV_HAVE_BIGENDIAN (*(uint16_t *)"\0\xff" < 0x0100)
#endif

#endif