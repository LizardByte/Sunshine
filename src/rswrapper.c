/**
 * @file src/rswrapper.c
 * @brief Wrappers for nanors vectorization with different ISA options
 */

// _FORTIY_SOURCE can cause some versions of GCC to try to inline
// memset() with incompatible target options when compiling rs.c
#ifdef _FORTIFY_SOURCE
  #undef _FORTIFY_SOURCE
#endif

// The assert() function is decorated with __cold on macOS which
// is incompatible with Clang's target multiversioning feature
#ifndef NDEBUG
  /**
   * @def NDEBUG
   * @brief Macro for NDEBUG.
   */
  #define NDEBUG
#endif

/**
 * @def DECORATE_FUNC_I(a, b)
 * @brief Macro for DECORATE FUNC i.
 */
#define DECORATE_FUNC_I(a, b) a##b
/**
 * @def DECORATE_FUNC(a, b)
 * @brief Macro for DECORATE FUNC.
 */
#define DECORATE_FUNC(a, b) DECORATE_FUNC_I(a, b)

// Append an ISA suffix to the public RS API
#define reed_solomon_init DECORATE_FUNC(reed_solomon_init, ISA_SUFFIX)
/**
 * @def reed_solomon_new
 * @brief Macro for reed solomon new.
 */
#define reed_solomon_new DECORATE_FUNC(reed_solomon_new, ISA_SUFFIX)
/**
 * @def reed_solomon_new_static
 * @brief Macro for reed solomon new static.
 */
#define reed_solomon_new_static DECORATE_FUNC(reed_solomon_new_static, ISA_SUFFIX)
/**
 * @def reed_solomon_release
 * @brief Macro for reed solomon release.
 */
#define reed_solomon_release DECORATE_FUNC(reed_solomon_release, ISA_SUFFIX)
/**
 * @def reed_solomon_decode
 * @brief Macro for reed solomon decode.
 */
#define reed_solomon_decode DECORATE_FUNC(reed_solomon_decode, ISA_SUFFIX)
/**
 * @def reed_solomon_encode
 * @brief Macro for reed solomon encode.
 */
#define reed_solomon_encode DECORATE_FUNC(reed_solomon_encode, ISA_SUFFIX)

// Append an ISA suffix to internal functions to prevent multiple definition errors
/**
 * @def obl_axpy_ref
 * @brief Macro for obl axpy ref.
 */
#define obl_axpy_ref DECORATE_FUNC(obl_axpy_ref, ISA_SUFFIX)
/**
 * @def obl_scal_ref
 * @brief Macro for obl scal ref.
 */
#define obl_scal_ref DECORATE_FUNC(obl_scal_ref, ISA_SUFFIX)
/**
 * @def obl_axpyb32_ref
 * @brief Macro for obl axpyb32 ref.
 */
#define obl_axpyb32_ref DECORATE_FUNC(obl_axpyb32_ref, ISA_SUFFIX)
/**
 * @def obl_axpy
 * @brief Macro for obl axpy.
 */
#define obl_axpy DECORATE_FUNC(obl_axpy, ISA_SUFFIX)
/**
 * @def obl_scal
 * @brief Macro for obl scal.
 */
#define obl_scal DECORATE_FUNC(obl_scal, ISA_SUFFIX)
/**
 * @def obl_swap
 * @brief Macro for obl swap.
 */
#define obl_swap DECORATE_FUNC(obl_swap, ISA_SUFFIX)
/**
 * @def obl_axpyb32
 * @brief Macro for obl axpyb32.
 */
#define obl_axpyb32 DECORATE_FUNC(obl_axpyb32, ISA_SUFFIX)
/**
 * @def axpy
 * @brief Macro for axpy.
 */
#define axpy DECORATE_FUNC(axpy, ISA_SUFFIX)
/**
 * @def scal
 * @brief Macro for scal.
 */
#define scal DECORATE_FUNC(scal, ISA_SUFFIX)
/**
 * @def gemm
 * @brief Macro for gemm.
 */
#define gemm DECORATE_FUNC(gemm, ISA_SUFFIX)
/**
 * @def invert_mat
 * @brief Macro for invert mat.
 */
#define invert_mat DECORATE_FUNC(invert_mat, ISA_SUFFIX)

