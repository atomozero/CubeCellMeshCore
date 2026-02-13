# CubeCellMeshCore Architecture

## Overview

CubeCellMeshCore is a MeshCore-compatible repeater firmware for Heltec CubeCell HTCC-AB01 boards. The architecture is designed to be modular, maintainable, and memory-efficient for the constrained 131KB Flash environment.

## Current Directory Structure

```
src/
├── main.cpp              # Main firmware (~3070 lines)
├── main.h                # Configuration defines
│
├── core/                 # Core system modules (extracted)
│   ├── globals.h         # Global variable declarations (extern)
│   ├── globals.cpp       # Global variable definitions
│   ├── Led.h/.cpp        # LED signaling (NeoPixel/GPIO)
│   └── Config.h/.cpp     # EEPROM configuration management
│
└── mesh/                 # MeshCore protocol implementation
    ├── Advert.h          # ADVERT packet generation
    ├── Contacts.h        # Contact management
    ├── Crypto.h          # Encryption helpers
    ├── Identity.h        # Ed25519 key management
    ├── Packet.h          # Packet structure and serialization
    ├── Repeater.h        # Repeater statistics and helpers
    └── Telemetry.h       # Battery, temperature monitoring

lib/
└── ed25519/              # Compact Ed25519 implementation
    ├── ed25519_orlp.h    # Public API
    ├── ge_scalarmult_base_compact.c  # Memory-optimized base mult
    ├── precomp_Bi.h      # Small precomputed table (~1KB)
    └── ...               # Other ed25519 source files

docs/
├── ARCHITECTURE.md       # This file
└── API.md                # API reference
```

## Future Modular Structure

Additional modules can be extracted from main.cpp:

```
src/
├── handlers/             # Packet and command handlers
│   ├── SerialCommands.*  # Serial console command processing (~1100 lines)
│   ├── RemoteCommands.*  # Remote CLI via mesh network (~200 lines)
│   └── PacketHandler.*   # Incoming packet processing (~700 lines)
│
├── services/             # High-level services
│   └── Messaging.*       # ADVERT, alerts, reports (~500 lines)
│
└── core/
    └── Radio.cpp/.h      # SX1262 radio management
```

**Note**: Further module extraction requires careful handling of the CubeCell-specific
includes (`cyPm.c`, `innerWdt.h`) which contain function definitions and cannot be
included from multiple compilation units.

## Module Descriptions

### Core Modules

#### Config (core/Config.cpp)
- EEPROM load/save for persistent settings
- Power management (RxBoost, DeepSleep)
- Password storage for remote access
- Daily report and alert settings

#### Led (core/Led.cpp)
- NeoPixel WS2812 support via Vext power control
- GPIO13 LED fallback
- Signals: RX (green), TX (viola), Error (red)

#### Radio (core/Radio.cpp)
- SX1262 initialization and configuration
- Duty-cycle receive mode
- Packet transmission with timeout
- CSMA/CA timing calculations
- Error handling and auto-recovery

### Mesh Protocol (mesh/)

#### Identity.h
- Ed25519 keypair generation and storage
- MeshCore-compatible 64-byte private keys
- Signature generation and verification
- Node name and location management

#### Packet.h
- MeshCore packet format: `[header][pathLen][path][payload]`
- Serialize/deserialize functions
- Route types: FLOOD, DIRECT, TRANSPORT_*
- Payload types: ADVERT, REQ, RESPONSE, etc.

#### Advert.h
- ADVERT packet generation
- Time synchronization from network
- Appdata format: `[flags][location?][name]`
- Self-verification of signatures

### Ed25519 Library (lib/ed25519/)

Custom implementation optimized for CubeCell:
- **Compact mode**: Uses double-and-add algorithm instead of precomputed tables
- **Memory savings**: ~97KB Flash saved vs standard implementation
- **Compatibility**: Produces signatures identical to orlp/ed25519

