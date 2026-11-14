# CubeCellMeshCore

MeshCore-compatible repeater firmware for Heltec CubeCell HTCC-AB01.

## Features

- **MeshCore Protocol Compatible** - Works with MeshCore Android/iOS apps
- **Ed25519 Signatures** - Compact implementation saves ~97KB Flash
- **ADVERT Broadcasting** - Node discovery and time synchronization
- **Packet Forwarding** - SNR-based CSMA/CA with weighted backoff
- **Remote Configuration** - Full CLI access via encrypted mesh channel (no USB needed)
- **Store-and-Forward Mailbox** - Stores messages for offline nodes, re-delivers on return
- **Mesh Health Monitor** - Automatic alerts when nodes go offline or links degrade
- **Low Power** - Deep sleep support with duty-cycle RX
- **Telemetry** - Battery voltage, node statistics
- **Neighbour Tracking** - Direct repeater discovery via 0-hop ADVERTs
- **Rate Limiting** - Protection against flood/spam attacks
- **Persistent Statistics** - Lifetime stats survive reboots (EEPROM)
- **Daily Reports** - Scheduled status reports sent to admin via mesh

## Hardware

- **Board**: Heltec CubeCell HTCC-AB01
- **MCU**: ASR6501 SiP (ARM Cortex-M0+ @ 48 MHz + SX1262)
- **Radio**: SX1262 LoRa transceiver (integrated, no FPU)
- **Flash**: 128 KB (97.9% used, ~2.7 KB free)
- **RAM**: 16 KB (48.3% static, ~8 KB free at runtime incl. stack/heap)
- **Deep sleep**: ~3.5 uA (MCU + radio)

## Quick Start

### Build
```bash
pio run
```

### Upload
```bash
pio run -t upload
```

### Monitor
```bash
pio device monitor -b 115200
```

## Radio Configuration

Default settings (EU868):

| Parameter | Value |
|-----------|-------|
| Frequency | 869.618 MHz |
| Bandwidth | 62.5 kHz |
| Spreading Factor | SF8 |
| Coding Rate | 4/8 |
| TX Power | 14 dBm |
| Sync Word | 0x12 |

## Serial Commands

Connect at 115200 baud. Type `help` for command list.

See [Command Reference](release/COMMANDS.md) for the full list of 80+ commands.

Commands use MeshCore-compatible naming (e.g. `set name`, `get name`, `set tx`, `password`). Legacy names are kept as aliases for backwards compatibility.

Key command categories: status, configuration, radio, network, mailbox, health monitor, daily report, alerts, rate limiting, ping/trace, identity/security, and system administration. All shared commands are available both via serial console and remotely via the MeshCore app's encrypted CLI.

## Project Structure

```
src/
├── main.cpp          # Main firmware (~3070 lines)
├── main.h            # Configuration and defines
├── core/             # Core modules
│   ├── globals.h/.cpp  # Global variables
│   ├── Led.h/.cpp      # LED signaling
│   └── Config.h/.cpp   # EEPROM config
└── mesh/             # MeshCore protocol
    ├── Advert.h      # ADVERT generation
    ├── Contacts.h    # Contact management
    ├── Crypto.h      # Encryption helpers
    ├── Identity.h    # Ed25519 keys
    ├── Mailbox.h     # Store-and-forward mailbox
    ├── Packet.h      # Packet format
    ├── Repeater.h    # Forwarding, sessions, rate limiting
    └── Telemetry.h   # Sensor data

lib/
└── ed25519/          # Compact Ed25519 implementation
    ├── ge_scalarmult_base_compact.c  # Memory-optimized
    └── precomp_Bi.h  # Small precomputed table (~1KB)

docs/
├── ARCHITECTURE.md   # System architecture
├── API.md            # API reference
└── TESTING.md        # Test procedures

tools/
├── serial_test.py    # Automated serial tests
├── quick_test.sh     # Quick test script
└── build_test.sh     # Build verification
```

## Documentation

- [Architecture Overview](docs/ARCHITECTURE.md) - System design and data flow
- [API Reference](docs/API.md) - Module APIs and serial commands
- [Testing Guide](docs/TESTING.md) - Test procedures and verification

## Testing

### Automated Serial Tests
```bash
# Install pyserial if needed
pip install pyserial

# Run tests (auto-detect port)
python tools/serial_test.py

# Run with specific port
python tools/serial_test.py /dev/ttyUSB0

# Run full test suite (includes state-changing tests)
python tools/serial_test.py --full
```

### Build Verification
```bash
./tools/build_test.sh
```

## MeshCore Compatibility

