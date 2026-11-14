# CubeCellMeshCore Architecture

## Overview

CubeCellMeshCore is a MeshCore-compatible repeater firmware for Heltec CubeCell HTCC-AB01 boards. The architecture is designed to be modular, maintainable, and memory-efficient for the constrained 131KB Flash environment.

## Current Directory Structure

```
src/
├── main.cpp              # setup() and loop()
├── main.h                # Configuration defines + shared prototypes
│
├── cli/
│   └── Commands.cpp      # Serial + remote CLI (CmdCtx, dispatchSharedCommand,
│                         #   processCommand, processRemoteCommand)
├── radio/
│   └── Radio.cpp         # Radio setup/TX/RX, airtime/delays, sleep, watchdog
│
├── core/                 # Core system modules (extracted)
│   ├── globals.h         # Global variable declarations (extern)
│   ├── globals.cpp       # Global variable definitions
│   ├── Led.h/.cpp        # LED signaling (NeoPixel/GPIO)
│   └── Config.h/.cpp     # EEPROM configuration management
│
└── mesh/                 # MeshCore protocol implementation
    ├── Messaging.cpp     # Ping/trace, ADVERT beacon, direct msg, alerts, health
    ├── PacketHandler.cpp # processReceivedPacket, forwarding, discovery,
    │                     #   ANON_REQ login, authenticated requests, TXT CLI
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

#### Repeater.h
- Neighbour tracking with enhanced link quality statistics **(v0.7.0+)**
- Adaptive TX power control **(v0.7.0+)**
- Rate limiting (login, request, forward)
- Circuit breaker for degraded links
- Statistics tracking (RX/TX/FWD, radio metrics)
- ACL (Access Control List) management

**v0.7.0 Enhancements:**
- **NeighbourInfo**: Extended with RSSI/SNR exponential moving averages (EMA)
  - `rssiAvg`, `snrAvg`: Smoothed values (alpha=0.125)
  - `pktCount`, `pktCountWindow`: Total and windowed packet counting
  - Enables link quality monitoring and packet loss estimation
- **Adaptive TX Power**: Automatic power adjustment (5-21 dBm)
  - Evaluates every 60 seconds based on average neighbour SNR
  - Reduces power when SNR > +10dB (energy saving)
  - Increases power when SNR < -5dB (better coverage)
  - 2 dBm step size for gradual adaptation

#### Contacts.h
- Contact database with Ed25519 public keys
- Name-to-pubkey resolution
- RSSI/SNR tracking per contact
- Used for direct messaging and encryption

### Ed25519 Library (lib/ed25519/)

Custom implementation optimized for CubeCell:
- **Compact mode**: Uses double-and-add algorithm instead of precomputed tables
- **Memory savings**: ~97KB Flash saved vs standard implementation
- **Compact scalar arithmetic**: `sc.c` uses a loop-based modL (TweetNaCl) instead of
  orlp's unrolled code (~12.8KB Flash saved); results verified identical to orlp
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
- Firmware code: ~115 KB (87.9%), ~16 KB free
- Ed25519 compact implementation saves ~97KB vs standard tables

### RAM (16 KB total)
- Static globals (.data + .bss): ~8.4 KB (52.3% as reported by PlatformIO)
- Stack (PSoC 4000 default): ~2 KB
- Heap (RadioLib, Crypto): ~1-2 KB
- Free at runtime: ~4 KB (shared between stack growth and heap)

Crypto functions (Ed25519, AES) allocate 128-256 byte local buffers on the stack.
Deep call chains risk silent stack overflow.

### EEPROM (emulated, 576 bytes)
- Offset 0x000: NodeConfig (~112 bytes) - power, passwords, report/alert settings
- Offset 0x080: Identity (~132 bytes) - Ed25519 keys, node name, location
- Offset 0x118: PersistentStats (~50 bytes) - lifetime counters, CRC16
- Offset 0x154: Mailbox (172 bytes) - 2 persistent store-and-forward slots
- Offset 0x200: RegionMap (57 bytes) - 4 region entries + wildcard flags

Flash wear: auto-save every 30 min = ~48 writes/day = ~5.7 years at 100K cycles.

## Build Configurations

### Standard Build
Full features including ANSI-formatted serial output.

### LITE_MODE
Reduced memory footprint:
- Minimal serial output
- Simplified command handler

### SILENT Mode
No serial output, maximum power savings.