## Data Flow

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   Radio     │────>│   Packet    │────>│   Handler   │
│  (SX1262)   │     │  Decoder    │     │  Dispatch   │
└─────────────┘     └─────────────┘     └─────────────┘
                                               │
                    ┌──────────────────────────┼──────────────────────────┐
                    │                          │                          │
                    v                          v                          v
             ┌─────────────┐           ┌─────────────┐           ┌─────────────┐
             │   ADVERT    │           │   REQUEST   │           │  FORWARD    │
             │   Handler   │           │   Handler   │           │   Queue     │
             └─────────────┘           └─────────────┘           └─────────────┘
                    │                          │                          │
                    v                          v                          v
             ┌─────────────┐           ┌─────────────┐           ┌─────────────┐
             │  TimeSync   │           │   Session   │           │   TX with   │
             │  Contacts   │           │   Manager   │           │   Backoff   │
             └─────────────┘           └─────────────┘           └─────────────┘
```

## Hardware: ASR6501 SiP

The ASR6501 is a System-in-Package combining:
- **CPU**: ARM Cortex-M0+ @ 48 MHz (ARMv6-M, no FPU, no HW divider)
- **Radio**: Semtech SX1262 (connected via internal SPI, not user-accessible)
- **Deep sleep**: ~3.5 uA (ILO 32 kHz keeps WDT/RTC running, full SRAM retention)

Key hardware constraints:
- Single SPI bus consumed by SX1262 (no external SPI peripherals)
- Single ADC channel (shared with VBAT measurement)
- No hardware FPU (all float ops are software-emulated)
- EEPROM emulated via flash pages (~100K erase cycles per page)
- `millis()` stops during `CySysPmDeepSleep()`, restored via `RtcGetTimerValue()`
- P4_1 (SPI MISO) must be set ANALOG during deep sleep to prevent current leakage

## Memory Layout

### Flash (128 KB total)
- Firmware code: ~128 KB (97.7%), ~3 KB free
- Ed25519 compact implementation saves ~97KB vs standard tables

### RAM (16 KB total)
- Static globals (.data + .bss): ~8.1 KB (49.7%)
- Stack (PSoC 4000 default): ~2 KB
- Heap (RadioLib, Crypto): ~1-2 KB
- Free at runtime: ~4 KB (shared between stack growth and heap)

Crypto functions (Ed25519, AES) allocate 128-256 byte local buffers on the stack.
Deep call chains risk silent stack overflow.

### EEPROM (emulated, 512 bytes)
- Offset 0x00: NodeConfig (~112 bytes) - power, passwords, report/alert settings
- Offset 0x80: Identity (~132 bytes) - Ed25519 keys, node name, location
- Offset 0x118: PersistentStats (~48 bytes) - lifetime counters, CRC16

Flash wear: auto-save every 5 min = ~288 writes/day = ~347 days at 100K cycles.
For long-term deployments, increase `STATS_SAVE_INTERVAL_MS`.

## Build Configurations

### Standard Build
Full features including ANSI-formatted serial output.

### LITE_MODE
Reduced memory footprint:
- Minimal serial output
- Simplified command handler

### SILENT Mode
No serial output, maximum power savings.

## Future Refactoring

The following modules are candidates for extraction from main.cpp:

1. **SerialCommands** (~1100 lines)
   - `processCommand()` - Serial console handler
   - `checkSerial()` - Serial input processing

2. **RemoteCommands** (~200 lines)
   - `processRemoteCommand()` - Mesh CLI handler

3. **PacketHandler** (~700 lines)
   - `processReceivedPacket()` - Main dispatcher
   - `processDiscoverRequest()` - Discovery handling
   - `processAuthenticatedRequest()` - Auth requests
   - `processAnonRequest()` - Anonymous requests

4. **Messaging** (~500 lines)
   - `sendAdvert()` - ADVERT transmission
   - `sendPing()` - Broadcast test packet
   - `sendDirectedPing()` / `sendPong()` - Directed ping/pong
   - `sendDailyReport()` - Scheduled reports
   - `sendNodeAlert()` - Node discovery alerts
