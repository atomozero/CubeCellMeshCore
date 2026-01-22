# CubeCellMeshCore Architecture

## Overview

CubeCellMeshCore is a MeshCore-compatible repeater firmware for Heltec CubeCell HTCC-AB01 boards. The architecture is designed to be modular, maintainable, and memory-efficient for the constrained 131KB Flash environment.

## Current Directory Structure

```
src/
├── main.cpp              # Main firmware (~3300 lines)
├── main.h                # Configuration defines, global declarations
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

## Planned Modular Structure (Future)

```
src/
├── main.cpp              # Entry point, setup(), loop()
├── main.h                # Configuration defines
│
├── core/                 # Core system modules
│   ├── Config.cpp/.h     # EEPROM configuration management
│   ├── Led.cpp/.h        # LED signaling (NeoPixel/GPIO)
│   └── Radio.cpp/.h      # SX1262 radio management
│
├── handlers/             # Packet and command handlers
│   ├── SerialCommands.*  # Serial console command processing
│   ├── RemoteCommands.*  # Remote CLI via mesh network
│   └── PacketHandler.*   # Incoming packet processing
│
├── services/             # High-level services
│   └── Messaging.*       # ADVERT, alerts, reports
│
└── mesh/                 # MeshCore protocol (unchanged)
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

## Memory Layout

### Flash (131KB total)
- Firmware code: ~130KB (99.3%)
- Ed25519 compact implementation saves ~97KB

### RAM (16KB total)
- Global state: ~8KB (49.5%)
- Stack and heap: ~8KB available

### EEPROM
- NodeConfig (0x00): Power settings, passwords, report config
- Identity (0x80): Ed25519 keys, node name, location

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
   - `sendPing()` - Test packet
   - `sendDailyReport()` - Scheduled reports
   - `sendNodeAlert()` - Node discovery alerts
