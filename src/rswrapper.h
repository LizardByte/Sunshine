/**
 * @file src/rswrapper.h
 * @brief Wrappers for nanors vectorization
 * @details This is a drop-in replacement for nanors rs.h
 */
#pragma once

// standard includes
#include <stdint.h>

/**
 * @def DATA_SHARDS_MAX
 * @brief Macro for DATA SHARDS MAX.
 */
#define DATA_SHARDS_MAX 255

/**
 * @brief Opaque Reed-Solomon encoder/decoder context.
 */
typedef struct _reed_solomon reed_solomon;

/**
 * @brief Function pointer used to create a Reed-Solomon context.
 */
typedef reed_solomon *(*reed_solomon_new_t)(int data_shards, int parity_shards);
/**
 * @brief Function pointer used to release a Reed-Solomon context.
 */
typedef void (*reed_solomon_release_t)(reed_solomon *rs);
/**
 * @brief Function pointer used to encode Reed-Solomon recovery shards.
 */
typedef int (*reed_solomon_encode_t)(reed_solomon *rs, uint8_t **shards, int nr_shards, int bs);
/**
 * @brief Function pointer used to decode Reed-Solomon recovery shards.
 */
typedef int (*reed_solomon_decode_t)(reed_solomon *rs, uint8_t **shards, uint8_t *marks, int nr_shards, int bs);

extern reed_solomon_new_t reed_solomon_new_fn;  ///< Reed solomon new.
extern reed_solomon_release_t reed_solomon_release_fn;  ///< Reed solomon release.
extern reed_solomon_encode_t reed_solomon_encode_fn;  ///< Reed solomon encode.
extern reed_solomon_decode_t reed_solomon_decode_fn;  ///< Reed solomon decode.

/**
 * @def reed_solomon_new
 * @brief Macro for reed solomon new.
 */
#define reed_solomon_new reed_solomon_new_fn
/**
 * @def reed_solomon_release
 * @brief Macro for reed solomon release.
 */
#define reed_solomon_release reed_solomon_release_fn
/**
 * @def reed_solomon_encode
 * @brief Macro for reed solomon encode.
 */
#define reed_solomon_encode reed_solomon_encode_fn
/**
 * @def reed_solomon_decode
 * @brief Macro for reed solomon decode.
 */
#define reed_solomon_decode reed_solomon_decode_fn

/**
 * @brief This initializes the RS function pointers to the best vectorized version available.
 * @details The streaming code will directly invoke these function pointers during encoding.
 */
void reed_solomon_init(void);
