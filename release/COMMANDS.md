# CubeCellMeshCore v0.3.1 - Command Reference

Serial console at 115200 baud.

## System Commands

| Command | Description |
|---------|-------------|
| `help` | Show available commands |
| `status` | Show system status (version, uptime, memory) |
| `stats` | Show packet statistics (RX/TX/FWD counts) |
| `reboot` | Restart the device |
| `factory` | Reset to factory defaults |

## Identity Commands

| Command | Description |
|---------|-------------|
| `identity` | Show node public key (hex) |
| `name` | Show current node name |
| `name <name>` | Set node name (max 15 chars) |
| `location` | Show current GPS coordinates |
| `location <lat> <lon>` | Set GPS coordinates |

## Network Commands

| Command | Description |
|---------|-------------|
| `advert` | Send ADVERT packet immediately |
| `nodes` | List all discovered nodes |
| `contacts` | List known contacts (from login) |
| `ping` | Send test ping packet |

## Telemetry Commands

| Command | Description |
|---------|-------------|
| `telemetry` | Show telemetry (battery, uptime, stats) |
| `radio` | Show radio status and settings |

## Configuration Commands

| Command | Description |
|---------|-------------|
| `config` | Show current configuration |
| `adminpw <password>` | Set admin password |
| `guestpw <password>` | Set guest password |
| `save` | Save configuration to EEPROM |
| `load` | Load configuration from EEPROM |

## Power Management

| Command | Description |
|---------|-------------|
| `sleep on` | Enable deep sleep mode |
| `sleep off` | Disable deep sleep mode |
| `rxboost on` | Enable RX gain boost |
| `rxboost off` | Disable RX gain boost |

## Debug Commands

| Command | Description |
|---------|-------------|
| `debug on` | Enable verbose debug output |
| `debug off` | Disable verbose debug output |
| `dump` | Dump EEPROM contents |

## Remote CLI (via MeshCore app)

Once authenticated via the MeshCore app, you can send CLI commands remotely.
Admin users have full access, guest users have read-only access.

### Admin-only commands (remote)
- `name`, `location`, `adminpw`, `guestpw`
- `save`, `reboot`, `factory`
- `sleep`, `rxboost`

### Guest-allowed commands (remote)
- `status`, `stats`, `telemetry`
- `nodes`, `contacts`, `radio`

## Output Format

### Status Output Example
```
CubeCellMeshCore v0.3.0
Uptime: 01:23:45
Free RAM: 8192 bytes
Radio: OK
```

### Stats Output Example
```
RX: 1234 TX: 567 FWD: 890 ERR: 0
Last: RSSI=-65dBm SNR=8.5dB
```

### Nodes Output Example
```
Nodes seen: 3
[0] ABC123 "NodeName" -72dBm 5m ago
[1] DEF456 "Repeater2" -85dBm 12m ago
[2] GHI789 "" -91dBm 25m ago
```

## Tips

1. Always `save` after changing configuration
2. Use `status` to verify settings before saving
3. Set passwords before deploying in the field
4. Use `advert` to force discovery by nearby nodes
5. Check `stats` to monitor network health