#if defined(__x86_64) || defined(__x86_64__) || defined(__amd64) || defined(__amd64__) || defined(_M_AMD64)

  // Compile a variant for SSSE3
  #if defined(__clang__)
    #pragma clang attribute push(__attribute__((target("ssse3"))), apply_to = function)
  #else
    #pragma GCC push_options
    #pragma GCC target("ssse3")
  #endif
  #define ISA_SUFFIX _ssse3
  #define OBLAS_SSE3
  #include "../third-party/nanors/rs.c"
  #undef OBLAS_SSE3
  #undef ISA_SUFFIX
  #if defined(__clang__)
    #pragma clang attribute pop
  #else
    #pragma GCC pop_options
  #endif

  // Compile a variant for AVX2
  #if defined(__clang__)
    #pragma clang attribute push(__attribute__((target("avx2"))), apply_to = function)
  #else
    #pragma GCC push_options
    #pragma GCC target("avx2")
  #endif
  #define ISA_SUFFIX _avx2
  #define OBLAS_AVX2
  #include "../third-party/nanors/rs.c"
  #undef OBLAS_AVX2
  #undef ISA_SUFFIX
  #if defined(__clang__)
    #pragma clang attribute pop
  #else
    #pragma GCC pop_options
  #endif

  // Compile a variant for AVX512BW
  #if defined(__clang__)
    #pragma clang attribute push(__attribute__((target("avx512f,avx512bw"))), apply_to = function)
  #else
    #pragma GCC push_options
    #pragma GCC target("avx512f,avx512bw")
  #endif
  #define ISA_SUFFIX _avx512
  #define OBLAS_AVX512
  #include "../third-party/nanors/rs.c"
  #undef OBLAS_AVX512
  #undef ISA_SUFFIX
  #if defined(__clang__)
    #pragma clang attribute pop
  #else
    #pragma GCC pop_options
  #endif

#endif

// Compile a default variant
/**
 * @def ISA_SUFFIX
 * @brief Macro for ISA SUFFIX.
 */
#define ISA_SUFFIX _def
#include "../third-party/nanors/deps/obl/autoshim.h"
#include "../third-party/nanors/rs.c"
#undef ISA_SUFFIX

#undef reed_solomon_init
#undef reed_solomon_new
#undef reed_solomon_new_static
#undef reed_solomon_release
#undef reed_solomon_decode
#undef reed_solomon_encode

#include "rswrapper.h"

reed_solomon_new_t reed_solomon_new_fn;  ///< Reed solomon new.
reed_solomon_release_t reed_solomon_release_fn;  ///< Reed solomon release.
reed_solomon_encode_t reed_solomon_encode_fn;  ///< Reed solomon encode.
reed_solomon_decode_t reed_solomon_decode_fn;  ///< Reed solomon decode.

/**
 * @brief This initializes the RS function pointers to the best vectorized version available.
 * @details The streaming code will directly invoke these function pointers during encoding.
 */
void reed_solomon_init(void) {
#if defined(__x86_64) || defined(__x86_64__) || defined(__amd64) || defined(__amd64__) || defined(_M_AMD64)
  if (__builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw")) {
    reed_solomon_new_fn = reed_solomon_new_avx512;
    reed_solomon_release_fn = reed_solomon_release_avx512;
    reed_solomon_encode_fn = reed_solomon_encode_avx512;
    reed_solomon_decode_fn = reed_solomon_decode_avx512;
    reed_solomon_init_avx512();
  } else if (__builtin_cpu_supports("avx2")) {
    reed_solomon_new_fn = reed_solomon_new_avx2;
    reed_solomon_release_fn = reed_solomon_release_avx2;
    reed_solomon_encode_fn = reed_solomon_encode_avx2;
    reed_solomon_decode_fn = reed_solomon_decode_avx2;
    reed_solomon_init_avx2();
  } else if (__builtin_cpu_supports("ssse3")) {
    reed_solomon_new_fn = reed_solomon_new_ssse3;
    reed_solomon_release_fn = reed_solomon_release_ssse3;
    reed_solomon_encode_fn = reed_solomon_encode_ssse3;
    reed_solomon_decode_fn = reed_solomon_decode_ssse3;
    reed_solomon_init_ssse3();
  } else
#endif
  {
    reed_solomon_new_fn = reed_solomon_new_def;
    reed_solomon_release_fn = reed_solomon_release_def;
    reed_solomon_encode_fn = reed_solomon_encode_def;
    reed_solomon_decode_fn = reed_solomon_decode_def;
    reed_solomon_init_def();
  }
}
