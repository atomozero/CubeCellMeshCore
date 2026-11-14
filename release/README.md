# CubeCellMeshCore v0.8.0

MeshCore-compatible repeater firmware for Heltec CubeCell HTCC-AB01.

## What's New in v0.8.0

- **No more lost traffic** - Packets whose path uses 2- or 3-byte hashes (MeshCore 1.11+) were dropped as `Bad pkt`; they are now parsed, deduplicated and forwarded correctly. Verified on air with 2-byte and 3-byte paths up to 9 hops
- **TRACE packets** forwarded as MeshCore does (direct only, SNR appended per hop)
- **Deduplication** now matches MeshCore (payload type + payload, path length only for TRACE)
- **`flood.max`** is enforced: it limits how far ADVERT floods travel (default 8)
- **Own ADVERT echo** no longer adds the repeater to its own node list or time sync
- **Security**
  - Fixed a stack overflow reachable by an unauthenticated login packet
  - Remote admin login is refused while the admin password is still the default `admin` (a warning is printed at boot)
  - `acl` shows passwords only to admins
- **Fixes** - EEPROM overlap between mailbox and region map; MeshCore aliases restored (`get tx`, `get radio`, `get repeat`, `get advert.interval`, `stats-radio`, `stats-packets`, `erase`)
- **Smaller firmware** - ~15 KB Flash free (was ~256 bytes) with direct messages enabled; source split into modules
- **Upgrade note** - the configuration layout changed: after flashing, settings return to defaults. Set name, passwords and radio options again

## Previous Features (v0.5.0-v0.7.0)

- **Enhanced Link Quality Statistics** - RSSI/SNR moving averages per neighbour
- **Adaptive TX Power** - `set tx auto on/off`, `txpower <5-21>`
- **DIRECT Routing Support** - Full support for DIRECT routed packets (path peeling)
- **Store-and-Forward Mailbox** - 2 persistent EEPROM + 4 volatile RAM slots
- **System Health Dashboard** - `health` command with vitals and problem nodes
- **Full Remote Configuration** - 50+ CLI commands via encrypted mesh channel
- **Loop Detection System** - Configurable modes: off, minimal, moderate, strict
- **Max Hops Filter** - Auto-add contacts limited by hop count (0=no limit)
- **Quiet Hours** - Night-time rate limiting for battery savings
- **Circuit Breaker** - Blocks DIRECT forwarding to degraded neighbours

## Features

- MeshCore protocol compatible (Android/iOS apps)
- Ed25519 identity and signatures
- ADVERT broadcasting with time synchronization
- SNR-based CSMA/CA packet forwarding
- **Enhanced link quality tracking with EMA** (v0.7.0+)
- **Adaptive TX power (5-21 dBm)** (v0.7.0+)
- **Packet deduplication cache** (documented v0.7.0+)
- Store-and-forward mailbox for offline nodes
- Mesh health monitoring with automatic alerts
- Full remote configuration via encrypted mesh CLI
- Deep sleep support (~20 uA)
- Battery and temperature telemetry
- Neighbour tracking with quality statistics
- Rate limiting (login, request, forward)
- Circuit breaker for degraded links
- Region-based flood filtering (MeshCore 1.10.0+ compatible)
- Persistent lifetime statistics (EEPROM)

## Hardware

- **Board**: Heltec CubeCell HTCC-AB01
- **MCU**: ASR6501 (ARM Cortex-M0+ @ 48 MHz + SX1262)
- **Flash**: 128 KB (88.5% used, ~15 KB free)
- **RAM**: 16 KB (52.4% used)
- **Radio**: SX1262 LoRa (EU868: 869.618 MHz, BW 62.5 kHz, SF8, CR 4/8)

## Files Included

| File | Description |
|------|-------------|
| `firmware.cyacd` | Flash image for CubeCellTool (Windows) |
| `firmware.hex` | Intel HEX format |
| `INSTALL.md` | Installation and first boot guide |
| `COMMANDS.md` | Full command reference (50+ commands) |
| `README.md` | This file |
| `README_VEN.md` | Sta pàgina qua in venessian |

## Quick Start

1. Flash `firmware.cyacd` via CubeCellTool or PlatformIO
2. Connect serial at 115200 baud
3. Set passwords: `password admin <pwd>` and `password guest <pwd>` then `save`
4. Set node name: `set name MyRepeater` then `save`
5. The node will start broadcasting ADVERTs and forwarding packets

See `INSTALL.md` for detailed instructions.

## Author

**Andrea Bernardi** - Project creator and lead developer

## Links

- Source: https://github.com/atomozero/CubeCellMeshCore
- MeshCore: https://github.com/meshcore-dev/MeshCore

## License

MIT License - See LICENSE file for details.
