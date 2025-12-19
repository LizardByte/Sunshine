/**
 * @file wgc_interop_guids.h
 * @brief Windows Graphics Capture Interop GUIDs for clang/llvm-mingw compatibility
 *
 * Provides __mingw_uuidof specialization for IGraphicsCaptureItemInterop
 * which is required for constexpr GUID evaluation with clang.
 */
#pragma once

#ifndef WGC_INTEROP_GUIDS_H
#define WGC_INTEROP_GUIDS_H

#include <guiddef.h>

// Forward declare the interface
struct IGraphicsCaptureItemInterop;

// MinGW UUID specialization for constexpr evaluation
template<>
constexpr const GUID & __mingw_uuidof<IGraphicsCaptureItemInterop>() {
    static constexpr GUID guid = { 0x3628E81B, 0x3CAC, 0x4C60, { 0xB7, 0xF4, 0x23, 0xCE, 0x0E, 0x0C, 0x33, 0x56 } };
    return guid;
}

#endif // WGC_INTEROP_GUIDS_H
