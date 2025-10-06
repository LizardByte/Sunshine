# OTP (One-Time Password) Pairing Implementation

## Overview

This Sunshine fork adds OTP pairing support for automated client pairing without manual PIN entry. Based on Apollo's OTP implementation, adapted for Backbone integration.

## Changes Made

### Files Modified
- `src/nvhttp.h` - Added OTP function declarations and constant
- `src/nvhttp.cpp` - Implemented OTP generation and validation

### New API

#### `request_otp(passphrase, deviceName)`
Generates a 4-digit OTP PIN for pairing.

**Parameters:**
- `passphrase` (string) - Secret passphrase to validate the OTP (min 4 chars)
- `deviceName` (string) - Name of the device being paired

**Returns:** 4-digit PIN string

**Example:**
```cpp
std::string otp = nvhttp::request_otp("my_secret_passphrase", "Mobile Device");
// Returns: "1234" (random 4 digits)
```

#### OTP Pairing Flow

1. **Server generates OTP:**
   ```cpp
   std::string pin = nvhttp::request_otp("backbone_secret", "iPhone");
   // pin = "5338"
   // Expires in 180 seconds
   ```

2. **Client calculates hash:**
   ```
   hash = SHA256(pin + salt + passphrase)
   // e.g., hash("5338" + "abc123..." + "backbone_secret")
   ```

3. **Client sends otpauth parameter:**
   ```
   GET /pair?uniqueid=...&clientcert=...&salt=...&otpauth=<hash>&phrase=getservercert
   ```

4. **Server validates and auto-pairs:**
   - Checks OTP not expired (< 180s)
   - Computes expected hash
   - If match, calls `getservercert()` automatically
   - Clears OTP after use (single-use)

## Implementation Details

### OTP Expiration
- **Duration:** 180 seconds (3 minutes)
- **Constant:** `OTP_EXPIRE_DURATION` in `nvhttp.h`

### Security
- OTP is single-use (cleared after successful pairing)
- Requires both PIN and passphrase for validation
- Uses SHA256 hash with salt for validation
- Client must know passphrase (shared secret)

### Error Codes
- **503** - OTP not available or expired
- **Standard pairing errors** - Invalid hash, etc.

## Usage in Fuji

### Generate OTP when creating pairing session:
```typescript
// In Fuji's Sunshine service
const otp = await this.sunshineAPI.requestOTP(
  userBackboneId,  // passphrase
  'Backbone Mobile' // device name
)

// Include in QR code
const qrData = {
  sunshine: {
    url: 'https://192.168.1.100:47990',
    otp: otp.pin,
    passphrase: userBackboneId,
    expiresAt: Date.now() + 180000
  }
}
```

### Mobile validates and pairs:
```typescript
// Calculate hash
const hash = sha256(otp.pin + salt + otp.passphrase)

// Send to Sunshine
await fetch(`${sunshineUrl}/pair?uniqueid=${deviceId}&otpauth=${hash}&...`)
```

## Benefits

1. **No manual PIN entry** - Fully automated pairing
2. **Secure** - Requires shared secret (passphrase)
3. **Time-limited** - OTP expires after 3 minutes
4. **Single-use** - OTP cleared after successful pairing
5. **Backward compatible** - Falls back to standard PIN pairing if no OTP

## Branch Info

- **Branch:** `backbone/otp-pairing`
- **Base:** LizardByte/Sunshine main (commit 8372c37)
- **Commit:** 0d19a9e6 "Add OTP (One-Time Password) pairing support"
- **Status:** Not pushed to remote (local only)
