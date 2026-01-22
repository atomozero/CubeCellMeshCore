# CubeCellMeshCore

MeshCore-compatible repeater firmware for Heltec CubeCell HTCC-AB01.

## Features

- **MeshCore Protocol Compatible** - Works with MeshCore Android/iOS apps
- **Ed25519 Signatures** - Compact implementation saves ~97KB Flash
- **ADVERT Broadcasting** - Node discovery and time synchronization
- **Packet Forwarding** - SNR-based CSMA/CA with weighted backoff
- **Remote CLI** - Encrypted command access via mesh network
- **Low Power** - Deep sleep support with duty-cycle RX
- **Telemetry** - Battery voltage, temperature monitoring

## Hardware

- **Board**: Heltec CubeCell HTCC-AB01
- **Radio**: SX1262 LoRa transceiver
- **Flash**: 131KB (99.3% used)
- **RAM**: 16KB (49.5% used)

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
| Frequency | 869.525 MHz |
| Bandwidth | 62.5 kHz |
| Spreading Factor | SF8 |
| Coding Rate | 4/8 |
| TX Power | 22 dBm |
| Sync Word | 0x24 |

## Serial Commands

Type `help` for full command list. Key commands:

```
status          - System status
stats           - Packet statistics
advert          - Send ADVERT now
nodes           - List discovered nodes
contacts        - List known contacts
identity        - Show public key
telemetry       - Battery, temperature
name <name>     - Set node name
location <lat> <lon> - Set GPS coords
save            - Save to EEPROM
reboot          - Restart device
```

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
    ├── Packet.h      # Packet format
    ├── Repeater.h    # Forwarding stats
    └── Telemetry.h   # Sensor data

lib/
└── ed25519/          # Compact Ed25519 implementation
    ├── ge_scalarmult_base_compact.c  # Memory-optimized
    └── precomp_Bi.h  # Small precomputed table (~1KB)

docs/
├── ARCHITECTURE.md   # System architecture
└── API.md            # API reference
```

## Documentation

- [Architecture Overview](docs/ARCHITECTURE.md) - System design and data flow
- [API Reference](docs/API.md) - Module APIs and serial commands

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
- `sleep on/off` - Enable/disable deep sleep
- `rxboost on/off` - Enable/disable RX gain boost

## Dependencies

- [RadioLib](https://github.com/jgromes/RadioLib) v6.6.0
- [Crypto](https://github.com/rweather/arduinolibs) v0.4.0

## License

MIT License - See LICENSE file for details.

## Acknowledgments

- [MeshCore](https://github.com/ripplebiz/MeshCore) - Protocol specification
- [orlp/ed25519](https://github.com/orlp/ed25519) - Ed25519 reference
- [RadioLib](https://github.com/jgromes/RadioLib) - LoRa library

## Changelog

### v1.0.0 (2026-01-22)
- Initial release
- MeshCore-compatible ADVERT packets
- Ed25519 signatures working
- Packet forwarding with CSMA/CA
- Serial console commands
- Remote CLI support
