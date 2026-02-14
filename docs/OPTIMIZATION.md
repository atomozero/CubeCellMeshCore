# CubeCellMeshCore Memory Optimization Guide

## Current Status (After Optimizations)
- **Flash**: 128,728 / 131,072 bytes (98.2%)
- **RAM**: 8,136 / 16,384 bytes (49.7%)
- **Available**: ~2,344 bytes Flash
- **Daily Report**: Disabled via `#define ENABLE_DAILY_REPORT`
- **Note**: RAM 49.7% is static only; ~4 KB free at runtime (incl. stack/heap)

## Implemented Optimizations

| ID | Optimization | Savings | Status |
|----|--------------|---------|--------|
| OPT-1 | Debug printf wrapped in `#ifdef DEBUG_VERBOSE` | 216 bytes Flash | ✅ Done |
| OPT-2 | Crypto test vectors in `#ifdef ENABLE_CRYPTO_TESTS` | 768 bytes Flash | ✅ Done |
| OPT-3 | Reduced buffer sizes (cmdBuffer 64→48, cliResponse 128→96) | 16 bytes RAM | ✅ Done |
| OPT-4 | ANSI_COLORS disabled by default | 920 bytes Flash | ✅ Done |
| OPT-5 | Fancy display already disabled via LITE_MODE | N/A | ✅ Already |
| OPT-6 | Help strings compacted | 120 bytes Flash | ✅ Done |
| OPT-7 | RadioLib exclude unused modules/protocols | 776 bytes Flash, 280 bytes RAM | ✅ Done |
| OPT-8 | ANSI box-drawing tables removed (single processCommand) | N/A (dead code) | ✅ Done |
| OPT-9 | Daily report separated from LITE_MODE (`ENABLE_DAILY_REPORT`) | Independent flag | ✅ Done |
| OPT-10 | `battdebug` command behind `ENABLE_BATTDEBUG` flag | 488 bytes Flash | ✅ Done |
| OPT-11 | Dead code removal (`sendAdvertNoFlags`) | 48 bytes Flash | ✅ Done |
| OPT-12 | Shortened strings in processRemoteCommand + alert commands | 288 bytes Flash | ✅ Done |
| OPT-13 | Reduced stack buffers (reportText, plaintext, encrypted, cmdStr) | Stack only | ✅ Done |
| OPT-14 | Float→integer: battery ADC, CayenneLPP, coordinates, airtime | ~200-400 bytes Flash | ✅ Done |
| OPT-15 | EEPROM save interval 5min→30min (48 writes/day vs 288) | 0 bytes (flash wear) | ✅ Done |

**Total Flash saved vs baseline: ~3,000-3,200 bytes (before daily report)**

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

### 4. ✅ DONE - ANSI box-drawing tables removed
The dual `processCommand()` (minimal + fancy ANSI) was consolidated into a single compact version.
All ANSI box-drawing characters (`┌─┐│└─┘├┤`) removed from the codebase.

### 4b. ✅ DONE - RadioLib unused modules excluded
Added `-D RADIOLIB_EXCLUDE_*` flags in `platformio.ini` for all unused radio modules (CC1101, NRF24, RF69, SX1231, SI443X, RFM2X, SX127X, SX128X, LR11X0, STM32WLX) and protocols (AFSK, AX25, Hellschreiber, Morse, RTTY, SSTV, APRS, FSK4, Pager, Bell, DirectReceive, LoRaWAN). Saved 776 bytes Flash.

### 4c. ✅ DONE - Daily report separated from LITE_MODE
Added `#define ENABLE_DAILY_REPORT` flag independent of LITE_MODE. Daily report functionality (generateReportContent, sendDailyReport, checkDailyReport, serial commands) now has its own compile flag.

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

### Phase 2: RadioLib + ANSI Cleanup ✅ COMPLETED
1. ✅ RadioLib exclude unused modules (-776 bytes Flash, -280 bytes RAM)
2. ✅ ANSI box-drawing tables removed (single compact processCommand)
3. ✅ Daily report enabled via `ENABLE_DAILY_REPORT` flag

### Phase 3: Future Optimization (if needed)
1. Create PROGMEM string table for LOG messages (~2KB)
2. Reduce array sizes (MAX_NEIGHBOURS, MC_MAX_CONTACTS) (~500 bytes RAM)
3. Feature flag for REMOTE_CLI (~1KB)

### Results Achieved
- **Baseline**: Flash 127,476 bytes (97.3%), RAM 8,408 bytes (51.3%)
- **After RadioLib exclusion**: Flash 126,700 bytes (96.7%), RAM 8,128 bytes (49.6%)
- **After ANSI removal + new commands**: Flash 127,804 bytes (97.5%)
- **After daily report enabled**: Flash 129,388 bytes (98.7%)
- **After moderate optimizations**: Flash 128,444 bytes (98.0%), RAM 8,136 bytes (49.7%)
- **After daily report disabled + directed ping**: Flash 128,068 bytes (97.7%)
- **After trace command + float→int**: Flash 128,728 bytes (98.2%)
- **Available**: ~2,344 bytes Flash

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
