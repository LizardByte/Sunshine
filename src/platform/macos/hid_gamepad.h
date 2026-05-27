/**
 * @file src/platform/macos/hid_gamepad.h
 * @brief Virtual HID gamepad via IOHIDUserDevice for macOS.
 * @details Creates a system-wide virtual gamepad that macOS Game Controller
 *          framework recognizes. Requires SIP to be disabled.
 */
#pragma once

#import <Foundation/Foundation.h>
#import <IOKit/hidsystem/IOHIDUserDevice.h>

/**
 * HID report sent to IOHIDUserDevice. Packed to exactly 14 bytes.
 * Matches the HID report descriptor defined in hid_gamepad.m.
 */
typedef struct __attribute__((packed)) {
    uint8_t  reportId;       // Always 0x01
    uint16_t buttons;        // 16 button bits
    uint8_t  hatSwitch;      // D-pad hat switch (0-7 = directions, 8 = neutral)
    uint8_t  leftTrigger;    // 0-255
    uint8_t  rightTrigger;   // 0-255
    int16_t  leftStickX;     // -32768 to 32767
    int16_t  leftStickY;     // -32768 to 32767
    int16_t  rightStickX;    // -32768 to 32767
    int16_t  rightStickY;    // -32768 to 32767
} HIDGamepadReport;

@interface HIDGamepad : NSObject

@property (nonatomic, assign) int gamepadIndex;
@property (nonatomic, assign) BOOL isConnected;
@property (nonatomic, assign) IOHIDUserDeviceRef hidDevice;
@property (nonatomic, strong) dispatch_queue_t hidQueue;

/**
 * Probes whether IOHIDUserDevice virtual gamepads can be created.
 * Returns NO when SIP is enabled (device creation fails).
 */
+ (BOOL)isAvailable;

- (instancetype)initWithIndex:(int)index;

/**
 * Creates the IOHIDUserDevice and sends an initial neutral-state report.
 * @return YES on success, NO on failure.
 */
- (BOOL)createDevice;

/**
 * Maps Sunshine's gamepad state to an HID report and sends it.
 * @param buttons  Sunshine's 32-bit buttonFlags (only lower 16 bits + HOME used)
 * @param lsX      Left stick X (-32768..32767)
 * @param lsY      Left stick Y (-32768..32767)
 * @param rsX      Right stick X (-32768..32767)
 * @param rsY      Right stick Y (-32768..32767)
 * @param lt       Left trigger (0..255)
 * @param rt       Right trigger (0..255)
 */
- (void)updateState:(uint32_t)buttons
         leftStickX:(int16_t)lsX
         leftStickY:(int16_t)lsY
        rightStickX:(int16_t)rsX
        rightStickY:(int16_t)rsY
        leftTrigger:(uint8_t)lt
       rightTrigger:(uint8_t)rt;

/**
 * Destroys the IOHIDUserDevice and cleans up resources.
 */
- (void)disconnect;

@end
