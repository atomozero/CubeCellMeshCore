# CubeCellMeshCore Memory Optimization Guide

## Current Status (After Optimizations)
- **Flash**: 128,280 / 131,072 bytes (97.9%)
- **RAM**: 8,088 / 16,384 bytes (49.4%)
- **Available**: ~2,792 bytes Flash
- **Saved**: 2,024 bytes Flash, 16 bytes RAM

## Implemented Optimizations

| ID | Optimization | Savings | Status |
|----|--------------|---------|--------|
| OPT-1 | Debug printf wrapped in `#ifdef DEBUG_VERBOSE` | 216 bytes Flash | ✅ Done |
| OPT-2 | Crypto test vectors in `#ifdef ENABLE_CRYPTO_TESTS` | 768 bytes Flash | ✅ Done |
| OPT-3 | Reduced buffer sizes (cmdBuffer 64→48, cliResponse 128→96) | 16 bytes RAM | ✅ Done |
| OPT-4 | ANSI_COLORS disabled by default | 920 bytes Flash | ✅ Done |
| OPT-5 | Fancy display already disabled via LITE_MODE | N/A | ✅ Already |
| OPT-6 | Help strings compacted | 120 bytes Flash | ✅ Done |

**Total Implemented: 2,024 bytes Flash + 16 bytes RAM**

---

## Remaining Optimization Opportunities

| File | Lines | Potential Savings | Priority |
|------|-------|-------------------|----------|
| main.cpp | 3070 | ~2-3 KB remaining | MEDIUM |
| Repeater.h | 1271 | ~300-500 bytes | MEDIUM |
| Advert.h | 647 | ~100-200 bytes | LOW |
| Crypto.h | 588 | ~100 bytes | LOW |
| Contacts.h | 427 | ~200-300 bytes | MEDIUM |
| Identity.h | 364 | ~50 bytes | LOW |

**Remaining Potential Savings: 3-4 KB**

---

## FUTURE - main.cpp (~2-3 KB remaining)

### 1. ✅ DONE - Debug Serial.printf() wrapped in DEBUG_VERBOSE

### 2. LOG Strings to PROGMEM (2,000 bytes)
475 LOG() calls with embedded strings like:
```cpp
LOG(TAG_OK " RX Boost enabled\n\r");
LOG(TAG_ERROR " Identity not initialized\n\r");
```

**Fix**: Create PROGMEM string table:
```cpp
const char MSG_RX_BOOST[] PROGMEM = "RX Boost enabled";
LOG(TAG_OK " %S\n\r", MSG_RX_BOOST);  // %S for PROGMEM strings
```

### 3. ✅ DONE - ANSI Color Codes disabled via ANSI_COLORS flag

### 4. ✅ DONE - Fancy Display already disabled via LITE_MODE + MINIMAL_DEBUG

### 5. ✅ DONE - Test Vectors wrapped in ENABLE_CRYPTO_TESTS

### 6. ✅ DONE - Buffer Sizes Reduced
- `cmdBuffer[64]` → `[48]`
- `cliResponse[128]` → `[96]`

---

## MEDIUM PRIORITY - Repeater.h (~300-500 bytes)

### 1. Reduce MAX_NEIGHBOURS (200 bytes)
```cpp
#define MAX_NEIGHBOURS 50  // Each entry = 14 bytes = 700 bytes RAM!
```

**Fix**: Reduce to 16-20 neighbors for embedded use:
```cpp
#define MAX_NEIGHBOURS 16  // Saves 476 bytes RAM
```

### 2. Reduce MAX_ACL_ENTRIES (100 bytes)
```cpp
#define MAX_ACL_ENTRIES 16  // Each entry = 14 bytes = 224 bytes
```

**Fix**: Reduce to 8:
```cpp
#define MAX_ACL_ENTRIES 8  // Saves 112 bytes RAM
```

### 3. Inline Small Functions
Several small getters could be `inline`:
```cpp
inline uint8_t getCount() const { ... }
inline bool isRepeatEnabled() const { return repeatEnabled; }
```

