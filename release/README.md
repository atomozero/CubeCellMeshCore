# CubeCellMeshCore v0.5.0

MeshCore-compatible repeater firmware for Heltec CubeCell HTCC-AB01.

## What's New in v0.5.0

- **DIRECT Routing Support** - Full support for DIRECT routed packets (path peeling). Fixes message forwarding between companion nodes through the repeater. Previously only FLOOD packets were forwarded.
- **Store-and-Forward Mailbox** - Messages for offline nodes are stored and automatically re-delivered when the node comes back online. 2 persistent EEPROM slots + 4 volatile RAM slots. Deduplication prevents storing the same packet from multiple repeaters.
- **System Health Dashboard** - `health` command shows system vitals (uptime, battery, sync), network status (online/offline count), subsystem status (mailbox, rate limiting, errors), and flags only problematic nodes.
- **Full Remote Configuration** - All 50+ CLI commands available remotely via the MeshCore app's encrypted channel. No USB cable needed to manage a deployed repeater.
- **Session Security** - Idle sessions now expire after 1 hour.
- **Loop Prevention** - FLOOD forwarding now checks if our hash is already in the path to prevent routing loops.
- **Major Code Optimization** - Merged duplicate CLI handlers and eliminated float parsing. Saved 12.9 KB Flash (was 98.2%, now 91.0%).
- **Quiet Hours** - Configurable night-time rate limiting (e.g., 22:00-06:00) reduces forward rate from 100 to 30 packets/min. Saves battery during low-traffic periods.
- **Circuit Breaker** - Automatically blocks DIRECT forwarding to neighbours with degraded links (SNR < -10dB). Auto-recovers after 5 min or on good SNR. FLOOD unaffected.
- **Adaptive TX Power** - Dynamically adjusts transmit power (5-14 dBm) based on average neighbour SNR. Reduces power when signal is strong, increases when weak.

## Features

- MeshCore protocol compatible (Android/iOS apps)
- Ed25519 identity and signatures
- ADVERT broadcasting with time synchronization
- SNR-based CSMA/CA packet forwarding
- Store-and-forward mailbox for offline nodes
- Mesh health monitoring with automatic alerts
- Full remote configuration via encrypted mesh CLI
- Daily status reports sent to admin
- Deep sleep support (~20 uA)
- Battery and temperature telemetry
- Neighbour tracking (direct 0-hop repeaters)
- Rate limiting (login, request, forward)
- Persistent lifetime statistics (EEPROM)

## Hardware

- **Board**: Heltec CubeCell HTCC-AB01
- **MCU**: ASR6501 (ARM Cortex-M0+ @ 48 MHz + SX1262)
- **Flash**: 128 KB (92.5% used, ~10 KB free)
- **RAM**: 16 KB (47.9% used)
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
3. Set passwords: `passwd admin <pwd>` then `save`
4. Set node name: `name MyRepeater` then `save`
5. The node will start broadcasting ADVERTs and forwarding packets

See `INSTALL.md` for detailed instructions.

## Links

- Source: https://github.com/atomozero/CubeCellMeshCore
- MeshCore: https://github.com/meshcore-dev/MeshCore
