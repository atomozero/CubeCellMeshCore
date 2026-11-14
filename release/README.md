# CubeCellMeshCore v0.7.0

MeshCore-compatible repeater firmware for Heltec CubeCell HTCC-AB01.

## What's New in v0.7.0

- **Enhanced Link Quality Statistics** - RSSI/SNR exponential moving averages (EMA) per neighbour
  - New metrics: `rssiAvg`, `snrAvg`, `pktCount`, `pktCountWindow`
  - 60-second measurement windows for packet rate tracking
  - Updated `neighbours` command shows current and average values
  - Enables link degradation detection and intelligent routing decisions
- **Adaptive TX Power** - Fully documented automatic power adjustment system
  - Evaluates every 60 seconds based on average neighbour SNR
  - SNR > +10dB → Reduces power by 2 dBm (energy saving)
  - SNR < -5dB → Increases power by 2 dBm (better coverage)
  - Range: 5-21 dBm with 2 dBm steps
  - Commands: `set tx auto on/off`, `txpower auto on/off`, `txpower <5-21>`
- **Packet Deduplication** - Documented existing 32-slot ring buffer cache
  - Prevents duplicate packet forwarding automatically
  - Reduces network congestion and prevents loops
- **Test Suite** - New verification script `tools/test_adaptive_tx.py`
- See [FEATURES_ADDED.md](../docs/FEATURES_ADDED.md) for detailed documentation

## Previous Features (v0.5.0-v0.6.0)

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
- Daily status reports sent to admin
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
- **Flash**: 128 KB (99.8% used, ~256 bytes free)
- **RAM**: 16 KB (53.1% used)
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
