// GamepadHelper library
const GamepadHelper = (function() {
    // Controller type definitions
    const CONTROLLER_TYPES = {
        XBOX: 'xbox',
        PLAYSTATION: 'playstation',
        SWITCH: 'switch',
        STANDARD: 'standard'
    };

    // Exact gamepad ID mappings with numbered indices
    const exactGamepadMappings = {
        0: {
            name: "Generic Gamepad",
            gamepad_api_ids: [
                "USB Gamepad (STANDARD GAMEPAD Vendor: 0079 Product: 0011)",
                "Logitech Cordless RumblePad 2 (STANDARD GAMEPAD Vendor: 046d Product: c219)",
                "Unknown Gamepad (Vendor: 2563 Product: 0575)",
                "PC/PS3/Android (Vendor: 2563 Product: 0575)",
                "Core (Plus) Wired Controller (Vendor: 20d6 Product: a711)",
                "Wireless Controller Extended Gamepad",
            ],
            type: CONTROLLER_TYPES.STANDARD,
        },
        1: {
            name: "Sony PlayStation 3",
            gamepad_api_ids: [
                "54c-268-PLAYSTATION(R)3 Controller",
                "PLAYSTATION(R)3 Controller (STANDARD GAMEPAD Vendor: 054c Product: 0268)",
                "PLAYSTATION(R)3 Controller (Vendor: 054c Product: 0268)",
                "PS3 GamePad (Vendor: 054c Product: 0268)",
                "PS3/PC Wired GamePad (Vendor: 2563 Product: 0523)",
            ],
            type: CONTROLLER_TYPES.PLAYSTATION
        },
        2: {
            name: "Sony DualShock (PS4)",
            gamepad_api_ids: [
                "054c-05c4-Wireless Controller",
                "Wireless controller (STANDARD GAMEPAD Vendor: 054c Product: 05c4)",
                "054c-09cc-Unknown Gamepad",
                "Unknown Gamepad (STANDARD GAMEPAD Vendor: 054c Product: 09cc)",
                "DS4 Wired Controller (Vendor: 7545 Product: 0104)",
            ],
            type: CONTROLLER_TYPES.PLAYSTATION
        },
        3: {
            name: "Sony DualSense (PS5)",
            gamepad_api_ids: [
                "054c-0ce6-Wireless Controller",
                "Wireless Controller (Vendor: 054c Product: 0ce6)",
                "Wireless Controller (STANDARD GAMEPAD Vendor: 054c Product: 0ce6)",
            ],
            type: CONTROLLER_TYPES.PLAYSTATION
        },
        4: {
            name: "Xbox",
            gamepad_api_ids: [
                "xinput",
                "Xbox Wireless Controller Extended Gamepad",
                "Xbox Wireless Controller",
            ],
            type: CONTROLLER_TYPES.XBOX
        },
        5: {
            name: "Xbox 360",
            gamepad_api_ids: [
            ],
            type: CONTROLLER_TYPES.XBOX
        },
        6: {
            name: "Xbox One/Series",
            gamepad_api_ids: [
                "HID-compliant game controller (STANDARD GAMEPAD Vendor: 045e Product: 0b13)",
            ],
            type: CONTROLLER_TYPES.XBOX
        },
        7: {
            name: "Nintendo Switch Pro Controller",
            gamepad_api_ids: [
                "Pro Controller (STANDARD GAMEPAD Vendor: 057e Product: 2009)",
            ],
            type: CONTROLLER_TYPES.SWITCH
        },
        8: {
            name: "Stadia Controller",
            gamepad_api_ids: [
                "Stadia Controller rev. A (STANDARD GAMEPAD Vendor: 18d1 Product: 9400)",
            ],
            type: CONTROLLER_TYPES.STANDARD,
        },
        9: {
            name: "SNES Gamepad",
            gamepad_api_ids: [
                "usb gamepad (Vendor: 0810 Product: e501)",
            ],
            type: CONTROLLER_TYPES.STANDARD,
        }
    };

    // Build a lookup map for fast exact matching
    const exactIdLookup = {};
    Object.values(exactGamepadMappings).forEach(mapping => {
        mapping.gamepad_api_ids.forEach(id => {
            exactIdLookup[id] = mapping;
        });
    });

    // Controller mappings with regex patterns for fallback detection
    const controllerMappings = {
        // Xbox controllers
        [CONTROLLER_TYPES.XBOX]: {
            buttonMap: {
                0: 'A',
                1: 'B',
                2: 'X',
                3: 'Y',
                4: 'LB',
                5: 'RB',
                6: 'LT',
                7: 'RT',
                8: 'Back',
                9: 'Start',
                10: 'LS',
                11: 'RS',
                12: 'DUp',
                13: 'DDown',
                14: 'DLeft',
                15: 'DRight',
                16: 'Home',
            },
            axisMap: {
                0: 'Left Stick X',
                1: 'Left Stick Y',
                2: 'Right Stick X',
                3: 'Right Stick Y'
            }
        },

        // PlayStation controllers
        [CONTROLLER_TYPES.PLAYSTATION]: {
            buttonMap: {
                0: '×',
                1: '○',
                2: '□',
                3: '△',
                4: 'L1',
                5: 'R1',
                6: 'L2',
                7: 'R2',
                8: 'Share',
                9: 'Options',
                10: 'L3',
                11: 'R3',
                12: 'DUp',
                13: 'DDown',
                14: 'DLeft',
                15: 'DRight',
                16: 'PS',
                17: 'TouchPad',
            },
            axisMap: {
                0: 'Left Stick X',
                1: 'Left Stick Y',
                2: 'Right Stick X',
                3: 'Right Stick Y'
            }
        },

        // Nintendo Switch controllers
        [CONTROLLER_TYPES.SWITCH]: {
            buttonMap: {
                0: 'B',
                1: 'A',
                2: 'Y',
                3: 'X',
                4: 'L',
                5: 'R',
                6: 'ZL',
                7: 'ZR',
                8: 'Minus',
                9: 'Plus',
                10: 'LS',
                11: 'RS',
                12: 'DUp',
                13: 'DDown',
                14: 'DLeft',
                15: 'DRight',
                16: 'Home',
                17: 'Capture',
            },
            axisMap: {
                0: 'Left Stick X',
                1: 'Left Stick Y',
                2: 'Right Stick X',
                3: 'Right Stick Y'
            }
        },

        // Default mapping for unknown controllers
        [CONTROLLER_TYPES.STANDARD]: {
            buttonMap: {},  // Will use index numbers by default
            axisMap: {
                0: 'Axis 0',
                1: 'Axis 1',
                2: 'Axis 2',
                3: 'Axis 3'
            }
        }
    };

    // Check if Gamepad API is supported
    function isSupported() {
        return !!navigator.getGamepads;
    }

    // Get gamepad info - combines exact matching with regex fallback
    function getGamepadInfo(gamepadId) {
        if (!gamepadId) {
            return {
                type: CONTROLLER_TYPES.STANDARD,
                name: 'Generic Controller'
            };
        }

        // Check for exact match first
        const exactMatch = exactIdLookup[gamepadId];
        if (exactMatch) {
            return {
                type: exactMatch.type,
                name: exactMatch.name
            };
        }

        return {
            type: CONTROLLER_TYPES.STANDARD,
            name: 'Generic Controller'
        };
    }

    // Detect controller type from gamepad ID (simplified version for backward compatibility)
    function detectControllerType(gamepadId) {
        return getGamepadInfo(gamepadId).type;
    }

    // Get button name for given controller type and button index
    function getButtonName(controllerType, buttonIndex) {
        const mapping = controllerMappings[controllerType] || controllerMappings[CONTROLLER_TYPES.STANDARD];
        return mapping.buttonMap[buttonIndex] || `B${buttonIndex}`;
    }

    // Get axis name for given controller type and axis index
    function getAxisName(controllerType, axisIndex) {
        const mapping = controllerMappings[controllerType] || controllerMappings[CONTROLLER_TYPES.STANDARD];
        return mapping.axisMap && mapping.axisMap[axisIndex] || `Axis ${axisIndex}`;
    }

    /// Detect if vibration is supported and return support information
    function isVibrationSupported(gamepad) {
        if (!gamepad || !gamepad.vibrationActuator) return false;

        // Return true for any actuator type, not just dual-rumble
        return true;
    }

    // Get detailed information about vibration capabilities
    function getVibrationCapabilities(gamepad) {
        if (!gamepad || !gamepad.vibrationActuator) {
            return { supported: false, type: null };
        }

        return {
            supported: true,
            type: gamepad.vibrationActuator.type || 'unknown'
        };
    }

    // Apply vibration if supported, with adaptive behavior for different actuator types
    function vibrate(gamepad, options = {}) {
        const { weakMagnitude = 0.5, strongMagnitude = 0.5, duration = 1000, startDelay = 0 } = options;

        if (!gamepad || !gamepad.vibrationActuator) {
            return Promise.reject(new Error('Vibration not supported on this gamepad'));
        }

        const actuator = gamepad.vibrationActuator;
        const actuatorType = actuator.type || 'unknown';

        // Different handling based on actuator type
        switch (actuatorType) {
            case 'dual-rumble':
                return actuator.playEffect("dual-rumble", {
                    startDelay: startDelay,
                    duration: duration,
                    weakMagnitude: weakMagnitude,
                    strongMagnitude: strongMagnitude
                });

            case 'vibration':
                // Some devices just have a simple vibration effect
                const magnitude = Math.max(weakMagnitude, strongMagnitude);
                return actuator.playEffect("vibration", {
                    startDelay: startDelay,
                    duration: duration,
                    magnitude: magnitude
                });

            default:
                // Try the default effect type for unknown actuators
                try {
                    return actuator.playEffect(actuator.type, {
                        startDelay: startDelay,
                        duration: duration,
                        weakMagnitude: weakMagnitude,
                        strongMagnitude: strongMagnitude,
                        magnitude: Math.max(weakMagnitude, strongMagnitude)
                    });
                } catch (e) {
                    console.warn(`Attempted to use unknown actuator type: ${actuatorType}`);
                    // Fallback to dual-rumble as it's the most common
                    return actuator.playEffect("dual-rumble", {
                        startDelay: startDelay,
                        duration: duration,
                        weakMagnitude: weakMagnitude,
                        strongMagnitude: strongMagnitude
                    });
                }
        }
    }

    // Stop vibration
    function stopVibration(gamepad) {
        if (gamepad && gamepad.vibrationActuator) {
            return vibrate(gamepad, { weakMagnitude: 0, strongMagnitude: 0 });
        }

        return Promise.reject(new Error('Vibration not supported on this browser or gamepad'));
    }

    // Get all connected gamepads
    function getConnectedGamepads() {
        if (!isSupported()) return [];

        const gamepads = navigator.getGamepads();
        return Array.from(gamepads).filter(gamepad => gamepad !== null);
    }

    // Public API
    return {
        isSupported,
        detectControllerType,
        getGamepadInfo,
        getButtonName,
        getAxisName,
        isVibrationSupported,
        getVibrationCapabilities,
        vibrate,
        stopVibration,
        getConnectedGamepads,
        CONTROLLER_TYPES
    };
})();
