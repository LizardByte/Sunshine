/**
 * @brief Create one immutable Windows virtual-key selector option.
 *
 * @param {number} value Numeric virtual-key value.
 * @param {string} description Human-readable key description.
 * @return {{value: number, code: string, description: string}} Virtual-key selector option.
 */
function keyCode(value, description) {
  return Object.freeze({
    value,
    code: `0x${value.toString(16).toUpperCase().padStart(2, '0')}`,
    description,
  })
}

/**
 * @brief Microsoft-defined Windows virtual-key codes, ordered by numeric value.
 */
export const virtualKeyCodes = Object.freeze([
  keyCode(0x01, 'Left mouse button'),
  keyCode(0x02, 'Right mouse button'),
  keyCode(0x03, 'Control-Break'),
  keyCode(0x04, 'Middle mouse button'),
  keyCode(0x05, 'X1 mouse button'),
  keyCode(0x06, 'X2 mouse button'),
  keyCode(0x08, 'Backspace'),
  keyCode(0x09, 'Tab'),
  keyCode(0x0C, 'Clear'),
  keyCode(0x0D, 'Enter'),
  keyCode(0x10, 'Shift'),
  keyCode(0x11, 'Ctrl'),
  keyCode(0x12, 'Alt'),
  keyCode(0x13, 'Pause'),
  keyCode(0x14, 'Caps Lock'),
  keyCode(0x15, 'IME Kana / Hangul mode'),
  keyCode(0x16, 'IME On'),
  keyCode(0x17, 'IME Junja mode'),
  keyCode(0x18, 'IME final mode'),
  keyCode(0x19, 'IME Hanja / Kanji mode'),
  keyCode(0x1A, 'IME Off'),
  keyCode(0x1B, 'Escape'),
  keyCode(0x1C, 'IME Convert'),
  keyCode(0x1D, 'IME Nonconvert'),
  keyCode(0x1E, 'IME Accept'),
  keyCode(0x1F, 'IME mode change'),
  keyCode(0x20, 'Spacebar'),
  keyCode(0x21, 'Page Up'),
  keyCode(0x22, 'Page Down'),
  keyCode(0x23, 'End'),
  keyCode(0x24, 'Home'),
  keyCode(0x25, 'Left Arrow'),
  keyCode(0x26, 'Up Arrow'),
  keyCode(0x27, 'Right Arrow'),
  keyCode(0x28, 'Down Arrow'),
  keyCode(0x29, 'Select'),
  keyCode(0x2A, 'Print'),
  keyCode(0x2B, 'Execute'),
  keyCode(0x2C, 'Print Screen'),
  keyCode(0x2D, 'Insert'),
  keyCode(0x2E, 'Delete'),
  keyCode(0x2F, 'Help'),
  keyCode(0x30, '0'),
  keyCode(0x31, '1'),
  keyCode(0x32, '2'),
  keyCode(0x33, '3'),
  keyCode(0x34, '4'),
  keyCode(0x35, '5'),
  keyCode(0x36, '6'),
  keyCode(0x37, '7'),
  keyCode(0x38, '8'),
  keyCode(0x39, '9'),
  keyCode(0x41, 'A'),
  keyCode(0x42, 'B'),
  keyCode(0x43, 'C'),
  keyCode(0x44, 'D'),
  keyCode(0x45, 'E'),
  keyCode(0x46, 'F'),
  keyCode(0x47, 'G'),
  keyCode(0x48, 'H'),
  keyCode(0x49, 'I'),
  keyCode(0x4A, 'J'),
  keyCode(0x4B, 'K'),
  keyCode(0x4C, 'L'),
  keyCode(0x4D, 'M'),
  keyCode(0x4E, 'N'),
  keyCode(0x4F, 'O'),
  keyCode(0x50, 'P'),
  keyCode(0x51, 'Q'),
  keyCode(0x52, 'R'),
  keyCode(0x53, 'S'),
  keyCode(0x54, 'T'),
  keyCode(0x55, 'U'),
  keyCode(0x56, 'V'),
  keyCode(0x57, 'W'),
  keyCode(0x58, 'X'),
  keyCode(0x59, 'Y'),
  keyCode(0x5A, 'Z'),
  keyCode(0x5B, 'Left Windows logo'),
  keyCode(0x5C, 'Right Windows logo'),
  keyCode(0x5D, 'Application menu'),
  keyCode(0x5F, 'Computer Sleep'),
  keyCode(0x60, 'Numpad 0'),
  keyCode(0x61, 'Numpad 1'),
  keyCode(0x62, 'Numpad 2'),
  keyCode(0x63, 'Numpad 3'),
  keyCode(0x64, 'Numpad 4'),
  keyCode(0x65, 'Numpad 5'),
  keyCode(0x66, 'Numpad 6'),
  keyCode(0x67, 'Numpad 7'),
  keyCode(0x68, 'Numpad 8'),
  keyCode(0x69, 'Numpad 9'),
  keyCode(0x6A, 'Numpad Multiply'),
  keyCode(0x6B, 'Numpad Add'),
  keyCode(0x6C, 'Numpad Separator'),
  keyCode(0x6D, 'Numpad Subtract'),
  keyCode(0x6E, 'Numpad Decimal'),
  keyCode(0x6F, 'Numpad Divide'),
  keyCode(0x70, 'F1'),
  keyCode(0x71, 'F2'),
  keyCode(0x72, 'F3'),
  keyCode(0x73, 'F4'),
  keyCode(0x74, 'F5'),
  keyCode(0x75, 'F6'),
  keyCode(0x76, 'F7'),
  keyCode(0x77, 'F8'),
  keyCode(0x78, 'F9'),
  keyCode(0x79, 'F10'),
  keyCode(0x7A, 'F11'),
  keyCode(0x7B, 'F12'),
  keyCode(0x7C, 'F13'),
  keyCode(0x7D, 'F14'),
  keyCode(0x7E, 'F15'),
  keyCode(0x7F, 'F16'),
  keyCode(0x80, 'F17'),
  keyCode(0x81, 'F18'),
  keyCode(0x82, 'F19'),
  keyCode(0x83, 'F20'),
  keyCode(0x84, 'F21'),
  keyCode(0x85, 'F22'),
  keyCode(0x86, 'F23'),
  keyCode(0x87, 'F24'),
  keyCode(0x90, 'Num Lock'),
  keyCode(0x91, 'Scroll Lock'),
  keyCode(0xA0, 'Left Shift'),
  keyCode(0xA1, 'Right Shift'),
  keyCode(0xA2, 'Left Ctrl'),
  keyCode(0xA3, 'Right Ctrl'),
  keyCode(0xA4, 'Left Alt'),
  keyCode(0xA5, 'Right Alt'),
  keyCode(0xA6, 'Browser Back'),
  keyCode(0xA7, 'Browser Forward'),
  keyCode(0xA8, 'Browser Refresh'),
  keyCode(0xA9, 'Browser Stop'),
  keyCode(0xAA, 'Browser Search'),
  keyCode(0xAB, 'Browser Favorites'),
  keyCode(0xAC, 'Browser Home'),
  keyCode(0xAD, 'Volume Mute'),
  keyCode(0xAE, 'Volume Down'),
  keyCode(0xAF, 'Volume Up'),
  keyCode(0xB0, 'Next Track'),
  keyCode(0xB1, 'Previous Track'),
  keyCode(0xB2, 'Stop Media'),
  keyCode(0xB3, 'Play / Pause Media'),
  keyCode(0xB4, 'Launch Mail'),
  keyCode(0xB5, 'Launch Media Selector'),
  keyCode(0xB6, 'Launch Application 1'),
  keyCode(0xB7, 'Launch Application 2'),
  keyCode(0xBA, 'Semicolon / Colon'),
  keyCode(0xBB, 'Equals / Plus'),
  keyCode(0xBC, 'Comma / Less Than'),
  keyCode(0xBD, 'Minus / Underscore'),
  keyCode(0xBE, 'Period / Greater Than'),
  keyCode(0xBF, 'Slash / Question Mark'),
  keyCode(0xC0, 'Grave Accent / Tilde'),
  keyCode(0xC3, 'Gamepad A'),
  keyCode(0xC4, 'Gamepad B'),
  keyCode(0xC5, 'Gamepad X'),
  keyCode(0xC6, 'Gamepad Y'),
  keyCode(0xC7, 'Gamepad Right Shoulder'),
  keyCode(0xC8, 'Gamepad Left Shoulder'),
  keyCode(0xC9, 'Gamepad Left Trigger'),
  keyCode(0xCA, 'Gamepad Right Trigger'),
  keyCode(0xCB, 'Gamepad D-pad Up'),
  keyCode(0xCC, 'Gamepad D-pad Down'),
  keyCode(0xCD, 'Gamepad D-pad Left'),
  keyCode(0xCE, 'Gamepad D-pad Right'),
  keyCode(0xCF, 'Gamepad Menu / Start'),
  keyCode(0xD0, 'Gamepad View / Back'),
  keyCode(0xD1, 'Gamepad Left Thumbstick Button'),
  keyCode(0xD2, 'Gamepad Right Thumbstick Button'),
  keyCode(0xD3, 'Gamepad Left Thumbstick Up'),
  keyCode(0xD4, 'Gamepad Left Thumbstick Down'),
  keyCode(0xD5, 'Gamepad Left Thumbstick Right'),
  keyCode(0xD6, 'Gamepad Left Thumbstick Left'),
  keyCode(0xD7, 'Gamepad Right Thumbstick Up'),
  keyCode(0xD8, 'Gamepad Right Thumbstick Down'),
  keyCode(0xD9, 'Gamepad Right Thumbstick Right'),
  keyCode(0xDA, 'Gamepad Right Thumbstick Left'),
  keyCode(0xDB, 'Left Bracket / Brace'),
  keyCode(0xDC, 'Backslash / Pipe'),
  keyCode(0xDD, 'Right Bracket / Brace'),
  keyCode(0xDE, 'Apostrophe / Quotation Mark'),
  keyCode(0xDF, 'OEM 8 (keyboard-dependent)'),
  keyCode(0xE2, 'ISO Backslash / Pipe'),
  keyCode(0xE5, 'IME Process'),
  keyCode(0xE7, 'Unicode Packet'),
  keyCode(0xF6, 'Attn'),
  keyCode(0xF7, 'CrSel'),
  keyCode(0xF8, 'ExSel'),
  keyCode(0xF9, 'Erase EOF'),
  keyCode(0xFA, 'Play'),
  keyCode(0xFB, 'Zoom'),
  keyCode(0xFC, 'No Name (reserved)'),
  keyCode(0xFD, 'PA1'),
  keyCode(0xFE, 'OEM Clear'),
])