---

## MEDIUM PRIORITY - Contacts.h (~200-300 bytes)

### 1. Reduce MC_MAX_CONTACTS (200 bytes)
```cpp
#define MC_MAX_CONTACTS 8  // Each Contact = 86 bytes = 688 bytes!
```

**Analysis**: Each Contact uses:
- pubKey: 32 bytes
- sharedSecret: 32 bytes
- name: 16 bytes
- lastSeen/rssi/snr/flags: 8 bytes
- **Total: 88 bytes × 8 = 704 bytes RAM**

**Fix**: Reduce to 4 contacts:
```cpp
#define MC_MAX_CONTACTS 4  // Saves 352 bytes RAM
```

### 2. Lazy Shared Secret Calculation
Don't store `sharedSecret[32]` in Contact struct - calculate on demand.
**Savings**: 32 × 8 = 256 bytes RAM

---

## LOW PRIORITY - Other Files (~200 bytes)

### Advert.h
- `TimeSync` class uses 32 bytes for consensus tracking that's rarely needed
- `AdvertInfo` struct could use bit flags instead of bools

### Crypto.h
- `uint8_t padded[256]` in `encryptThenMAC()` is large stack allocation
- Could reduce to 128 bytes for typical messages

### Identity.h
- `reserved[8]` in NodeIdentity could be removed (8 bytes EEPROM only)

---

## Quick Wins (Implement First)

### 1. Remove debug printf (5 minutes, 250 bytes)
```cpp
// In main.cpp, find and remove/wrap lines 2743-2751:
#ifdef DEBUG_VERBOSE
    Serial.printf("[RX-ADV] Raw ts bytes...");
#endif
```

### 2. Reduce array sizes (10 minutes, 500+ bytes RAM)
```cpp
// In globals.h:
#define MC_MAX_SEEN_NODES 8     // Was 16
#define MC_TX_QUEUE_SIZE  3     // Was 4
#define MC_PACKET_ID_CACHE 16   // Was 32

// In Repeater.h:
#define MAX_NEIGHBOURS 16       // Was 50
#define MAX_ACL_ENTRIES 8       // Was 16

// In Contacts.h:
#define MC_MAX_CONTACTS 4       // Was 8
```

### 3. Define PROGMEM strings for common messages (30 minutes, 500+ bytes)
```cpp
// Create new file: src/core/strings.h
const char STR_OK[] PROGMEM = "[OK]";
const char STR_ERR[] PROGMEM = "[E]";
const char STR_SAVED[] PROGMEM = "Saved to EEPROM";
// etc.
```

---

## Implementation Status

### Phase 1: Quick Wins ✅ COMPLETED
1. ✅ Debug Serial.printf() wrapped in DEBUG_VERBOSE (-216 bytes)
2. ✅ Buffer sizes reduced (-16 bytes RAM)
3. ✅ Test vectors wrapped in ENABLE_CRYPTO_TESTS (-768 bytes)
4. ✅ ANSI_COLORS disabled (-920 bytes)
5. ✅ Help strings compacted (-120 bytes)

### Phase 2: Future Optimization (if needed)
1. Create PROGMEM string table for LOG messages (~2KB)
2. Reduce array sizes (MAX_NEIGHBOURS, MC_MAX_CONTACTS) (~500 bytes RAM)
3. Feature flags for DAILY_REPORT, REMOTE_CLI (~1KB each)

### Results Achieved
- **Before**: Flash 130,304 bytes (99.4%), RAM 8,104 bytes (49.5%)
- **After**: Flash 128,280 bytes (97.9%), RAM 8,088 bytes (49.4%)
- **Saved**: 2,024 bytes Flash, 16 bytes RAM
- **Available**: ~2,792 bytes Flash for new features

---

## Commands to Check Size

```bash
# After changes, check size:
pio run 2>&1 | grep -E "(RAM|Flash):"

# Detailed size analysis:
pio run -t size

# Check specific sections:
arm-none-eabi-nm -S --size-sort .pio/build/cubecell_board/firmware.elf | tail -50
```