This firmware is compatible with:
- MeshCore Android app
- MeshCore iOS app
- Other MeshCore repeaters and nodes

### ADVERT Format
```
[PublicKey:32][Timestamp:4][Signature:64][Flags:1][Location?:8][Name:var]
```

Signature covers: `PublicKey + Timestamp + Appdata`

### Packet Format
```
[Header:1][PathLen:1][Path:var][Payload:var]
```

Note: `PayloadLen` is NOT transmitted - it's calculated from total packet length.

## Ed25519 Implementation

The firmware uses a custom compact Ed25519 implementation:

- **Memory Savings**: ~97KB Flash saved vs standard precomputed tables
- **Algorithm**: Double-and-add instead of windowed multiplication
- **Compatibility**: Produces signatures identical to orlp/ed25519

Key files:
- `ge_scalarmult_base_compact.c` - Compact base point multiplication
- `precomp_Bi.h` - Small table (~1KB) for verification only

## Power Management

| Mode | Description |
|------|-------------|
| Normal | Full operation, serial active |
| Deep Sleep | MCU sleeps between RX windows |
| Light Sleep | Brief delays with quick wake |

Commands:
- `powersaving on/off` - Quick switch: `on`=mode 2, `off`=mode 0
- `mode 0/1/2` - Set power mode (0=perf, 1=balanced, 2=powersave)
- `sleep on/off` - Enable/disable deep sleep
- `rxboost on/off` - Enable/disable RX gain boost

## Dependencies

