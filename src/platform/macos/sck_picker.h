/**
 * @file src/platform/macos/sck_picker.h
 * @brief C++-safe interface to the ScreenCaptureKit content sharing picker.
 */
#pragma once

/**
 * @brief Check if the SCK Picker API is available.
 * @return true on macOS 14+
 */
bool sck_picker_available();

/**
 * @brief Present the SCContentSharingPicker for the active capture stream on the host's display.
 * @return true if a capture stream is active and the picker will be presented, false otherwise.
 */
bool sck_present_picker();