const virtualKeyCodeValues = new Set(virtualKeyCodes.map(option => option.code))
const virtualKeyCodesByValue = new Map(virtualKeyCodes.map(option => [option.value, option]))

/**
 * @brief Test whether a value is accepted by Sunshine's integer-list parser.
 *
 * @param {string} value Candidate decimal or hexadecimal key code.
 * @return {boolean} Whether the value can be serialized as a key code.
 */
export function isValidVirtualKeyCode(value) {
  return /^(?:-?\d+|0x[\da-fA-F]+)$/.test(String(value).trim())
}

/**
 * @brief Test whether a value exactly matches a canonical selector option.
 *
 * @param {string} value Candidate virtual-key code.
 * @return {boolean} Whether the complete selector already contains the value.
 */
export function hasVirtualKeyCodeOption(value) {
  return virtualKeyCodeValues.has(String(value).trim())
}

/**
 * @brief Find the standard description for a decimal or hexadecimal virtual-key value.
 *
 * @param {string} value Candidate virtual-key code.
 * @return {string|null} Standard description, or null for an unknown value.
 */
export function getVirtualKeyCodeDescription(value) {
  if (!isValidVirtualKeyCode(value)) {
    return null
  }

  const keyCodeValue = String(value).trim()
  const numericValue = keyCodeValue.startsWith('0x')
    ? Number.parseInt(keyCodeValue.slice(2), 16)
    : Number.parseInt(keyCodeValue, 10)
  return virtualKeyCodesByValue.get(numericValue)?.description ?? null
}