- [RadioLib](https://github.com/jgromes/RadioLib) v6.6.0
- [Crypto](https://github.com/rweather/arduinolibs) v0.4.0

## Author

**Andrea Bernardi** - Project creator and lead developer

## License

MIT License - See LICENSE file for details.

## Acknowledgments

- [MeshCore](https://github.com/ripplebiz/MeshCore) - Protocol specification
- [orlp/ed25519](https://github.com/orlp/ed25519) - Ed25519 reference
- [RadioLib](https://github.com/jgromes/RadioLib) - LoRa library

## Changelog

### v0.7.0 (2026-03-06)
- **Enhanced Link Quality Statistics** - RSSI/SNR exponential moving averages (EMA)
  - New metrics: `rssiAvg`, `snrAvg`, `pktCount`, `pktCountWindow` per neighbour
  - 60-second measurement windows for packet rate tracking
  - Updated `neighbours` command shows current and average values
  - Base for future intelligent routing algorithms
- **Adaptive TX Power** - Automatic power adjustment based on neighbour SNR
  - Reduces power when SNR > +10dB (energy saving)
  - Increases power when SNR < -5dB (better coverage)
  - Range: 5-21 dBm, 2 dBm steps, evaluated every 60 seconds
  - Commands: `set tx auto on/off`, `txpower auto on/off`, `txpower <5-21>`
  - Enabled via serial CLI, disabled by default
- **Packet Deduplication** - Already implemented, documented
  - 32-slot ring buffer cache prevents duplicate forwarding
  - Automatic duplicate detection based on packet ID hash
  - Reduces network congestion and prevents loops
- **Test Suite** - New verification script `tools/test_adaptive_tx.py`
- See [FEATURES_ADDED.md](docs/FEATURES_ADDED.md) for detailed documentation

### v0.6.0 (2026-03-06)
- **Loop Detection System** - MeshCore 1.14 compatible configurable loop detection
  - Four modes: `off`, `minimal` (4+ occurrences), `moderate` (2+), `strict` (1, default)
  - New commands: `get loop.detect`, `set loop.detect {off|minimal|moderate|strict}`
  - Backward compatible: default mode `strict` maintains original behavior
  - Stored in EEPROM, survives reboots
  - Test script: `tools/test_loop_detect.py`
- **Max Hops Filter for Auto-add Contacts** - MeshCore 1.14 compatible hop count filtering
  - Limit auto-add of contacts by hop count (0 = no limit, 1-64 = max hops)
  - New commands: `get autoadd.maxhops`, `set autoadd.maxhops <0-64>`
  - Default: 0 (no limit, backward compatible)
  - Reduces contact list pollution from distant nodes
  - Test script: `tools/test_autoadd_maxhops.py`
- **Bug Fix: millis() Wraparound Handling** - Fixes scheduler issues after ~49.7 days uptime
  - Fixed timer comparisons in pendingAdvertTime, tempRadioExpireTime, and rebootTime
  - Uses signed subtraction to correctly handle millis() wraparound
  - Prevents scheduled tasks from getting stuck when millis() wraps around

### v0.5.2 (2026-02-15)
- **Extended MeshCore CLI** - 23 new commands for full MeshCore standard compatibility
  - `get` aliases: `get name`, `get lat`, `get lon`, `get tx`, `get radio`, `get freq`, `get repeat`, `get flood.max`, `get advert.interval`, `get guest.password`, `get public.key`
  - Radio: `set freq <MHz>`, `tempradio` with timeout (5th parameter in minutes)
  - Identity: `get/set prv.key` (Ed25519 seed backup/restore), `get/set owner.info`
  - Tuning: `get/set af` (airtime factor), `get/set adc.multiplier` (battery ADC calibration)
  - Forwarding: `get/set txdelay`, `get/set rxdelay`, `get/set direct.txdelay`
  - Network: `get/set flood.advert.interval`, `get/set agc.reset.interval`
  - Security: `setperm <pubkey> <perm>` (per-node ACL permissions)
  - Stats aliases: `stats-core`, `stats-radio`, `stats-packets`
  - System: `board`, `clkreboot`, `log`/`log start`/`log stop`/`log erase`

### v0.5.1 (2026-02-15)
- **MeshCore CLI Compatibility** - Renamed commands to match MeshCore standard naming
  - `set name`, `set lat`, `set lon`, `set tx`, `set advert.interval`, `password`, `set guest.password`
  - New commands: `ver`, `clock`, `powersaving on/off`, `clear stats`, `neighbor.remove`, `set radio`, `erase`
  - All legacy command names kept as aliases for backwards compatibility

### v0.5.0 (2026-02-14)
- **Store-and-Forward Mailbox** - Messages for offline nodes stored and re-delivered automatically
  - 2 persistent EEPROM slots + 4 volatile RAM overflow slots = 6 messages max
  - 24h TTL, automatic cleanup, triggered by ADVERT from returning node
- **Mesh Health Monitor** - Automatic alerts when nodes go offline (>30 min)
  - Per-node SNR tracking (EMA), offline detection, chat node impersonation for alert delivery
- **Full Remote Configuration** - All CLI commands available via encrypted mesh channel
  - sleep, rxboost, mode, alert, report, ratelimit, mailbox - all remotely accessible
- **Session security** - Idle sessions now expire after 1 hour
- **Code optimization** - Merged duplicate CLI handlers, eliminated float parsing
  - Saved 12.9 KB Flash by removing strtod/scanf float dependency chain
  - Flash usage: 91.0% (was 98.2% in v0.4.0)

### v0.3.5 (2026-01-30)
- Added temporary radio parameters (`tempradio` command)
- Change frequency, bandwidth, SF, CR without saving to EEPROM
- Useful for testing and debugging radio configurations
- New commands: `radio`, `tempradio <freq> <bw> <sf> <cr>`, `tempradio off`

### v0.3.4 (2026-01-30)
- Fixed remote CLI commands via mesh network
- Added TXT_MSG (MC_PAYLOAD_PLAIN) handler for CLI commands
- Now supports both REQUEST and TXT_MSG methods for remote CLI
- Added detailed logging for authentication and CLI processing

### v0.3.3 (2026-01-23)
- Added persistent statistics stored in EEPROM
- Tracks lifetime: RX/TX/FWD packets, unique nodes, logins, uptime
- Auto-save every 5 minutes with CRC16 integrity
- New commands: `lifetime`, `savestats`
- Boot counter for reliability monitoring

### v0.3.2 (2026-01-23)
- Added rate limiting for login, request, and forwarding
- Protection against brute-force login attacks (5/min)
- Protection against request spam (30/min)
- Protection against network flooding (100/min forward)
- New serial command: `ratelimit [on|off|reset]`

### v0.3.1 (2026-01-23)
- Added neighbour tracking for direct (0-hop) repeater ADVERTs
- Fixed GET_NEIGHBOURS response format for MeshCore app compatibility
- Neighbours now visible in MeshCore app "Manage > Neighbours" section

### v0.3.0 (2026-01-23)
- Fixed MeshCore login protocol compatibility
- Fixed telemetry response format (CayenneLPP with LPP_VOLTAGE)
- Added TRACE packet support for ping functionality
- Improved packet routing (don't forward packets addressed to us)
- Reduced debug output and Flash usage (94.1%)
- Telemetry now reports node count

### v0.2.x (2026-01-22)
- Authentication system with Ed25519 key exchange
- AES-128-ECB encrypted session support
- Remote CLI commands via mesh network
- Node discovery and tracking

### v0.1.0 (2026-01-22)
- Initial release
- MeshCore-compatible ADVERT packets
- Ed25519 signatures working
- Packet forwarding with CSMA/CA
- Serial console commands
