# CubeCellMeshCore

Lightweight MeshCore mesh network repeater for Heltec CubeCell boards. Features Ed25519 authentication, encrypted messaging, daily status reports, and ultra-low power consumption.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange)](https://platformio.org/)
[![Status](https://img.shields.io/badge/Status-Experimental-red)](https://github.com/atomozero/CubeCellMeshCore)

## Disclaimer

> **This is an independent, experimental implementation.**
>
> - This project is **not affiliated with, endorsed by, or supported by** the official [MeshCore project](https://meshcore.co.uk/)
> - CubeCell/ASR6501 is **not officially supported** by MeshCore
> - **Interoperability with official MeshCore devices has not been fully tested on hardware**
> - Use at your own risk - this is experimental software

## About

This firmware implements a minimal MeshCore-compatible packet repeater for Heltec CubeCell boards. It's designed to extend mesh network coverage by forwarding flood-routed packets.

Based on the [MeshCore protocol](https://github.com/meshcore-dev/MeshCore) specification - a lightweight, hybrid routing mesh protocol for packet radios.

### Why This Project?

MeshCore officially supports ESP32, nRF52, RP2040, and STM32 platforms, but **not** the ASR6501/ASR6502 chips used in Heltec CubeCell boards. This project aims to:

- Provide MeshCore mesh coverage using low-cost CubeCell hardware
- Leverage CubeCell's excellent power efficiency for solar-powered repeaters
- Offer a simple, minimal repeater without the full MeshCore feature set

## Features

### Core Repeater
- **MeshCore Protocol**: Compatible with MeshCore packet format and flood routing
- **Ed25519 ADVERT**: Cryptographically signed node advertisements (visible on MeshCore map)
- **Node Identity**: Persistent Ed25519 keypair with auto-generated or custom names
- **Time Synchronization**: Automatic time sync from received ADVERTs (consensus-based re-sync)
- **GPS Location**: Optional location broadcast in ADVERT packets

### MeshCore Standard Features
- **Node Discovery**: Responds to discovery requests from MeshCore companion apps
- **Login/Auth**: Admin and guest passwords with X25519 ECDH + AES-128-ECB encryption
- **Session Management**: Up to 8 authenticated client sessions with replay protection
- **Authenticated Requests**: Handles GET_STATUS, GET_TELEMETRY, GET_NEIGHBOURS, etc.
- **Remote CLI**: Execute commands remotely via companion app (admin only)
- **Daily Report**: Automatic encrypted status messages to admin user
- **Neighbour Tracking**: Tracks up to 50 nearby repeaters with SNR/RSSI
- **Statistics**: Core stats, radio stats, packet stats (flood vs direct)
- **Telemetry**: CayenneLPP format telemetry (battery, temperature, GPS)
- **Packet Logging**: Ring buffer of last 32 packets for debugging
- **Region Filtering**: Optional transport code filtering (up to 4 regions)

### Additional Features
- **Seen Nodes Tracker**: Displays node names, RSSI, SNR, and packet counts
- **Packet Deduplication**: Caches recent packet IDs to prevent loops
- **SNR-Weighted TX Delay**: Fair channel access - strong signal nodes wait longer
- **Active Reception Detection**: Collision avoidance by detecting ongoing transmissions
- **Power Management**: Deep sleep (~3.5uA), RX duty cycle, configurable power modes
- **Watchdog Timer**: Automatic recovery from radio errors and system hangs
- **Persistent Configuration**: Settings and identity saved to EEPROM
- **Serial Console**: Status monitoring and configuration commands
- **LED Signaling**: Visual feedback via NeoPixel (red=no sync, blue=sync acquired, green=forward, violet=TX own)

## Limitations

This is a **repeater implementation** with some limitations:

- No BLE/WiFi/USB companion app interface
- No group management
- Cannot originate encrypted messages to other nodes (only LOGIN responses)
- Supports LOGIN authentication but not full encrypted messaging

For full MeshCore functionality, use [official MeshCore firmware](https://flasher.meshcore.co.uk/) on supported hardware.

## Hardware

### Supported Boards

| Board | Chip | Status |
|-------|------|--------|
| HTCC-AB01 (CubeCell Board) | ASR6501 | Tested |
| HTCC-AB01 Plus (with GPS) | ASR6502 | Should work |
| HTCC-AC01 (CubeCell Capsule) | ASR6501 | Should work |

### Hardware Notes

- **Radio**: SX1262 integrated in ASR650x SoC
- **Power**: Optimized for battery/solar operation
- **LED**: NeoPixel RGB on Vext-controlled power rail

## Compatibility

### Tested With

| Device | Firmware | Status |
|--------|----------|--------|
| CubeCell AB01 | This firmware | Working |
| *Other MeshCore devices* | *Not yet tested* | *Unknown* |

### Known Issues

- Interoperability with official MeshCore devices needs testing
- GPS functionality not implemented on AB01 Plus

**Please report compatibility test results!**

## Building

Requires [PlatformIO](https://platformio.org/).

```bash
# Clone the repository
git clone https://github.com/atomozero/CubeCellMeshCore.git
cd CubeCellMeshCore

# Build for CubeCell Board
pio run -e cubecell_board

# Build and upload
pio run -e cubecell_board -t upload

# Monitor serial output (115200 baud)
pio device monitor -b 115200
```

### Build Environments

```bash
pio run -e cubecell_board        # HTCC-AB01
pio run -e cubecell_board_plus   # HTCC-AB01 Plus
pio run -e cubecell_capsule      # HTCC-AC01
```

## Configuration

Edit `src/main.h` for compile-time settings.

### Region Selection

```cpp
#define MC_REGION_EU868     // Europe 868MHz (default)
//#define MC_REGION_US915   // USA 915MHz
//#define MC_REGION_AU915   // Australia 915MHz
```

### LoRa Parameters (MeshCore "narrow" preset)

| Parameter | EU868 | US915 | AU915 |
|-----------|-------|-------|-------|
| Frequency | 869.525 MHz | 910.525 MHz | 916.525 MHz |
| Bandwidth | 62.5 kHz | 62.5 kHz | 62.5 kHz |
| Spreading | SF7 | SF7 | SF7 |
| TX Power | 14 dBm | 20 dBm | 20 dBm |
| Sync Word | 0x12 | 0x12 | 0x12 |

### Other Options

```cpp
#define SILENT                      // Disable serial output
#define MC_SIGNAL_NEOPIXEL          // Use NeoPixel LED (default)
//#define MC_SIGNAL_GPIO13          // Use GPIO13 LED
#define MC_DEEP_SLEEP_ENABLED true  // Enable deep sleep
#define MC_WATCHDOG_ENABLED true    // Enable watchdog timer
```

## Serial Commands

Connect at 115200 baud. Settings are automatically saved to EEPROM.

| Command | Description |
|---------|-------------|
| `status` | Firmware version, node ID, radio config, uptime |
| `stats` | RX/TX/FWD packet counts and errors |
| `rssi` | Last received RSSI and SNR |
| `nodes` | List of seen nodes with name/RSSI/SNR/packet count |
| `ping` | Send a test packet (FLOOD/PLAIN) |
| `advert` | Send ADVERT packet (flood) |
| `advert local` | Send ADVERT packet (local/zero-hop) |
| `advert interval` | Show current ADVERT beacon interval |
| `advert interval <s>` | Set ADVERT interval in seconds (60-86400) |
| `advert debug` | Show raw ADVERT packet bytes for debugging |
| `identity` | Show node identity (name, hash, public key, time sync status) |
| `identity reset` | Generate new Ed25519 keypair |
| `name <name>` | Set node name (1-15 characters) |
| `location <lat> <lon>` | Set GPS coordinates (e.g., `location 45.464 9.191`) |
| `location` | Show current location |
| `location clear` | Remove GPS location |
| `time` | Show current time and sync status |
| `time <timestamp>` | Set Unix timestamp manually (e.g., `time 1737312000`) |
| `telemetry` | Show battery voltage, uptime, statistics |
| `power` | Current power saving settings |
| `rxboost on/off` | Toggle RX boost mode |
| `deepsleep on/off` | Toggle deep sleep |
| `mode 0/1/2` | Power mode: 0=Performance, 1=Balanced, 2=PowerSave |
| `neighbours` | List tracked repeater neighbours with SNR/RSSI |
| `radiostats` | Show radio statistics (avg/min/max RSSI/SNR) |
| `packetstats` | Show packet statistics (flood vs direct) |
| `acl` | Show ACL (Access Control List) entries |
| `repeat` | Show repeat mode settings |
| `report` | Show daily report settings |
| `report on/off` | Enable/disable daily report |
| `report time HH:MM` | Set report time (e.g., `report time 08:00`) |
| `report test` | Send a test report now |
| `report clear` | Clear destination and disable |
| `set password <pwd>` | Set admin password (max 15 chars) |
| `set guest <pwd>` | Set guest password (max 15 chars) |
| `set repeat on/off` | Enable/disable packet repeating |
| `set flood.max <n>` | Set max flood path length (1-64) |
| `log` | Show last 32 logged packets |
| `log clear` | Clear packet log |
| `save` | Manually save configuration |
| `reset` | Reset to default settings |
| `reboot` | Reboot the node |
| `help` | List all commands |

### Remote CLI Commands

When connected via MeshCore companion app with admin authentication, you can execute commands remotely using the `REQ_TYPE_SEND_CLI` request type.

**Read-only commands** (available to admin):
| Command | Description |
|---------|-------------|
| `status` | Node firmware, name, hash, uptime |
| `stats` | RX/TX/FWD packet counts |
| `time` | Current timestamp and sync status |
| `telemetry` | Battery voltage, uptime |
| `nodes` | List seen nodes with RSSI/SNR |
| `neighbours` | Count of tracked neighbours |
| `repeat` | Current repeat mode and max hops |
| `identity` | Node name, hash, location |
| `power` | Power mode, RX boost, deep sleep |

**Admin-only commands** (modify settings):
| Command | Description |
|---------|-------------|
| `set repeat on/off` | Enable/disable repeating |
| `set flood.max <n>` | Set max flood hops (1-64) |
| `rxboost on/off` | Toggle RX boost mode |
| `deepsleep on/off` | Toggle deep sleep |
| `mode 0/1/2` | Set power mode |
| `name <name>` | Set node name |
| `location <lat> <lon>` | Set GPS coordinates |
| `location clear` | Clear GPS location |
| `save` | Save configuration to EEPROM |
| `reset` | Reset to default settings |
| `reboot` | Reboot the node |

**Note**: The `reboot` command triggers a delayed reboot (500ms) to allow the response to be sent first.

### Daily Report

The daily report feature sends an encrypted status message to a configured admin user at a specified time each day. This allows remote monitoring of repeater health without requiring manual checks.

**Setup:**
1. Login to the repeater from the MeshCore companion app using the **admin password**
2. The repeater automatically captures your public key as the report destination
3. Enable the report with `report on` via serial console
4. Optionally set the time with `report time HH:MM` (default: 08:00)

**Report content:**
```
NodeName: Daily Report
Uptime: XXh
RX:X TX:X FWD:X ERR:X
Batt: XmV
```

**Commands:**
| Command | Description |
|---------|-------------|
| `report` | Show current settings (enabled, time, destination) |
| `report on` | Enable daily report (requires destination key) |
| `report off` | Disable daily report |
| `report time HH:MM` | Set report time (24h format) |
| `report test` | Send a test report immediately |
| `report clear` | Clear destination key and disable |

**Notes:**
- The destination public key is automatically captured when an admin logs in from the app
- Reports are sent as encrypted FLOOD packets (MC_PAYLOAD_PLAIN)
- Time sync is required for scheduled reports to work
- Only one report is sent per day (tracked by day number)

### Example Session

```
╔══════════════════════════════════════════════════════════╗
║         CubeCellMeshCore Repeater v0.2.4                 ║
║         MeshCore Compatible - EU868 Region               ║
╚══════════════════════════════════════════════════════════╝

00:00:01 [CONFIG] Loaded from EEPROM
00:00:01 [SYSTEM] Watchdog timer enabled
00:00:01 [SYSTEM] Node ID: CCA78F13
00:00:01 [SYSTEM] Initializing identity...
00:00:01 [OK] Identity ready: ZeroRepeater (hash: B9)
00:00:01 [INFO] ADVERT beacon: enabled (interval: 300s)
00:00:01 [INFO] Telemetry initialized
00:00:01 [RADIO] Ready: 869.618 MHz  BW=62.5 kHz  SF8  CR=4/8  CRC=ON
00:00:01 [SYSTEM] Ready - listening for packets
00:00:01 [INFO] Waiting for time sync before sending ADVERT...

00:00:07 [RX] DR ADV path=0 len=117 rssi=-13dBm snr=12.0dB
00:00:07 [OK] Time synchronized! Unix: 1737312000
00:00:07 [INFO] Will send ADVERT in 5 seconds
00:00:07 [NODE] AtomoZero [CHAT] hash=0B loc=45.4641,9.1914
00:00:07 [NODE] New node discovered via ADVERT

00:00:12 [ADVERT] Sending scheduled ADVERT after time sync
00:00:12 [TX] FL ADV path=0 len=121
00:00:13 [TX] Complete
00:00:13 [ADVERT] Transmission successful

00:01:23 [RX] FL TXT path=2 len=45 rssi=-67dBm snr=8.0dB
00:01:23 [NODE] New relay node detected: 3C
00:01:23 [FWD] Queued for relay (path=3)
```

### Ping Command

Send a test packet to verify transmission:

```
ping
[PING] Sending #1...
[TX] Route=FL Type=TXT Path=1 Payload=25
[TX] Done
[PING] Sent OK
```

### Nodes Command

View all nodes that have been seen:

```
nodes
┌────────────────────────────────────────────────────────┐
│ SEEN NODES: 2                                          │
├──────┬────────────┬─────────┬─────────┬──────┬────────┤
│ Hash │   Name     │  RSSI   │   SNR   │ Pkts │   Ago  │
├──────┼────────────┼─────────┼─────────┼──────┼────────┤
│  0B  │ AtomoZero  │ -13dBm  │ 12.00dB │    5 │   12s  │
│  3C  │ Node-3C    │ -67dBm  │  8.00dB │    2 │   45s  │
└──────┴────────────┴─────────┴─────────┴──────┴────────┘
```

- **Hash**: 1-byte node identifier (first byte of public key for ADVERTs)
- **Name**: Node name from ADVERT, or "Node-XX" if not known
- **RSSI**: Last received signal strength (dBm)
- **SNR**: Last signal-to-noise ratio (dB)
- **Pkts**: Total packets received from this node
- **Ago**: Seconds since last packet

### Identity Command

View the node's cryptographic identity:

```
identity
┌────────────────────────────────┐
│        NODE IDENTITY           │
├────────────────────────────────┤
│ Name        ZeroRepeater       │
│ Hash        0xB9               │
│ PubKey      B9A78F13...        │
│ Flags       0x92               │
├────────────────────────────────┤
│ Location    45.4641,9.1914     │
├────────────────────────────────┤
│ ADVERT Int  300s               │
│ Next in     245s               │
├────────────────────────────────┤
│ Time        synced             │
└────────────────────────────────┘
```

Time sync status can be:
- **synced** (green): Time synchronized from received ADVERT
- **pending (1/2)** (yellow): Received different time, waiting for confirmation
- **not synced** (red): No time received yet

### Telemetry Command

View battery and system statistics:

```
telemetry
=== TELEMETRY ===
Battery: 4120mV (91%)
Temperature: 25C
Uptime: 01:23:45
RX: 127 TX: 89 FWD: 85 ERR: 0
Last: RSSI=-67dBm SNR=8.0dB
```

### ADVERT Command

Manually send an ADVERT packet (also sent automatically every 5 minutes):

```
advert
[ADVERT] Sending flood ADVERT (CC-A78F13)
[TX] FLOOD ADVERT path=0 len=109
[TX] Complete
[ADVERT] Transmission successful
```

## MeshCore Protocol

### Packet Format

```
[Header: 1 byte]
  - Bits 0-1: Route type (0-3)
  - Bits 2-5: Payload type (0-15)
  - Bits 6-7: Version (0-3)
[Path Length: 1 byte]
[Payload Length: 1 byte]
[Path: 0-64 bytes] - Node hashes
[Payload: 0-180 bytes]
```

### Route Types

| Value | Name | Forwarded |
|-------|------|-----------|
| 0x00 | TRANSPORT_FLOOD | Yes |
| 0x01 | FLOOD | Yes |
| 0x02 | DIRECT | No |
| 0x03 | TRANSPORT_DIRECT | No |

### Payload Types

| Value | Name | Description |
|-------|------|-------------|
| 0x00 | REQUEST | Encrypted request |
| 0x01 | RESPONSE | Response |
| 0x02 | PLAIN | Plain text |
| 0x03 | ACK | Acknowledgment |
| 0x04 | ADVERT | Node advertisement |
| 0x06 | ANON_REQ | Anonymous login request |
| 0x08 | PATH_RETURN | Returned path |
| 0x0B | CONTROL | Control/discovery |

### MeshCore Encryption (ANON_REQ/LOGIN)

MeshCore uses X25519 ECDH key exchange with AES-128-ECB encryption and truncated HMAC-SHA256 authentication.

**ANON_REQ Packet Format (Login Request):**
```
[dest_hash:1][ephemeral_pubkey:32][MAC:2][ciphertext:16+]
```
- `dest_hash`: First byte of destination node's public key
- `ephemeral_pubkey`: 32-byte X25519 ephemeral public key from client
- `MAC`: 2-byte truncated HMAC-SHA256 (first 2 bytes)
- `ciphertext`: AES-128-ECB encrypted payload (zero-padded to 16 bytes)

**Encrypted Payload (decrypted content):**
```
[timestamp:4][password:1-15 bytes][zero-padding]
```
- `timestamp`: 4-byte little-endian Unix timestamp
- `password`: Admin or guest password (max 15 characters)

**Key Derivation:**
```
1. Shared secret = Curve25519(my_private_key, ephemeral_pubkey)
2. AES key = SHA256(shared_secret)[0:16]
3. MAC key = SHA256(shared_secret)[16:32]
```

**Cipher Format (encrypt-then-MAC):**
```
[MAC:2][ciphertext]
```
- MAC is calculated over ciphertext only
- MAC is placed BEFORE ciphertext (not after)

## How It Works

### Time Synchronization

CubeCell boards don't have RTC or WiFi, so time is synchronized from received ADVERTs:

1. **Boot**: The repeater starts and waits for time sync (no ADVERT sent yet)
2. **First ADVERT received**: Time is synchronized immediately
3. **5 seconds later**: Own ADVERT is sent with valid timestamp
4. **Regular beacon**: ADVERT sent every 5 minutes (only if time is synced)

**Re-synchronization** (when already synced):
- If received time differs by > 5 minutes from our time, it's stored as "pending"
- If a second different time (matching the pending one) is received within 1 hour, time is re-synchronized
- This consensus mechanism prevents sync from a single faulty node

### Packet Forwarding Flow

```
1. Receive packet via LoRa
2. Validate packet structure
3. Check route type (only forward FLOOD types)
4. Check packet ID cache (skip duplicates)
5. Add own node hash to path
6. Calculate SNR-weighted TX delay
7. Wait, checking for channel activity
8. Transmit with updated path
```

### SNR-Weighted TX Delay

Nodes receiving with strong signal wait longer, giving priority to nodes with weaker signal (likely farther from source = better coverage).

```
SNR: -20dB to +15dB
Delay: 2-8 slot times (~56-224ms for SF7/62.5kHz)
```

### Error Recovery

- **Radio errors**: Reset radio after 5 consecutive errors
- **Total errors**: Reboot after 10 cumulative errors
- **Watchdog**: Automatic reboot on firmware hang

## Power Consumption

| State | Current | Notes |
|-------|---------|-------|
| Deep Sleep | ~3.5 uA | MCU + radio sleeping |
| RX (duty cycle) | ~2-5 mA | Periodic listening |
| RX (continuous) | ~6 mA | With RX boost off |
| TX (14 dBm) | ~80 mA | EU868 max |
| TX (20 dBm) | ~120 mA | US915/AU915 |

### Battery Life Estimates (1000mAh, Balanced mode)

| Traffic | Estimated Life |
|---------|---------------|
| Idle | ~30 days |
| 1 pkt/min | ~20 days |
| 10 pkt/min | ~7 days |

## Memory Usage

```
RAM:   42.3% (6936 / 16384 bytes)
Flash: 82.3% (107896 / 131072 bytes)
```

## Project Structure

```
CubeCellMeshCore/
├── platformio.ini          # Build configuration
├── README.md               # This file
├── LICENSE                 # MIT License
└── src/
    ├── main.h              # Configuration & declarations
    ├── main.cpp            # Main implementation
    └── mesh/
        ├── Packet.h        # MeshCore packet structure
        ├── Identity.h      # Ed25519 key management
        ├── Advert.h        # ADVERT packet generation
        ├── Telemetry.h     # Battery & system telemetry
        ├── Repeater.h      # MeshCore standard repeater features
        └── Crypto.h        # X25519 ECDH + AES-128-HMAC encryption
```

## Contributing

Contributions are welcome! Please:

1. Test with official MeshCore devices if possible
2. Report compatibility results (working or not)
3. Open issues for bugs or feature requests
4. Submit PRs for improvements

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## Testing Checklist

If you test this firmware, please report:

- [ ] Hardware used (board model, antenna)
- [ ] Region/frequency configuration
- [ ] Interoperability with official MeshCore devices
- [ ] Packet forwarding success rate
- [ ] Power consumption measurements
- [ ] Range/coverage observations
- [ ] Any errors or unexpected behavior

## Changelog

### v0.2.6 (Current)
- **Persistent Passwords** - Admin and guest passwords now saved to EEPROM:
  - Passwords survive device reboot
  - Default passwords: admin="admin", guest="guest"
  - Use `set password <pwd>` and `set guest <pwd>` to change
  - Use `reset` to restore default passwords
  - EEPROM config version upgraded to v2

### v0.2.5
- **Node Discovery** - Repeater now visible to MeshCore companion apps:
  - Responds to `CONTROL` packets with `CTL_TYPE_DISCOVER_REQ`
  - Returns node type, SNR, and public key prefix in discovery response
  - Rate limiting: max 4 responses per 2 minutes
  - Random delay to spread responses from multiple repeaters
- **Authenticated Request Handling** - Full support for client requests after login:
  - `GET_STATUS` - Returns core stats (battery, uptime, queue length, error flags)
  - `GET_TELEMETRY` - Returns CayenneLPP telemetry (battery, GPS if available)
  - `GET_NEIGHBOURS` - Returns list of known repeater neighbours with SNR/RSSI
  - `GET_MINMAXAVG` - Returns radio statistics
  - `GET_ACCESS_LIST` - Returns ACL entries (admin only)
  - `KEEP_ALIVE` - Acknowledges client connection
- **Encrypted Responses** - All responses encrypted with session-specific ECDH keys
- **Replay Protection** - Timestamp validation on all authenticated requests
- **New log tag** - `[DISC]` for discovery-related events

### v0.2.4
- **X25519 ECDH Encryption** - Full cryptographic support for LOGIN authentication:
  - X25519 (Curve25519) key exchange for secure shared secret derivation
  - AES-128-ECB encryption with truncated HMAC-SHA256 (2-byte MAC, encrypt-then-MAC)
  - MeshCore cipher format: `[MAC:2][ciphertext]` - MAC before ciphertext
  - ANON_REQ packet decryption: `[dest_hash:1][ephemeral_pub:32][MAC:2][ciphertext]`
  - Encrypted RESPONSE packet generation for login confirmations
- **Session Management** - Secure client session handling:
  - Up to 8 concurrent authenticated sessions
  - Timestamp-based replay attack protection
  - Automatic session timeout (1 hour inactivity)
  - Per-session ECDH shared secrets for response encryption
- **LOGIN Flow** - Complete MeshCore authentication support:
  - Receive and decrypt ANON_REQ login requests
  - Verify admin/guest passwords
  - Send encrypted LOGIN_OK responses
  - Grant appropriate permissions (ADMIN/GUEST)
- **New module** - `src/mesh/Crypto.h` with:
  - `MeshCrypto` class for ECDH and AES operations
  - `SessionManager` class for authenticated clients
  - `KeyConverter` class for Ed25519/X25519 conversion

### v0.2.3
- **MeshCore Standard Repeater Features** - Full compliance with MeshCore repeater protocol:
  - **Login/Auth System** - Admin and guest passwords with replay protection (timestamp-based)
  - **Neighbour Tracking** - Tracks up to 50 nearby repeaters with SNR/RSSI/last seen time
  - **Statistics** - Core stats, radio stats (avg/min/max RSSI/SNR), packet stats (flood vs direct)
  - **Telemetry** - CayenneLPP format encoding for battery voltage, temperature, GPS location
  - **Packet Logging** - Ring buffer of last 32 packets for debugging
  - **Region Filtering** - Optional transport code filtering (up to 4 regions)
- **New CLI Commands**:
  - `neighbours` - List tracked repeater neighbours
  - `radiostats` - Show radio statistics
  - `packetstats` - Show flood vs direct packet counts
  - `acl` - Show ACL (Access Control List) entries
  - `repeat` - Show repeat mode settings
  - `set password <pwd>` - Set admin password
  - `set guest <pwd>` - Set guest password
  - `set repeat on/off` - Enable/disable packet repeating
  - `set flood.max <n>` - Set max flood path length
  - `log` / `log clear` - View/clear packet log
- **New module** - `src/mesh/Repeater.h` with:
  - `ACLManager` class for authentication
  - `NeighbourTracker` class for neighbour management
  - `RepeaterHelper` class for stats and serialization
  - `PacketLogger` class for ring buffer logging
  - `CayenneLPP` class for telemetry encoding
  - `RegionManager` class for transport code filtering

### v0.2.2
- **LED Status Indicators** - Meaningful visual feedback:
  - **Red solid**: No time sync (waiting for first ADVERT)
  - **Blue double blink**: Time synchronized from ADVERT
  - **Green blink**: Forwarding/retransmitting a packet
  - **Violet**: Transmitting own packets (ADVERT, ping)
- **Protocol Compatibility Fix** - Fixed flags byte encoding per MeshCore specification
  - Lower 4 bits: node type (0x01=chat, 0x02=repeater, 0x03=room, 0x04=sensor)
  - Upper 4 bits: flags (0x10=location, 0x80=name, etc.)
  - Repeater now correctly identified as 0x82 (repeater + has_name)
- **Protocol Verification** - Verified against official MeshCore documentation:
  - ADVERT field order: pubkey(32) + timestamp(4) + signature(64) + appdata(variable) ✓
  - Signature calculation: signs pubkey + timestamp + appdata ✓
  - Timestamp format: 4 bytes little-endian Unix timestamp ✓
  - Location encoding: int32 * 1000000, signed, little-endian ✓
  - Packet header: route(2 bits) + type(4 bits) + version(2 bits) ✓

### v0.2.1
- **Time Synchronization** - Automatic time sync from received ADVERTs
  - First ADVERT: sync immediately and send own ADVERT after 5 seconds
  - Already synced: requires 2 matching different timestamps within 1 hour to re-sync (consensus)
  - `time` command to show current time, `time <unix>` to set manually
- **GPS Location Support** - Set and broadcast location in ADVERT packets
  - `location <lat> <lon>` command to set coordinates
  - `location clear` to remove location
  - Location encoded as int32 * 1000000 (MeshCore format)
- **ADVERT Improvements**
  - `advert interval <s>` command to change beacon interval (60-86400 seconds)
  - `advert debug` command to show raw packet bytes for debugging
  - ADVERT TX/RX statistics shown in `stats` command
- **Node Names** - Custom node names with `name <name>` command
- **Improved Nodes Table** - Shows node names from received ADVERTs
- **ADVERT Parsing** - Displays received node info (name, location, flags)
- **Fixed ADVERT Format** - Correct location encoding (int32 * 1000000, not IEEE 754 float)
- **Fixed Name Parsing** - Correct parsing without length prefix
- Improved serial console tables with box-drawing characters
- Time sync status shown in identity table (synced/pending/not synced)
- ADVERT delayed until time is synchronized

### v0.2.0
- **Ed25519 ADVERT** - Cryptographically signed node advertisements
- **Node Identity** - Persistent Ed25519 keypair stored in EEPROM
- **ADVERT Beacon** - Automatic broadcast every 5 minutes
- **Telemetry** - Battery voltage, uptime, packet statistics
- **`advert` command** - Manually send ADVERT packets
- **`identity` command** - View/reset node identity
- **`telemetry` command** - View system telemetry
- Colored serial output with ANSI codes
- Timestamp logging (HH:MM:SS format)
- Watchdog timer with automatic recovery
- Radio error counter with auto-reset
- Node ID generation from chip unique ID (using `getID()`)
- SNR-weighted TX delay (fair channel access)
- Active reception detection (collision avoidance)
- EEPROM configuration persistence
- Power mode selection (0/1/2)
- Extended serial commands
- **`ping` command** - Send test packets
- **`nodes` command** - View seen nodes with RSSI/SNR/packet count
- Seen nodes tracker (up to 16 nodes)
- Fixed RX duty cycle timing
- Fixed timing calculations
- Explicit CRC-16 configuration

### v0.1.0
- Initial release
- Basic MeshCore packet forwarding
- Deep sleep power management
- NeoPixel LED signaling

## License

MIT License - see [LICENSE](LICENSE) file.

## Acknowledgments

- [MeshCore](https://github.com/meshcore-dev/MeshCore) - Protocol specification
- [RadioLib](https://github.com/jgromes/RadioLib) - LoRa radio library
- [Crypto](https://github.com/rweather/arduinolibs) - Ed25519 cryptographic library
- [Heltec Automation](https://github.com/HelTecAutomation) - CubeCell platform

## Resources

- [MeshCore Protocol](https://github.com/meshcore-dev/MeshCore)
- [MeshCore Official Flasher](https://flasher.meshcore.co.uk/)
- [Heltec CubeCell Arduino](https://github.com/HelTecAutomation/CubeCell-Arduino)
- [RadioLib Documentation](https://jgromes.github.io/RadioLib/)

---

**Note**: This project fills a gap for CubeCell users who want to participate in MeshCore networks. However, for the best experience and full features, consider using [officially supported hardware](https://meshcore.co.uk/get.html).
