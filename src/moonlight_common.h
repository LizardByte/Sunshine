/**
 * @file src/moonlight_common.h
 * @brief Include Moonlight's internal protocol headers with C++ atomic support and C linkage.
 */
#pragma once

// standard includes
// C++23's compatibility header defines atomic types using C++ templates. Load it before the C linkage block.
#include <stdatomic.h>

// lib includes
extern "C" {
#include <moonlight-common-c/src/Limelight-internal.h>
#include <moonlight-common-c/src/Rtsp.h>
}
